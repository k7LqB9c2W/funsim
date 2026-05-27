#include "settlements.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include "factions.h"
#include "humans.h"
#include "tech_system.h"
#include "tree_rules.h"
#include "util.h"
#include "world.h"

namespace {
constexpr int kZonePopThreshold = 10;
constexpr int kZoneRequiredDays = 3;
constexpr int kMinVillageDistTiles = 16;
constexpr int kClaimRadiusTownHall = 30;
constexpr int kClaimRadiusHouse = 10;
constexpr int kClaimRadiusFarm = 10;
constexpr int kClaimRadiusGranary = 12;
constexpr int kClaimRadiusWell = 10;
constexpr int kClaimRadiusMarket = 14;
constexpr int kClaimRadiusForge = 12;
constexpr int kInfluenceVillage = 36;
constexpr int kInfluenceTown = 48;
constexpr int kInfluenceCity = 64;
constexpr int kInfluenceCapitalBonus = 10;
constexpr int kTownPopThreshold = 40;
constexpr int kCityPopThreshold = 120;
constexpr int kTownAgeDays = 60;
constexpr int kCityAgeDays = 180;
constexpr int kTechMaxTier = 7;
constexpr int kRebellionMinPop = 20;
constexpr int kRebellionStabilityThreshold = 25;
constexpr int kRebellionUnrestDays = 7;
constexpr int kWarLossFoodFactor = 6;
constexpr int kWarLossWoodFactor = 3;
constexpr int kFoundingMinGroup = 6;
constexpr int kFoundingMaxGroup = 18;
constexpr int kFoundingParentReservePop = 8;
constexpr int kFoundingFoodPerPerson = 18;
constexpr int kFoundingWoodPerPerson = 4;
constexpr int kFoundingCooldownDays = 120;
constexpr int kFoundingParentMinPop = 60;
constexpr int kFoundingMinSiteDist = 48;
constexpr int kFoundingMaxSiteDist = 92;
constexpr int kMigrationIntervalDays = 7;
constexpr int kMigrationMinSourcePop = 18;
constexpr int kMigrationTargetHousingReserve = 4;
constexpr int kMigrationCrowdingEarlyMargin = 6;
constexpr int kFoundingCrowdingEarlyMargin = 4;
constexpr int kMigrationPressureThreshold = 22;
constexpr int kMigrationOpportunityGap = 42;
constexpr int kMigrationMaxPerInterval = 8;
constexpr int kMigrationFoundingDeferralPressure = 30;

constexpr int kGatherRadius = 12;
constexpr int kWoodRadius = 12;
constexpr int kHouseBuildRadius = 16;
constexpr int kFarmBuildRadius = 12;
constexpr int kFarmWorkRadius = 14;
constexpr int kGranaryDropRadius = 4;
constexpr int kGranaryBuildRadius = 4;
constexpr int kMarketBuildRadius = 12;
constexpr int kForgeBuildRadius = 12;
constexpr int kFarGatherRadius = 24;
constexpr int kHousingBuffer = 10;
constexpr int kDesiredFoodPerPop = 60;
constexpr int kDesiredWoodPerPop = 4;
constexpr int kDesiredStonePerPop = 2;
constexpr int kDesiredMetalPerPop = 1;
constexpr int kTradeCheckIntervalDays = 5;
constexpr int kTradeMaxDistanceTiles = 140;
constexpr int kTradeMinShipment = 8;
constexpr int kTradeMaxShipment = 90;
constexpr int kFarmsPerPop = 3;
constexpr int kWaterSearchRadius = 28;
constexpr int kFactionLinkRadiusTiles = 96;
constexpr int kEmergencyFoodPerPop = 12;
constexpr int kEmergencyFarmerPct = 60;
constexpr int kEmergencyGathererPct = 60;
constexpr int kWellSourceRadius = 6;
constexpr int kWellRadiusStrong = 12;
constexpr int kWellRadiusMedium = 6;
constexpr int kWellRadiusWeak = 3;
constexpr int kWellRadiusTiny = 1;
constexpr int kWellWaterScentThreshold = 18000;

uint32_t Hash32(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B9u;
  h ^= b * 0x85EBCA6Bu;
  h ^= (h >> 13);
  h *= 0xC2B2AE35u;
  h ^= (h >> 16);
  return h;
}

int DesiredFoundingGroupSize(int parentPop) {
  if (parentPop <= kFoundingParentReservePop + kFoundingMinGroup) return 0;
  int group = std::max(kFoundingMinGroup, parentPop / 12);
  group = std::min(group, kFoundingMaxGroup);
  return std::min(group, parentPop - kFoundingParentReservePop);
}

void TransferFoundingResources(Settlement& source, Settlement& founded, int founders) {
  if (founders <= 0) return;

  int foodReserve = std::max(0, source.population) * kEmergencyFoodPerPop;
  int foodAvailable = std::max(0, source.stockFood - foodReserve);
  int foodTransfer = std::min(foodAvailable, founders * kFoundingFoodPerPerson);
  source.stockFood = std::max(0, source.stockFood - foodTransfer);
  founded.stockFood += foodTransfer;

  int woodTransfer = std::min(source.stockWood, std::max(8, founders * kFoundingWoodPerPerson));
  source.stockWood = std::max(0, source.stockWood - woodTransfer);
  founded.stockWood += woodTransfer;
}

bool IsEligibleHumanFounder(const Human& human, int sourceSettlementId) {
  if (!human.alive) return false;
  if (sourceSettlementId > 0) {
    if (human.settlementId != sourceSettlementId) return false;
  } else if (human.settlementId != -1) {
    return false;
  }
  if (human.role == Role::Soldier || human.role == Role::Guard) return false;
  if (human.armyState != ArmyState::Idle || human.warId > 0) return false;
  if (human.hasTask || human.carrying) return false;
  if (human.goal == Goal::FleeFire || human.goal == Goal::GoToTask) return false;
  return true;
}

int CountEligibleFoundingHumans(const HumanManager& humans, int sourceSettlementId, int centerX,
                                int centerY, int maxDistSq) {
  int count = 0;
  const auto& list = humans.Humans();
  for (const Human& human : list) {
    if (!IsEligibleHumanFounder(human, sourceSettlementId)) continue;
    if (maxDistSq >= 0) {
      int dx = human.x - centerX;
      int dy = human.y - centerY;
      if (dx * dx + dy * dy > maxDistSq) continue;
    }
    count++;
  }
  return count;
}

int AssignFoundingHumans(HumanManager& humans, int sourceSettlementId, const Settlement& founded,
                         int desiredFounders, int maxDistSq) {
  if (desiredFounders <= 0) return 0;

  struct Candidate {
    int distanceSq = 0;
    int index = -1;
  };

  std::vector<Candidate> candidates;
  auto& list = humans.HumansMutable();
  candidates.reserve(static_cast<size_t>(desiredFounders) * 2u);
  for (int i = 0; i < static_cast<int>(list.size()); ++i) {
    const Human& human = list[i];
    if (!IsEligibleHumanFounder(human, sourceSettlementId)) continue;
    int dx = human.x - founded.centerX;
    int dy = human.y - founded.centerY;
    int distSq = dx * dx + dy * dy;
    if (maxDistSq >= 0 && distSq > maxDistSq) continue;
    candidates.push_back(Candidate{distSq, i});
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.distanceSq < b.distanceSq;
  });

  int moved = 0;
  for (const Candidate& candidate : candidates) {
    if (moved >= desiredFounders) break;
    if (candidate.index < 0 || candidate.index >= static_cast<int>(list.size())) continue;
    Human& human = list[candidate.index];
    if (!IsEligibleHumanFounder(human, sourceSettlementId)) continue;

    human.settlementId = founded.id;
    human.homeX = founded.centerX;
    human.homeY = founded.centerY;
    human.targetX = founded.centerX;
    human.targetY = founded.centerY;
    human.goal = Goal::StayHome;
    human.role = Role::Idle;
    human.hasTask = false;
    human.taskSettlementId = -1;
    human.forceReplan = true;
    moved++;
  }

  return moved;
}

int TransferFoundingMacroPopulation(Settlement& source, Settlement& founded, int desiredFounders) {
  if (desiredFounders <= 0) return 0;

  int moved = 0;
  constexpr int kPreferredBins[] = {3, 2, 4, 1, 0, 5};
  for (int bin : kPreferredBins) {
    while (moved < desiredFounders && (source.macroPopF[bin] > 0 || source.macroPopM[bin] > 0)) {
      if (source.macroPopF[bin] > 0) {
        source.macroPopF[bin]--;
        founded.macroPopF[bin]++;
        moved++;
      }
      if (moved >= desiredFounders) break;
      if (source.macroPopM[bin] > 0) {
        source.macroPopM[bin]--;
        founded.macroPopM[bin]++;
        moved++;
      }
    }
  }

  source.population = source.MacroTotal();
  founded.population = founded.MacroTotal();
  return moved;
}

int TierOpportunityBonus(SettlementTier tier) {
  switch (tier) {
    case SettlementTier::Town:
      return 15;
    case SettlementTier::City:
      return 35;
    default:
      return 0;
  }
}

int SettlementOpportunityScore(const Settlement& settlement) {
  int pop = std::max(1, settlement.population);
  int spareHousing = settlement.housingCap - settlement.population;
  int foodSurplus = settlement.stockFood - pop * kEmergencyFoodPerPop;
  int idleRatio = (settlement.idle * 100) / pop;
  int diseasePressure = settlement.diseaseInfected / std::max(1, pop / 20 + 1);

  int score = settlement.stability + settlement.economyProsperity -
              settlement.economyStress + TierOpportunityBonus(settlement.tier);
  score += std::max(-50, std::min(80, spareHousing * 3));
  score += std::max(-40, std::min(50, foodSurplus / std::max(1, pop)));
  score -= settlement.warPressure * 16;
  score -= settlement.borderPressure * 4;
  score -= diseasePressure * 3;
  if (idleRatio > 20) {
    score -= std::min(35, idleRatio - 20);
  }
  if (settlement.isCapital) {
    score += 8;
  }
  return score;
}

int SettlementMigrationPressure(const Settlement& settlement) {
  int pop = settlement.population;
  if (pop < kMigrationMinSourcePop) return 0;

  int crowdedAt = std::max(0, settlement.housingCap - kMigrationCrowdingEarlyMargin);
  int overcrowded = (settlement.housingCap > 0) ? std::max(0, pop - crowdedAt)
                                                : std::max(0, pop - kMigrationMinSourcePop);
  int foodDeficit = std::max(0, pop * kEmergencyFoodPerPop - settlement.stockFood);
  int idlePressure = std::max(0, settlement.idle - std::max(2, pop / 5));
  int lowStability = std::max(0, 62 - settlement.stability);
  int diseasePressure = settlement.diseaseInfected / std::max(1, pop / 18 + 1);

  int pressure = overcrowded * 5 + foodDeficit / std::max(1, pop / 10 + 1) +
                 idlePressure * 2 + lowStability + settlement.economyStress / 2 +
                 settlement.warPressure * 12 + settlement.borderPressure * 3 +
                 diseasePressure * 4;
  if (pop >= 160) {
    pressure += (pop - 150) / 2;
  }
  return std::max(0, pressure);
}

bool IsEligibleHumanMigrant(const Human& human, int sourceSettlementId) {
  if (!IsEligibleHumanFounder(human, sourceSettlementId)) return false;
  if (human.ageDays < Human::kAdultAgeDays) return false;
  if (human.pregnant) return false;
  return true;
}

int AssignMigratingHumans(HumanManager& humans, int sourceSettlementId, const Settlement& target,
                          int desiredMigrants, Random& rng) {
  if (desiredMigrants <= 0) return 0;

  struct Candidate {
    int score = 0;
    int index = -1;
  };

  std::vector<Candidate> candidates;
  auto& list = humans.HumansMutable();
  candidates.reserve(static_cast<size_t>(desiredMigrants) * 3u);
  for (int i = 0; i < static_cast<int>(list.size()); ++i) {
    const Human& human = list[i];
    if (!IsEligibleHumanMigrant(human, sourceSettlementId)) continue;
    int score = static_cast<int>(human.wanderlust) + rng.RangeInt(0, 40);
    if (HumanHasTrait(human.traits, HumanTrait::Curious)) score += 35;
    if (HumanHasTrait(human.traits, HumanTrait::Ambitious)) score += 25;
    if (HumanHasTrait(human.traits, HumanTrait::Lazy)) score -= 30;
    candidates.push_back(Candidate{score, i});
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.score > b.score;
  });

  int moved = 0;
  for (const Candidate& candidate : candidates) {
    if (moved >= desiredMigrants) break;
    if (candidate.index < 0 || candidate.index >= static_cast<int>(list.size())) continue;
    Human& human = list[candidate.index];
    if (!IsEligibleHumanMigrant(human, sourceSettlementId)) continue;

    human.settlementId = target.id;
    human.homeX = target.centerX;
    human.homeY = target.centerY;
    human.targetX = target.centerX;
    human.targetY = target.centerY;
    human.goal = Goal::StayHome;
    human.role = Role::Idle;
    human.hasTask = false;
    human.taskSettlementId = -1;
    human.mateTargetId = -1;
    human.forceReplan = true;
    moved++;
  }

  return moved;
}

int TransferMigratingMacroPopulation(Settlement& source, Settlement& target, int desiredMigrants) {
  if (desiredMigrants <= 0) return 0;

  int moved = 0;
  constexpr int kPreferredBins[] = {3, 2, 4, 1, 5, 0};
  for (int bin : kPreferredBins) {
    while (moved < desiredMigrants && (source.macroPopF[bin] > 0 || source.macroPopM[bin] > 0)) {
      if (source.macroPopM[bin] > 0) {
        source.macroPopM[bin]--;
        target.macroPopM[bin]++;
        moved++;
      }
      if (moved >= desiredMigrants) break;
      if (source.macroPopF[bin] > 0) {
        source.macroPopF[bin]--;
        target.macroPopF[bin]++;
        moved++;
      }
    }
  }

  source.population = source.MacroTotal();
  target.population = target.MacroTotal();
  return moved;
}

bool IsBuildableTile(const World& world, int x, int y) {
  if (!world.InBounds(x, y)) return false;
  const Tile& tile = world.At(x, y);
  if (tile.type != TileType::Land) return false;
  if (tile.burning) return false;
  if (tile.building != BuildingType::None) return false;
  if (TreeRules::BlocksBuildingPlacement(world, x, y)) return false;
  return true;
}

int ClaimRadiusForBuilding(BuildingType type) {
  switch (type) {
    case BuildingType::TownHall:
      return kClaimRadiusTownHall;
    case BuildingType::House:
      return kClaimRadiusHouse;
    case BuildingType::Farm:
      return kClaimRadiusFarm;
    case BuildingType::Granary:
      return kClaimRadiusGranary;
    case BuildingType::Well:
      return kClaimRadiusWell;
    case BuildingType::Market:
      return kClaimRadiusMarket;
    case BuildingType::Forge:
      return kClaimRadiusForge;
    case BuildingType::Monument:
    case BuildingType::Archive:
    case BuildingType::Walls:
    case BuildingType::Barracks:
    case BuildingType::Mint:
    case BuildingType::School:
      return TechSystem::ClaimRadiusForBuilding(type);
    default:
      return 0;
  }
}

bool IsBuildableTileForSettlement(const World& world, const SettlementManager& settlements,
                                  int settlementId, int x, int y) {
  if (!IsBuildableTile(world, x, y)) return false;
  if (settlementId <= 0) return true;
  int ownerId = settlements.ZoneOwnerForTile(x, y);
  return ownerId == -1 || ownerId == settlementId;
}

int DepositRoll(int settlementId, int x, int y, uint32_t salt, int minValue, int spread) {
  uint32_t h = Hash32(static_cast<uint32_t>(settlementId) ^ salt,
                      static_cast<uint32_t>(x * 31 + y * 17));
  return minValue + static_cast<int>(h % static_cast<uint32_t>(std::max(1, spread)));
}

void InitializeSettlementDeposits(Settlement& settlement) {
  uint32_t h = Hash32(static_cast<uint32_t>(settlement.centerX),
                      static_cast<uint32_t>(settlement.centerY ^ settlement.id));
  settlement.stoneDeposit = DepositRoll(settlement.id, settlement.centerX, settlement.centerY,
                                        0x53544f4eu, 180, 520);
  settlement.metalDeposit = DepositRoll(settlement.id, settlement.centerX, settlement.centerY,
                                        0x4d455441u, 40, 240);
  settlement.goldDeposit = 0;
  if ((h % 100u) < 35u) {
    settlement.goldDeposit = DepositRoll(settlement.id, settlement.centerX, settlement.centerY,
                                         0x474f4c44u, 12, 95);
  }
}

int TradeResourceIndex(TradeResource resource) {
  int index = static_cast<int>(resource);
  if (index < 0 || index >= kTradeResourceCount) return 0;
  return index;
}

int DesiredStockForResource(const Settlement& settlement, TradeResource resource);
int StockForResource(const Settlement& settlement, TradeResource resource);

void UpdateSupplyDemand(Settlement& settlement) {
  constexpr TradeResource kResources[] = {
      TradeResource::Food, TradeResource::Wood, TradeResource::Stone,
      TradeResource::Metal, TradeResource::Gold};

  for (TradeResource resource : kResources) {
    int desired = std::max(1, DesiredStockForResource(settlement, resource));
    int stock = std::max(0, StockForResource(settlement, resource));
    int shortage = std::max(0, desired - stock);
    int surplus = std::max(0, stock - desired);

    int demandScore = std::min(100, (shortage * 100) / desired);
    int supplyScore = std::min(100, (surplus * 100) / std::max(1, desired));
    int valueScore = 50 + demandScore / 2 - supplyScore / 3;
    if (resource == TradeResource::Food && stock < std::max(1, settlement.population * kEmergencyFoodPerPop)) {
      valueScore += 20;
    }
    if (resource == TradeResource::Gold && settlement.isCapital) {
      valueScore += 8;
    }
    valueScore = std::max(0, std::min(100, valueScore));

    ResourcePressure& pressure = settlement.resourcePressure[TradeResourceIndex(resource)];
    pressure.desired = desired;
    pressure.stock = stock;
    pressure.shortage = shortage;
    pressure.surplus = surplus;
    pressure.demandScore = demandScore;
    pressure.supplyScore = supplyScore;
    pressure.valueScore = valueScore;
  }
}

void UpdateEconomyMood(Settlement& settlement) {
  UpdateSupplyDemand(settlement);
  const auto& food = settlement.resourcePressure[TradeResourceIndex(TradeResource::Food)];
  const auto& wood = settlement.resourcePressure[TradeResourceIndex(TradeResource::Wood)];
  const auto& stone = settlement.resourcePressure[TradeResourceIndex(TradeResource::Stone)];
  const auto& metal = settlement.resourcePressure[TradeResourceIndex(TradeResource::Metal)];
  int pop = std::max(1, settlement.population);

  int stress = 0;
  if (settlement.stockFood < pop * kEmergencyFoodPerPop) stress += 45;
  stress += food.demandScore / 3;
  stress += wood.demandScore / 5;
  if (settlement.tier != SettlementTier::Village) stress += stone.demandScore / 8;
  if (settlement.soldiers > 0) stress += metal.demandScore / 8;
  stress += std::min(20, settlement.unrest * 2);
  stress += std::min(20, settlement.warPressure * 2);
  settlement.economyStress = std::max(0, std::min(100, stress));

  int prosperity = 0;
  prosperity += std::min(22, food.supplyScore / 3);
  prosperity += std::min(14, wood.supplyScore / 5);
  prosperity += std::min(10, stone.supplyScore / 6);
  prosperity += std::min(12, metal.supplyScore / 5);
  if (settlement.markets > 0) prosperity += 8;
  if (settlement.forges > 0) prosperity += 6;
  prosperity += settlement.archives * TechSystem::ProsperityYieldForBuilding(BuildingType::Archive);
  prosperity += settlement.mints * TechSystem::ProsperityYieldForBuilding(BuildingType::Mint);
  prosperity += settlement.schools * TechSystem::ProsperityYieldForBuilding(BuildingType::School);
  prosperity += settlement.workshops * TechSystem::ProsperityYieldForBuilding(BuildingType::Workshop);
  prosperity += settlement.banks * TechSystem::ProsperityYieldForBuilding(BuildingType::Bank);
  prosperity += settlement.universities * TechSystem::ProsperityYieldForBuilding(BuildingType::University);
  prosperity += settlement.factories * TechSystem::ProsperityYieldForBuilding(BuildingType::Factory);
  prosperity += settlement.powerPlants * TechSystem::ProsperityYieldForBuilding(BuildingType::PowerPlant);
  prosperity += settlement.airfields * TechSystem::ProsperityYieldForBuilding(BuildingType::Airfield);
  prosperity += settlement.reactors * TechSystem::ProsperityYieldForBuilding(BuildingType::Reactor);
  prosperity += settlement.satelliteArrays *
                TechSystem::ProsperityYieldForBuilding(BuildingType::SatelliteArray);
  prosperity += settlement.roboticsLabs *
                TechSystem::ProsperityYieldForBuilding(BuildingType::RoboticsLab);
  prosperity += settlement.harbors * TechSystem::ProsperityYieldForBuilding(BuildingType::Harbor);
  prosperity += settlement.railDepots * TechSystem::ProsperityYieldForBuilding(BuildingType::RailDepot);
  prosperity += settlement.hospitals * TechSystem::ProsperityYieldForBuilding(BuildingType::Hospital);
  prosperity += settlement.dataCenters * TechSystem::ProsperityYieldForBuilding(BuildingType::DataCenter);
  prosperity += settlement.militaryHQs * TechSystem::ProsperityYieldForBuilding(BuildingType::MilitaryHQ);
  prosperity += std::min(22, settlement.stockGold / std::max(1, pop / 3 + 1));
  prosperity += std::min(10, settlement.techTier * 3);
  prosperity += settlement.isCapital ? 8 : 0;
  prosperity -= settlement.economyStress / 2;
  settlement.economyProsperity = std::max(0, std::min(100, prosperity));
}

int DesiredStockForResource(const Settlement& settlement, TradeResource resource) {
  int pop = std::max(1, settlement.population);
  switch (resource) {
    case TradeResource::Food:
      return pop * kDesiredFoodPerPop;
    case TradeResource::Wood: {
      int desired = pop * kDesiredWoodPerPop;
      if (settlement.housingCap < pop + kHousingBuffer) desired += Settlement::kHouseWoodCost * 2;
      if (settlement.markets == 0 &&
          (settlement.tier != SettlementTier::Village || settlement.population >= 45 ||
           settlement.isCapital)) {
        desired += Settlement::kMarketWoodCost;
      }
      if (settlement.forges == 0 &&
          (settlement.techTier >= 1 || settlement.population >= 55 || settlement.soldiers >= 8)) {
        desired += Settlement::kForgeWoodCost;
      }
      return desired;
    }
    case TradeResource::Stone: {
      int desired = pop * kDesiredStonePerPop + settlement.houses + settlement.townHalls * 8;
      if (settlement.markets == 0 &&
          (settlement.tier != SettlementTier::Village || settlement.population >= 45 ||
           settlement.isCapital)) {
        desired += Settlement::kMarketStoneCost;
      }
      if (settlement.forges == 0 &&
          (settlement.techTier >= 1 || settlement.population >= 55 || settlement.soldiers >= 8)) {
        desired += Settlement::kForgeStoneCost;
      }
      return desired;
    }
    case TradeResource::Metal: {
      int desired = std::max(8, pop * kDesiredMetalPerPop / 2 + settlement.soldiers);
      if (settlement.forges == 0 &&
          (settlement.techTier >= 1 || settlement.population >= 55 || settlement.soldiers >= 8)) {
        desired += Settlement::kForgeMetalCost;
      }
      if (settlement.warPressure > 0) desired += std::max(4, settlement.soldiers / 2);
      return desired;
    }
    case TradeResource::Gold:
      return settlement.isCapital ? std::max(12, pop / 3) : std::max(4, pop / 8);
    default:
      return 0;
  }
}

int& StockForResource(Settlement& settlement, TradeResource resource) {
  switch (resource) {
    case TradeResource::Food:
      return settlement.stockFood;
    case TradeResource::Wood:
      return settlement.stockWood;
    case TradeResource::Stone:
      return settlement.stockStone;
    case TradeResource::Metal:
      return settlement.stockMetal;
    case TradeResource::Gold:
      return settlement.stockGold;
    default:
      return settlement.stockFood;
  }
}

int StockForResource(const Settlement& settlement, TradeResource resource) {
  switch (resource) {
    case TradeResource::Food:
      return settlement.stockFood;
    case TradeResource::Wood:
      return settlement.stockWood;
    case TradeResource::Stone:
      return settlement.stockStone;
    case TradeResource::Metal:
      return settlement.stockMetal;
    case TradeResource::Gold:
      return settlement.stockGold;
    default:
      return 0;
  }
}

int BuildingCount(const Settlement& settlement, BuildingType building) {
  switch (building) {
    case BuildingType::Monument:
      return settlement.monuments;
    case BuildingType::Archive:
      return settlement.archives;
    case BuildingType::Walls:
      return settlement.walls;
    case BuildingType::Barracks:
      return settlement.barracks;
    case BuildingType::Mint:
      return settlement.mints;
    case BuildingType::School:
      return settlement.schools;
    case BuildingType::Workshop:
      return settlement.workshops;
    case BuildingType::Bank:
      return settlement.banks;
    case BuildingType::University:
      return settlement.universities;
    case BuildingType::Factory:
      return settlement.factories;
    case BuildingType::PowerPlant:
      return settlement.powerPlants;
    case BuildingType::Airfield:
      return settlement.airfields;
    case BuildingType::Reactor:
      return settlement.reactors;
    case BuildingType::SatelliteArray:
      return settlement.satelliteArrays;
    case BuildingType::RoboticsLab:
      return settlement.roboticsLabs;
    case BuildingType::Harbor:
      return settlement.harbors;
    case BuildingType::RailDepot:
      return settlement.railDepots;
    case BuildingType::Hospital:
      return settlement.hospitals;
    case BuildingType::DataCenter:
      return settlement.dataCenters;
    case BuildingType::MilitaryHQ:
      return settlement.militaryHQs;
    default:
      return 0;
  }
}

bool IsWaterTile(TileType type) {
  return type == TileType::Ocean || type == TileType::FreshWater;
}

int WaterAdjacencyScore(const World& world, int cx, int cy, int radius) {
  int best = 0;
  for (int dy = -radius; dy <= radius; ++dy) {
    int y = cy + dy;
    if (y < 0 || y >= world.height()) continue;
    for (int dx = -radius; dx <= radius; ++dx) {
      int x = cx + dx;
      if (x < 0 || x >= world.width()) continue;
      int dist = std::abs(dx) + std::abs(dy);
      if (dist > radius) continue;
      if (IsWaterTile(world.At(x, y).type)) {
        best = std::max(best, radius - dist + 1);
      }
    }
  }
  return best;
}

bool SettlementNearWater(const World& world, const Settlement& settlement, int radius) {
  for (int dy = -radius; dy <= radius; ++dy) {
    int y = settlement.centerY + dy;
    if (y < 0 || y >= world.height()) continue;
    for (int dx = -radius; dx <= radius; ++dx) {
      int x = settlement.centerX + dx;
      if (x < 0 || x >= world.width()) continue;
      if (IsWaterTile(world.At(x, y).type)) return true;
    }
  }
  return false;
}

bool CanAffordBuilding(const Settlement& settlement, BuildingType building) {
  TechSystem::BuildingCost cost = TechSystem::CostForBuilding(building);
  return settlement.stockWood >= cost.wood &&
         settlement.stockStone >= cost.stone &&
         settlement.stockMetal >= cost.metal;
}

void PayBuildingCost(Settlement& settlement, BuildingType building) {
  TechSystem::BuildingCost cost = TechSystem::CostForBuilding(building);
  settlement.stockWood = std::max(0, settlement.stockWood - cost.wood);
  settlement.stockStone = std::max(0, settlement.stockStone - cost.stone);
  settlement.stockMetal = std::max(0, settlement.stockMetal - cost.metal);
}

bool ShouldBuildTechBuilding(const World& world, const Settlement& settlement, const Faction* faction,
                             BuildingType building) {
  if (!faction || !TechSystem::IsUnlocked(faction->unlockedTechs, building)) return false;
  if (BuildingCount(settlement, building) > 0) return false;
  if (settlement.population <= 0) return false;

  switch (building) {
    case BuildingType::Monument:
      return settlement.population >= 12 || settlement.isCapital;
    case BuildingType::Archive:
      return settlement.population >= 28 || settlement.isCapital;
    case BuildingType::Walls:
      return settlement.population >= 35 &&
             (settlement.isCapital || settlement.borderPressure > 1 || settlement.warPressure > 0);
    case BuildingType::Barracks:
      return settlement.population >= 35 &&
             (settlement.isCapital || settlement.soldiers >= 3 ||
              settlement.borderPressure > 1 || settlement.warPressure > 0);
    case BuildingType::Mint:
      return settlement.population >= 45 &&
             (settlement.isCapital || settlement.stockGold >= 8 || settlement.lastGoldProduced > 0);
    case BuildingType::School:
      return settlement.population >= 55 || settlement.isCapital;
    case BuildingType::Workshop:
      return settlement.population >= 55 &&
             (settlement.isCapital || settlement.forges > 0 || settlement.stockMetal >= 12);
    case BuildingType::Bank:
      return settlement.population >= 70 &&
             (settlement.isCapital || settlement.markets > 0 || settlement.mints > 0 ||
              settlement.stockGold >= 18);
    case BuildingType::University:
      return settlement.population >= 80 &&
             (settlement.isCapital || settlement.schools > 0 || settlement.archives > 0);
    case BuildingType::Factory:
      return settlement.population >= 95 &&
             (settlement.isCapital || settlement.workshops > 0 || settlement.forges > 0);
    case BuildingType::PowerPlant:
      return settlement.population >= 110 &&
             (settlement.isCapital || settlement.factories > 0 || settlement.stockMetal >= 30);
    case BuildingType::Airfield:
      return settlement.population >= 90 &&
             (settlement.isCapital || settlement.soldiers >= 8 || settlement.borderPressure > 2);
    case BuildingType::Reactor:
      return settlement.population >= 125 &&
             (settlement.isCapital || settlement.powerPlants > 0);
    case BuildingType::SatelliteArray:
      return settlement.population >= 115 &&
             (settlement.isCapital || settlement.universities > 0 || settlement.airfields > 0);
    case BuildingType::RoboticsLab:
      return settlement.population >= 130 &&
             (settlement.isCapital || settlement.factories > 0 || settlement.universities > 0);
    case BuildingType::Harbor:
      return settlement.population >= 75 && SettlementNearWater(world, settlement, 22);
    case BuildingType::RailDepot:
      return settlement.population >= 105 &&
             (settlement.isCapital || settlement.factories > 0 || settlement.markets > 0);
    case BuildingType::Hospital:
      return settlement.population >= 100 &&
             (settlement.isCapital || settlement.schools > 0 || settlement.universities > 0);
    case BuildingType::DataCenter:
      return settlement.population >= 135 &&
             (settlement.isCapital || settlement.satelliteArrays > 0 || settlement.universities > 0);
    case BuildingType::MilitaryHQ:
      return settlement.population >= 100 &&
             (settlement.isCapital || settlement.airfields > 0 || settlement.soldiers >= 12);
    default:
      return false;
  }
}
}  // namespace

const char* TradeResourceName(TradeResource resource) {
  switch (resource) {
    case TradeResource::Food:
      return "food";
    case TradeResource::Wood:
      return "wood";
    case TradeResource::Stone:
      return "stone";
    case TradeResource::Metal:
      return "metal";
    case TradeResource::Gold:
      return "gold";
    default:
      return "unknown";
  }
}

const char* SettlementTierName(SettlementTier tier) {
  switch (tier) {
    case SettlementTier::Village:
      return "village";
    case SettlementTier::Town:
      return "town";
    case SettlementTier::City:
      return "city";
    default:
      return "unknown";
  }
}

int HouseCapacityForTier(int tier) {
  float mult = 1.0f + 0.12f * static_cast<float>(tier);
  int value = static_cast<int>(std::round(static_cast<float>(Settlement::kHouseCapacity) * mult));
  return std::max(1, value);
}

int TownHallCapacityForTier(int tier) {
  float mult = 1.0f + 0.18f * static_cast<float>(tier);
  int value = static_cast<int>(std::round(static_cast<float>(Settlement::kTownHallCapacity) * mult));
  return std::max(1, value);
}

int FarmYieldForTier(int tier) {
  float mult = 1.0f + 0.2f * static_cast<float>(tier);
  int value = static_cast<int>(std::round(static_cast<float>(Settlement::kFarmYield) * mult));
  return std::max(1, value);
}

int GatherYieldForTier(int tier) {
  int value = 1 + (tier / 2);
  return std::max(1, value);
}

int FarmsPerPopForTier(int tier) {
  int value = kFarmsPerPop - tier;
  if (value < 1) value = 1;
  return value;
}

bool SettlementManager::HasSettlement(int settlementId) const {
  for (const auto& settlement : settlements_) {
    if (settlement.id == settlementId) return true;
  }
  return false;
}

const Settlement* SettlementManager::Get(int settlementId) const {
  if (settlementId >= 0 && settlementId < static_cast<int>(idToIndex_.size())) {
    int idx = idToIndex_[settlementId];
    if (idx >= 0 && idx < static_cast<int>(settlements_.size())) {
      return &settlements_[idx];
    }
  }
  for (const auto& settlement : settlements_) {
    if (settlement.id == settlementId) return &settlement;
  }
  return nullptr;
}

Settlement* SettlementManager::GetMutable(int settlementId) {
  if (settlementId >= 0 && settlementId < static_cast<int>(idToIndex_.size())) {
    int idx = idToIndex_[settlementId];
    if (idx >= 0 && idx < static_cast<int>(settlements_.size())) {
      return &settlements_[idx];
    }
  }
  for (auto& settlement : settlements_) {
    if (settlement.id == settlementId) return &settlement;
  }
  return nullptr;
}

int SettlementManager::ZoneOwnerForTile(int x, int y) const {
  if (zonesX_ == 0 || zonesY_ == 0) return -1;
  int zx = x / zoneSize_;
  int zy = y / zoneSize_;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return -1;
  return ZoneOwnerAt(zx, zy);
}

int SettlementManager::ZoneOwnerAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return -1;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return -1;
  int idx = zy * zonesX_ + zx;
  if (idx < 0 || idx >= static_cast<int>(zoneOwnerStampByIndex_.size())) return -1;
  return (zoneOwnerStampByIndex_[static_cast<size_t>(idx)] == zoneOwnerGeneration_)
             ? zoneOwnerByIndex_[static_cast<size_t>(idx)]
             : -1;
}

int SettlementManager::ZonePopAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return 0;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return 0;
  int idx = zy * zonesX_ + zx;
  if (idx < 0 || idx >= static_cast<int>(zonePopStampByIndex_.size())) return 0;
  return (zonePopStampByIndex_[static_cast<size_t>(idx)] == zonePopGeneration_)
             ? zonePopByIndex_[static_cast<size_t>(idx)]
             : 0;
}

int SettlementManager::ZoneFoundingPopAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return 0;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return 0;
  int idx = zy * zonesX_ + zx;
  if (idx < 0 || idx >= static_cast<int>(zonePopStampByIndex_.size())) return 0;
  return (zonePopStampByIndex_[static_cast<size_t>(idx)] == zonePopGeneration_)
             ? zoneFoundingPopByIndex_[static_cast<size_t>(idx)]
             : 0;
}

int SettlementManager::ZoneConflictAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return 0;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return 0;
  int idx = zy * zonesX_ + zx;
  if (idx < 0 || idx >= static_cast<int>(zoneConflictStampByIndex_.size())) return 0;
  return (zoneConflictStampByIndex_[static_cast<size_t>(idx)] == zoneConflictGeneration_)
	             ? zoneConflictByIndex_[static_cast<size_t>(idx)]
	             : 0;
}

int SettlementManager::FindMigrationDestinationIndex(int sourceIndex) const {
  if (sourceIndex < 0 || sourceIndex >= static_cast<int>(settlements_.size())) return -1;
  const Settlement& source = settlements_[sourceIndex];
  if (source.population < kMigrationMinSourcePop || source.factionId <= 0) return -1;

  int sourceOpportunity = SettlementOpportunityScore(source);
  int pressure = SettlementMigrationPressure(source);
  int bestIndex = -1;
  int bestScore = std::numeric_limits<int>::min();

  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    if (i == sourceIndex) continue;
    const Settlement& target = settlements_[i];
    if (target.factionId != source.factionId || target.population <= 0) continue;
    int spareHousing = target.housingCap - target.population - kMigrationTargetHousingReserve;
    if (spareHousing <= 0) continue;
    if (target.stockFood < target.population * kEmergencyFoodPerPop) continue;
    if (target.stability < 38) continue;
    if (target.warPressure > source.warPressure && source.warPressure == 0) continue;

    int opportunity = SettlementOpportunityScore(target);
    int opportunityGap = opportunity - sourceOpportunity;
    if (pressure < kMigrationPressureThreshold && opportunityGap < kMigrationOpportunityGap) {
      continue;
    }

    int score = opportunityGap + pressure / 3 + std::min(50, spareHousing * 2) -
                target.warPressure * 8;
    if (score > bestScore) {
      bestScore = score;
      bestIndex = i;
    }
  }

  return bestIndex;
}

bool SettlementManager::HasMigrationReliefOption(int sourceIndex) const {
  if (sourceIndex < 0 || sourceIndex >= static_cast<int>(settlements_.size())) return false;
  const Settlement& source = settlements_[sourceIndex];
  if (SettlementMigrationPressure(source) < kMigrationFoundingDeferralPressure) return false;
  return FindMigrationDestinationIndex(sourceIndex) >= 0;
}

void SettlementManager::RunMigration(HumanManager* humans, Random& rng, int dayCount) {
  if (dayCount % kMigrationIntervalDays != 0) return;
  if (settlements_.size() < 2) return;

  if (idToIndex_.size() != static_cast<size_t>(nextId_)) {
    idToIndex_.assign(nextId_, -1);
    for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
      if (settlements_[i].id >= 0 && settlements_[i].id < static_cast<int>(idToIndex_.size())) {
        idToIndex_[settlements_[i].id] = i;
      }
    }
  }

  for (int sourceIndex = 0; sourceIndex < static_cast<int>(settlements_.size()); ++sourceIndex) {
    int targetIndex = FindMigrationDestinationIndex(sourceIndex);
    if (targetIndex < 0 || targetIndex == sourceIndex) continue;
    if (targetIndex >= static_cast<int>(settlements_.size())) continue;

    Settlement& source = settlements_[sourceIndex];
    Settlement& target = settlements_[targetIndex];
    int pressure = SettlementMigrationPressure(source);
    int opportunityGap = SettlementOpportunityScore(target) - SettlementOpportunityScore(source);
    if (pressure < kMigrationPressureThreshold && opportunityGap < kMigrationOpportunityGap) {
      continue;
    }

    int targetCapacity = target.housingCap - target.population - kMigrationTargetHousingReserve;
    int sourceReserve = std::max(kMigrationMinSourcePop / 2, kFoundingParentReservePop);
    int sourceAvailable = source.population - sourceReserve;
    if (targetCapacity <= 0 || sourceAvailable <= 0) continue;

    int desired = 1;
    if (pressure >= kMigrationPressureThreshold) {
      desired += pressure / 28;
    }
    if (opportunityGap >= kMigrationOpportunityGap) {
      desired += 1;
    }
    desired = std::min(desired, kMigrationMaxPerInterval);
    desired = std::min(desired, targetCapacity);
    desired = std::min(desired, sourceAvailable);
    if (desired <= 0) continue;

    int moved = 0;
    if (humans) {
      moved = AssignMigratingHumans(*humans, source.id, target, desired, rng);
    } else {
      moved = TransferMigratingMacroPopulation(source, target, desired);
    }
    if (moved <= 0) continue;

    if (humans) {
      source.population = std::max(0, source.population - moved);
      target.population += moved;
    }
  }
}

int SettlementManager::ConsumeWarDeaths() {
  int count = warDeathsPending_;
  warDeathsPending_ = 0;
  return count;
}

void SettlementManager::EnsureZoneBuffers(const World& world) {
  int neededZonesX = (world.width() + zoneSize_ - 1) / zoneSize_;
  int neededZonesY = (world.height() + zoneSize_ - 1) / zoneSize_;
  if (neededZonesX == zonesX_ && neededZonesY == zonesY_) return;

  zonesX_ = neededZonesX;
  zonesY_ = neededZonesY;

  const int totalZones = std::max(0, zonesX_ * zonesY_);
  zonePopGeneration_ = 1;
  zonePopStampByIndex_.assign(static_cast<size_t>(totalZones), 0u);
  zonePopByIndex_.assign(static_cast<size_t>(totalZones), 0);
  zoneFoundingPopByIndex_.assign(static_cast<size_t>(totalZones), 0);

  zoneDenseGeneration_ = 1;
  zoneDenseStampByIndex_.assign(static_cast<size_t>(totalZones), 0u);
  zoneDenseDaysByIndex_.assign(static_cast<size_t>(totalZones), 0);
  denseZoneIndices_.clear();

  zoneOwnerGeneration_ = 1;
  zoneOwnerStampByIndex_.assign(static_cast<size_t>(totalZones), 0u);
  zoneOwnerByIndex_.assign(static_cast<size_t>(totalZones), -1);
  zoneOwnerBestDistSqByIndex_.assign(static_cast<size_t>(totalZones), 0);
  ownedZoneIndices_.clear();

  zoneConflictGeneration_ = 1;
  zoneConflictStampByIndex_.assign(static_cast<size_t>(totalZones), 0u);
  zoneConflictByIndex_.assign(static_cast<size_t>(totalZones), 0);
  conflictZoneIndices_.clear();
}

void SettlementManager::RecomputeZonePop(const World& world, const HumanManager& humans, int dayDelta) {
  if (dayDelta < 1) dayDelta = 1;
  std::vector<int> previousDense;
  previousDense.swap(denseZoneIndices_);

  zonePopGeneration_++;
  if (zonePopGeneration_ == 0) {
    std::fill(zonePopStampByIndex_.begin(), zonePopStampByIndex_.end(), 0u);
    zonePopGeneration_ = 1;
  }

  zoneDenseGeneration_++;
  if (zoneDenseGeneration_ == 0) {
    std::fill(zoneDenseStampByIndex_.begin(), zoneDenseStampByIndex_.end(), 0u);
    zoneDenseGeneration_ = 1;
  }

  std::vector<int> touchedZones;
  touchedZones.reserve(humans.Humans().size());

  for (const auto& human : humans.Humans()) {
    if (!human.alive) continue;
    int zx = human.x / zoneSize_;
    int zy = human.y / zoneSize_;
    if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) continue;
    int idx = zy * zonesX_ + zx;
    if (idx < 0 || idx >= static_cast<int>(zonePopStampByIndex_.size())) continue;
    if (zonePopStampByIndex_[static_cast<size_t>(idx)] != zonePopGeneration_) {
      zonePopStampByIndex_[static_cast<size_t>(idx)] = zonePopGeneration_;
      zonePopByIndex_[static_cast<size_t>(idx)] = 0;
      zoneFoundingPopByIndex_[static_cast<size_t>(idx)] = 0;
      touchedZones.push_back(idx);
    }
    zonePopByIndex_[static_cast<size_t>(idx)]++;
    bool contributesToFounding = true;
    if (human.role == Role::Soldier && human.armyState != ArmyState::Idle) {
      contributesToFounding = false;
    }
    if (contributesToFounding) {
      zoneFoundingPopByIndex_[static_cast<size_t>(idx)]++;
    }
  }

  denseZoneIndices_.reserve(touchedZones.size());
  for (int idx : touchedZones) {
    int pop = zoneFoundingPopByIndex_[static_cast<size_t>(idx)];
    if (pop < kZonePopThreshold) continue;
    zoneDenseStampByIndex_[static_cast<size_t>(idx)] = zoneDenseGeneration_;
    zoneDenseDaysByIndex_[static_cast<size_t>(idx)]++;
    denseZoneIndices_.push_back(idx);
  }

  for (int idx : previousDense) {
    if (idx < 0 || idx >= static_cast<int>(zoneDenseStampByIndex_.size())) continue;
    if (zoneDenseStampByIndex_[static_cast<size_t>(idx)] != zoneDenseGeneration_) {
      zoneDenseDaysByIndex_[static_cast<size_t>(idx)] = 0;
    }
  }

  (void)dayDelta;
  (void)world;
}

void SettlementManager::RecomputeZonePopMacro() {
  std::vector<int> previousDense;
  previousDense.swap(denseZoneIndices_);

  zonePopGeneration_++;
  if (zonePopGeneration_ == 0) {
    std::fill(zonePopStampByIndex_.begin(), zonePopStampByIndex_.end(), 0u);
    zonePopGeneration_ = 1;
  }

  zoneDenseGeneration_++;
  if (zoneDenseGeneration_ == 0) {
    std::fill(zoneDenseStampByIndex_.begin(), zoneDenseStampByIndex_.end(), 0u);
    zoneDenseGeneration_ = 1;
  }

  std::vector<int> touchedZones;
  touchedZones.reserve(settlements_.size());

  for (const auto& settlement : settlements_) {
    int zx = settlement.centerX / zoneSize_;
    int zy = settlement.centerY / zoneSize_;
    if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) continue;
    int idx = zy * zonesX_ + zx;
    if (idx < 0 || idx >= static_cast<int>(zonePopStampByIndex_.size())) continue;
    if (zonePopStampByIndex_[static_cast<size_t>(idx)] != zonePopGeneration_) {
      zonePopStampByIndex_[static_cast<size_t>(idx)] = zonePopGeneration_;
      zonePopByIndex_[static_cast<size_t>(idx)] = 0;
      zoneFoundingPopByIndex_[static_cast<size_t>(idx)] = 0;
      touchedZones.push_back(idx);
    }
    zonePopByIndex_[static_cast<size_t>(idx)] += settlement.population;
    zoneFoundingPopByIndex_[static_cast<size_t>(idx)] += settlement.population;
  }

  denseZoneIndices_.reserve(touchedZones.size());
  for (int idx : touchedZones) {
    int pop = zoneFoundingPopByIndex_[static_cast<size_t>(idx)];
    if (pop < kZonePopThreshold) continue;
    zoneDenseStampByIndex_[static_cast<size_t>(idx)] = zoneDenseGeneration_;
    zoneDenseDaysByIndex_[static_cast<size_t>(idx)]++;
    denseZoneIndices_.push_back(idx);
  }

  for (int idx : previousDense) {
    if (idx < 0 || idx >= static_cast<int>(zoneDenseStampByIndex_.size())) continue;
    if (zoneDenseStampByIndex_[static_cast<size_t>(idx)] != zoneDenseGeneration_) {
      zoneDenseDaysByIndex_[static_cast<size_t>(idx)] = 0;
    }
  }
}

void SettlementManager::TryFoundNewSettlements(World& world, HumanManager* humans, Random& rng, int dayCount,
                                               std::vector<VillageMarker>& markers,
                                               FactionManager& factions) {
  const int minDistSq = kMinVillageDistTiles * kMinVillageDistTiles;

  auto tooCloseToExistingSettlement = [&](int x, int y, int minDist) {
    const int minDistSqLocal = minDist * minDist;
    for (const auto& settlement : settlements_) {
      int dx = settlement.centerX - x;
      int dy = settlement.centerY - y;
      if (dx * dx + dy * dy < minDistSqLocal) return true;
    }
    return false;
  };

  auto removeFailedSettlement = [&](int settlementId, int x, int y) {
    world.ClearBuilding(x, y);
    settlements_.erase(std::remove_if(settlements_.begin(), settlements_.end(),
                                      [&](const Settlement& settlement) {
                                        return settlement.id == settlementId;
                                      }),
                       settlements_.end());
    idToIndex_.assign(nextId_, -1);
    for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
      if (settlements_[i].id >= 0 && settlements_[i].id < static_cast<int>(idToIndex_.size())) {
        idToIndex_[settlements_[i].id] = i;
      }
    }
    homeFieldDirty_ = true;
  };

  auto findExpansionSite = [&](const Settlement& parent, int& outX, int& outY) {
    outX = -1;
    outY = -1;
    int bestScore = std::numeric_limits<int>::min();

    for (int attempt = 0; attempt < 72; ++attempt) {
      int dx = rng.RangeInt(-kFoundingMaxSiteDist, kFoundingMaxSiteDist);
      int dy = rng.RangeInt(-kFoundingMaxSiteDist, kFoundingMaxSiteDist);
      int distSq = dx * dx + dy * dy;
      if (distSq < kFoundingMinSiteDist * kFoundingMinSiteDist ||
          distSq > kFoundingMaxSiteDist * kFoundingMaxSiteDist) {
        continue;
      }

      int x = parent.centerX + dx;
      int y = parent.centerY + dy;
      if (!world.TownHallFootprintAllLand(x, y)) continue;
      if (tooCloseToExistingSettlement(x, y, kFoundingMinSiteDist)) continue;

      int ownerId = ZoneOwnerForTile(x, y);
      if (ownerId != -1 && ownerId != parent.id) {
        const Settlement* ownerSettlement = Get(ownerId);
        if (ownerSettlement && ownerSettlement->factionId != parent.factionId) continue;
      }

      const Tile& tile = world.At(x, y);
      int dist = static_cast<int>(std::round(std::sqrt(static_cast<float>(distSq))));
      int score = static_cast<int>(world.WaterScentAt(x, y)) * 2 +
                  static_cast<int>(world.FoodScentAt(x, y)) + tile.trees * 35 -
                  static_cast<int>(world.FireRiskAt(x, y)) * 3;
      score -= std::abs(dist - 68) * 120;
      if (ownerId == parent.id) score += 4000;
      if (score > bestScore) {
        bestScore = score;
        outX = x;
        outY = y;
      }
    }

    return outX != -1 && outY != -1;
  };

  auto foundSettlement = [&](Settlement& parent, int x, int y, int desiredFounders,
                             int founderMaxDistSq) {
    const int parentId = parent.id;
    const int parentFactionId = parent.factionId;
    const int parentTechTier = parent.techTier;
    int movedFounders = 0;
    int starterFood = static_cast<int>(world.At(x, y).food);
    int settlementId = nextId_++;
    world.PlaceBuilding(x, y, BuildingType::TownHall, settlementId, 0);
    if (world.At(x, y).building != BuildingType::TownHall ||
        world.At(x, y).buildingOwnerId != settlementId) {
      return false;
    }

    Settlement settlement;
    settlement.id = settlementId;
    settlement.centerX = x;
    settlement.centerY = y;
    settlement.factionId = parentFactionId;
    settlement.stockFood = 50 + starterFood;
    settlement.stockWood = 0;
    settlement.stockStone = 0;
    settlement.stockMetal = 0;
    settlement.stockGold = 0;
    settlement.population = 0;
    settlement.lastFoundedSettlementDay = dayCount;
    settlement.ageDays = 0;
    settlement.tier = SettlementTier::Village;
    settlement.techTier = parentTechTier;
    settlement.techProgress = 0.0f;
    settlement.stability = 75;
    settlement.unrest = 0;
    settlement.borderPressure = 0;
    settlement.warPressure = 0;
    settlement.influenceRadius = kClaimRadiusTownHall;
    settlement.isCapital = false;
    InitializeSettlementDeposits(settlement);
    settlements_.push_back(settlement);

    Settlement* founded = GetMutable(settlementId);
    Settlement* source = GetMutable(parentId);
    if (!founded || !source) return false;
    if (humans) {
      movedFounders =
          AssignFoundingHumans(*humans, source->id, *founded, desiredFounders, founderMaxDistSq);
      source->population = std::max(0, source->population - movedFounders);
      founded->population = movedFounders;
    } else {
      movedFounders = TransferFoundingMacroPopulation(*source, *founded, desiredFounders);
    }
    if (movedFounders < kFoundingMinGroup) {
      if (humans) {
        int restored = 0;
        for (Human& human : humans->HumansMutable()) {
          if (human.settlementId != settlementId) continue;
          human.settlementId = source->id;
          human.homeX = source->centerX;
          human.homeY = source->centerY;
          human.targetX = source->centerX;
          human.targetY = source->centerY;
          human.forceReplan = true;
          restored++;
        }
        source->population += restored;
      } else {
        for (int bin = 0; bin < 6; ++bin) {
          source->macroPopF[bin] += founded->macroPopF[bin];
          source->macroPopM[bin] += founded->macroPopM[bin];
        }
        source->population = source->MacroTotal();
      }
      removeFailedSettlement(settlementId, x, y);
      return false;
    }

    TransferFoundingResources(*source, *founded, movedFounders);
    source->lastFoundedSettlementDay = dayCount;
    homeFieldDirty_ = true;
    markers.push_back(VillageMarker{x, y, 25});
    return true;
  };

  if (!settlements_.empty()) {
    for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
      Settlement& parent = settlements_[i];
      if (parent.population < kFoundingParentMinPop) continue;
      if (parent.factionId <= 0) continue;
      if (dayCount - parent.lastFoundedSettlementDay < kFoundingCooldownDays) continue;
      if (factions.ActiveWarIdForFaction(parent.factionId) > 0) continue;

      const int desiredFounders = DesiredFoundingGroupSize(parent.population);
      if (desiredFounders < kFoundingMinGroup) continue;
      int requiredFood = desiredFounders * kFoundingFoodPerPerson + 80;
      int requiredWood = desiredFounders * kFoundingWoodPerPerson + Settlement::kTownHallWoodCost;
      if (parent.stockFood < requiredFood || parent.stockWood < requiredWood) continue;

      bool expansionPressure =
          (parent.housingCap > 0 &&
           parent.population >= parent.housingCap - kFoundingCrowdingEarlyMargin) ||
          parent.population >= 160 ||
          (parent.stockFood > parent.population * 5 && parent.stockWood > parent.population);
      if (!expansionPressure) continue;
      if (HasMigrationReliefOption(i)) continue;

      if (humans) {
        int eligibleFounders = CountEligibleFoundingHumans(*humans, parent.id, parent.centerX,
                                                           parent.centerY, -1);
        if (eligibleFounders < kFoundingMinGroup) continue;
      } else if (parent.MacroTotal() < kFoundingParentReservePop + kFoundingMinGroup) {
        continue;
      }

      int bestX = -1;
      int bestY = -1;
      if (!findExpansionSite(parent, bestX, bestY)) continue;

      if (foundSettlement(parent, bestX, bestY, desiredFounders, -1)) {
        return;
      }
    }
  }

  for (size_t denseIndex = 0; denseIndex < denseZoneIndices_.size();) {
    int zoneIndex = denseZoneIndices_[denseIndex];
    if (zoneIndex < 0 || zoneIndex >= zonesX_ * zonesY_) {
      denseIndex++;
      continue;
    }
    int denseDays = zoneDenseDaysByIndex_[static_cast<size_t>(zoneIndex)];
    int zx = zoneIndex % zonesX_;
    int zy = zoneIndex / zonesX_;
    int startX = zx * zoneSize_;
    int startY = zy * zoneSize_;
    int endX = std::min(world.width(), startX + zoneSize_);
    int endY = std::min(world.height(), startY + zoneSize_);

    bool tooClose = false;
    int nearestId = -1;
    int nearestDistSq = std::numeric_limits<int>::max();
    for (const auto& settlement : settlements_) {
      int dx = settlement.centerX - (startX + zoneSize_ / 2);
      int dy = settlement.centerY - (startY + zoneSize_ / 2);
      int distSq = dx * dx + dy * dy;
      if (distSq < nearestDistSq) {
        nearestDistSq = distSq;
        nearestId = settlement.id;
      }
      if (distSq <= minDistSq) {
        tooClose = true;
        break;
      }
    }
    if (tooClose) {
      denseIndex++;
      continue;
    }

    const Settlement* nearestSettlement = Get(nearestId);
    int ownerId = ZoneOwnerAt(zx, zy);
    const bool seedSettlement = settlements_.empty();
    const bool independentSettlement =
        !seedSettlement && humans && ownerId == -1 &&
        nearestDistSq >= kFactionLinkRadiusTiles * kFactionLinkRadiusTiles;
    if (!seedSettlement && !independentSettlement) {
      denseIndex++;
      continue;
    }

    int zonePop = ZoneFoundingPopAt(zx, zy);
    int sourceFactionId = 0;
    int sourceSettlementId = nearestSettlement ? nearestSettlement->id : -1;
    if (!independentSettlement && nearestSettlement) {
      sourceFactionId = nearestSettlement->factionId;
    }
    const Faction* sourceFaction = factions.Get(sourceFactionId);
    float expansionBias = sourceFaction ? sourceFaction->traits.expansionBias + sourceFaction->leaderInfluence.expansion
                                        : 1.0f;
    expansionBias = std::max(0.6f, std::min(1.6f, expansionBias));
    int popThreshold = std::max(6, static_cast<int>(std::round(kZonePopThreshold / expansionBias)));
    int requiredDays = std::max(2, static_cast<int>(std::round(kZoneRequiredDays / expansionBias)));
    if (zonePop < popThreshold) {
      denseIndex++;
      continue;
    }
    if (denseDays < requiredDays) {
      denseIndex++;
      continue;
    }

    int desiredFounders = 0;
    int foundingSourceId = -1;
    if (seedSettlement || independentSettlement) {
      if (!humans) {
        denseIndex++;
        continue;
      }
      desiredFounders = std::min(kFoundingMaxGroup, std::max(kFoundingMinGroup, zonePop));
    }

    if (ownerId != -1 && sourceFactionId > 0) {
      const Settlement* ownerSettlement = Get(ownerId);
      if (ownerSettlement && ownerSettlement->factionId > 0 &&
          ownerSettlement->factionId != sourceFactionId) {
        bool resourceStress = false;
        if (nearestSettlement) {
          int pop = std::max(1, nearestSettlement->population);
          resourceStress = (nearestSettlement->stockFood < pop * 2) ||
                           (nearestSettlement->stockWood < pop);
        }
        if (!factions.CanExpandInto(sourceFactionId, ownerSettlement->factionId, resourceStress)) {
          denseIndex++;
          continue;
        }
      }
    }

    int bestScore = std::numeric_limits<int>::min();
    int bestX = -1;
    int bestY = -1;

    for (int y = startY; y < endY; ++y) {
      for (int x = startX; x < endX; ++x) {
        if (!world.TownHallFootprintAllLand(x, y)) continue;
        if (independentSettlement && ZoneOwnerForTile(x, y) != -1) continue;
        if (independentSettlement && tooCloseToExistingSettlement(x, y, kFactionLinkRadiusTiles)) {
          continue;
        }
        const Tile& tile = world.At(x, y);

        int score = static_cast<int>(world.WaterScentAt(x, y)) * 2 +
                    static_cast<int>(world.FoodScentAt(x, y)) + tile.trees * 50 -
                    static_cast<int>(world.FireRiskAt(x, y)) * 3;
        if (score > bestScore) {
          bestScore = score;
          bestX = x;
          bestY = y;
        }
      }
    }

    if (bestX == -1 || bestY == -1) {
      denseIndex++;
      continue;
    }
    if (!world.TownHallFootprintAllLand(bestX, bestY)) {
      denseIndex++;
      continue;
    }

    const int maxFounderDist = std::max(zoneSize_ * 2, kMinVillageDistTiles);
    const int maxFounderDistSq = maxFounderDist * maxFounderDist;
    if (humans) {
      int eligibleFounders =
          CountEligibleFoundingHumans(*humans, foundingSourceId, bestX, bestY, maxFounderDistSq);
      if (eligibleFounders < kFoundingMinGroup) {
        denseIndex++;
        continue;
      }
      desiredFounders = std::min(desiredFounders, eligibleFounders);
      if (desiredFounders < kFoundingMinGroup) {
        denseIndex++;
        continue;
      }
    }

    int starterFood = static_cast<int>(world.At(bestX, bestY).food);
    int settlementId = nextId_++;
    world.PlaceBuilding(bestX, bestY, BuildingType::TownHall, settlementId, 0);
    if (world.At(bestX, bestY).building != BuildingType::TownHall ||
        world.At(bestX, bestY).buildingOwnerId != settlementId) {
      denseIndex++;
      continue;
    }

    Settlement settlement;
    settlement.id = settlementId;
    settlement.centerX = bestX;
    settlement.centerY = bestY;
    settlement.factionId = 0;
    settlement.stockFood = 50 + starterFood;
    settlement.stockWood = 0;
    settlement.stockStone = 0;
    settlement.stockMetal = 0;
    settlement.stockGold = 0;
    settlement.population = 0;
    settlement.lastFoundedSettlementDay = dayCount;
    settlement.ageDays = 0;
    settlement.tier = SettlementTier::Village;
    settlement.techTier = 0;
    settlement.techProgress = 0.0f;
    settlement.stability = 70;
    settlement.unrest = 0;
    settlement.borderPressure = 0;
    settlement.warPressure = 0;
    settlement.influenceRadius = kClaimRadiusTownHall;
    settlement.isCapital = false;
    InitializeSettlementDeposits(settlement);

    int factionId = 0;
    if (!independentSettlement && nearestSettlement && sourceFactionId > 0) {
      int linkRadius = kFactionLinkRadiusTiles;
      if (sourceFaction && sourceFaction->traits.outlook == FactionOutlook::Isolationist) {
        linkRadius = kFactionLinkRadiusTiles / 2;
      }
      if (nearestDistSq <= linkRadius * linkRadius) {
        factionId = sourceFactionId;
      }
    }
    if (factionId == 0) {
      factionId = factions.CreateFaction(rng);
    }
    settlement.factionId = factionId;
    settlements_.push_back(settlement);
    Settlement* founded = GetMutable(settlementId);
    Settlement* source = GetMutable(sourceSettlementId);
    if (founded && (seedSettlement || independentSettlement)) {
      int movedFounders = AssignFoundingHumans(*humans, -1, *founded, desiredFounders, maxFounderDistSq);
      founded->population = movedFounders;
      if (movedFounders < kFoundingMinGroup) {
        for (Human& human : humans->HumansMutable()) {
          if (human.settlementId != settlementId) continue;
          human.settlementId = -1;
          human.homeX = human.x;
          human.homeY = human.y;
          human.targetX = human.x;
          human.targetY = human.y;
          human.goal = Goal::Wander;
          human.forceReplan = true;
        }
        removeFailedSettlement(settlementId, bestX, bestY);
        denseIndex++;
        continue;
      }
      if (movedFounders > 0) {
        founded->stability = std::max(founded->stability, 75);
      }
    } else if (founded && source && source->id != founded->id) {
      int movedFounders = 0;
      if (humans) {
        movedFounders = AssignFoundingHumans(*humans, source->id, *founded, desiredFounders,
                                             maxFounderDistSq);
        source->population = std::max(0, source->population - movedFounders);
        founded->population = movedFounders;
      } else {
        movedFounders = TransferFoundingMacroPopulation(*source, *founded, desiredFounders);
      }
      TransferFoundingResources(*source, *founded, movedFounders);
      if (movedFounders > 0) {
        founded->stability = std::max(founded->stability, 75);
      }
      source->lastFoundedSettlementDay = dayCount;
    }
    homeFieldDirty_ = true;

    markers.push_back(VillageMarker{bestX, bestY, 25});
    zoneDenseDaysByIndex_[static_cast<size_t>(zoneIndex)] = 0;
    denseZoneIndices_[denseIndex] = denseZoneIndices_.back();
    denseZoneIndices_.pop_back();
    continue;
  }
  (void)dayCount;
}

void SettlementManager::RecomputeZoneOwners(const World& world) {
  ownedZoneIndices_.clear();
  zoneOwnerGeneration_++;
  if (zoneOwnerGeneration_ == 0) {
    std::fill(zoneOwnerStampByIndex_.begin(), zoneOwnerStampByIndex_.end(), 0u);
    zoneOwnerGeneration_ = 1;
  }
  if (settlements_.empty()) {
    return;
  }
  if (zonesX_ <= 0 || zonesY_ <= 0) return;

  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    const Settlement& settlement = settlements_[i];
    if (i >= static_cast<int>(claimSources_.size())) continue;
    const auto& sources = claimSources_[i];
    if (sources.empty()) continue;

    for (const auto& source : sources) {
      int radius = source.radius;
      if (radius <= 0) continue;
      int radiusSq = radius * radius;

      int minZoneX = std::max(0, (source.x - radius) / zoneSize_);
      int maxZoneX = std::min(zonesX_ - 1, (source.x + radius) / zoneSize_);
      int minZoneY = std::max(0, (source.y - radius) / zoneSize_);
      int maxZoneY = std::min(zonesY_ - 1, (source.y + radius) / zoneSize_);

      for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
        int centerY = std::min(world.height() - 1, zy * zoneSize_ + zoneSize_ / 2);
        for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
          int centerX = std::min(world.width() - 1, zx * zoneSize_ + zoneSize_ / 2);
          int dx = source.x - centerX;
          int dy = source.y - centerY;
          int dist = dx * dx + dy * dy;
          if (dist > radiusSq) continue;
          int idx = zy * zonesX_ + zx;
          if (idx < 0 || idx >= static_cast<int>(zoneOwnerStampByIndex_.size())) continue;
          if (zoneOwnerStampByIndex_[static_cast<size_t>(idx)] != zoneOwnerGeneration_) {
            zoneOwnerStampByIndex_[static_cast<size_t>(idx)] = zoneOwnerGeneration_;
            zoneOwnerBestDistSqByIndex_[static_cast<size_t>(idx)] = dist;
            zoneOwnerByIndex_[static_cast<size_t>(idx)] = settlement.id;
            ownedZoneIndices_.push_back(idx);
          } else if (dist < zoneOwnerBestDistSqByIndex_[static_cast<size_t>(idx)]) {
            zoneOwnerBestDistSqByIndex_[static_cast<size_t>(idx)] = dist;
            zoneOwnerByIndex_[static_cast<size_t>(idx)] = settlement.id;
          }
        }
      }
    }
  }
}

void SettlementManager::UpdateZoneConflict(const FactionManager& factions) {
  UpdateBorderPressure(factions);
}

void SettlementManager::UpdateBorderPressure(const FactionManager& factions) {
  for (auto& settlement : settlements_) {
    settlement.borderPressure = 0;
    settlement.warPressure = 0;
  }
  if (zonesX_ <= 0 || zonesY_ <= 0 || ownedZoneIndices_.empty()) return;
  const bool warEnabled = factions.WarEnabled();
  conflictZoneIndices_.clear();
  zoneConflictGeneration_++;
  if (zoneConflictGeneration_ == 0) {
    std::fill(zoneConflictStampByIndex_.begin(), zoneConflictStampByIndex_.end(), 0u);
    zoneConflictGeneration_ = 1;
  }

  auto bumpConflict = [&](int zoneIndex, int amount) {
    if (amount <= 0) return;
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zoneConflictStampByIndex_.size())) return;
    if (zoneConflictStampByIndex_[static_cast<size_t>(zoneIndex)] != zoneConflictGeneration_) {
      zoneConflictStampByIndex_[static_cast<size_t>(zoneIndex)] = zoneConflictGeneration_;
      zoneConflictByIndex_[static_cast<size_t>(zoneIndex)] = 0;
      conflictZoneIndices_.push_back(zoneIndex);
    }
    int& slot = zoneConflictByIndex_[static_cast<size_t>(zoneIndex)];
    if (amount > slot) slot = amount;
  };

  for (int zoneIndex : ownedZoneIndices_) {
    if (zoneIndex < 0 || zoneIndex >= zonesX_ * zonesY_) continue;
    int zx = zoneIndex % zonesX_;
    int zy = zoneIndex / zonesX_;
    int ownerId = ZoneOwnerAt(zx, zy);
    if (ownerId <= 0) continue;
    Settlement* ownerSettlement = GetMutable(ownerId);
    if (!ownerSettlement) continue;
    int factionA = ownerSettlement->factionId;
    if (factionA <= 0) continue;

    auto handleNeighbor = [&](int nx, int ny) {
      if (nx < 0 || ny < 0 || nx >= zonesX_ || ny >= zonesY_) return;
      int neighborOwner = ZoneOwnerAt(nx, ny);
      if (neighborOwner <= 0 || neighborOwner == ownerId) return;
      Settlement* neighborSettlement = GetMutable(neighborOwner);
      if (!neighborSettlement) return;
      int factionB = neighborSettlement->factionId;
      if (factionB <= 0 || factionA == factionB) return;

      ownerSettlement->borderPressure++;
      neighborSettlement->borderPressure++;

      if (!warEnabled) return;
      bool atWar = factions.IsAtWar(factionA, factionB);
      bool hostile = (factions.RelationType(factionA, factionB) == FactionRelation::Hostile);
      if (atWar) {
        ownerSettlement->warPressure += 2;
        neighborSettlement->warPressure += 2;
        bumpConflict(zoneIndex, 200);
        bumpConflict(ny * zonesX_ + nx, 200);
      } else if (hostile) {
        ownerSettlement->warPressure += 1;
        neighborSettlement->warPressure += 1;
        bumpConflict(zoneIndex, 120);
        bumpConflict(ny * zonesX_ + nx, 120);
      }
    };

    handleNeighbor(zx + 1, zy);
    handleNeighbor(zx, zy + 1);
  }
}

void SettlementManager::EnsureSettlementFactions(FactionManager& factions, Random& rng) {
  for (size_t i = 0; i < settlements_.size(); ++i) {
    Settlement& settlement = settlements_[i];
    if (settlement.factionId > 0 && factions.Get(settlement.factionId)) {
      continue;
    }

    int nearestFactionId = 0;
    int nearestDistSq = std::numeric_limits<int>::max();
    for (size_t j = 0; j < settlements_.size(); ++j) {
      if (i == j) continue;
      const Settlement& other = settlements_[j];
      if (other.factionId <= 0 || !factions.Get(other.factionId)) continue;
      int dx = other.centerX - settlement.centerX;
      int dy = other.centerY - settlement.centerY;
      int distSq = dx * dx + dy * dy;
      if (distSq < nearestDistSq) {
        nearestDistSq = distSq;
        nearestFactionId = other.factionId;
      }
    }

    int assigned = 0;
    if (nearestFactionId > 0) {
      const Faction* faction = factions.Get(nearestFactionId);
      int linkRadius = kFactionLinkRadiusTiles;
      if (faction && faction->traits.outlook == FactionOutlook::Isolationist) {
        linkRadius = kFactionLinkRadiusTiles / 2;
      }
      if (nearestDistSq <= linkRadius * linkRadius) {
        assigned = nearestFactionId;
      }
    }

    if (assigned == 0) {
      assigned = factions.CreateFaction(rng);
    }
    settlement.factionId = assigned;
  }
}

void SettlementManager::AssignHumansToSettlements(HumanManager& humans) {
  idToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    idToIndex_[settlements_[i].id] = i;
  }

  for (auto& human : humans.HumansMutable()) {
    if (!human.alive) continue;
    int idx = (human.settlementId >= 0 && human.settlementId < static_cast<int>(idToIndex_.size()))
                  ? idToIndex_[human.settlementId]
                  : -1;
    if (idx < 0) {
      int ownerId = ZoneOwnerForTile(human.x, human.y);
      int ownerIdx = (ownerId >= 0 && ownerId < static_cast<int>(idToIndex_.size()))
                         ? idToIndex_[ownerId]
                         : -1;
      if (ownerIdx >= 0) {
        human.settlementId = ownerId;
        idx = ownerIdx;
      }
    }

    if (idx < 0) {
      human.role = Role::Idle;
      continue;
    }
    human.homeX = settlements_[idx].centerX;
    human.homeY = settlements_[idx].centerY;
  }
}

void SettlementManager::RecomputeSettlementBuildings(const World& world) {
  if (settlements_.empty()) {
    claimSources_.clear();
    return;
  }
  if (idToIndex_.size() != static_cast<size_t>(nextId_)) {
    idToIndex_.assign(nextId_, -1);
    for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
      idToIndex_[settlements_[i].id] = i;
    }
  }
  claimSources_.assign(settlements_.size(), {});
  for (auto& settlement : settlements_) {
    settlement.houses = 0;
    settlement.farms = 0;
    settlement.granaries = 0;
    settlement.wells = 0;
    settlement.markets = 0;
    settlement.forges = 0;
    settlement.monuments = 0;
    settlement.archives = 0;
    settlement.walls = 0;
    settlement.barracks = 0;
    settlement.mints = 0;
    settlement.schools = 0;
    settlement.workshops = 0;
    settlement.banks = 0;
    settlement.universities = 0;
    settlement.factories = 0;
    settlement.powerPlants = 0;
    settlement.airfields = 0;
    settlement.reactors = 0;
    settlement.satelliteArrays = 0;
    settlement.roboticsLabs = 0;
    settlement.harbors = 0;
    settlement.railDepots = 0;
    settlement.hospitals = 0;
    settlement.dataCenters = 0;
    settlement.militaryHQs = 0;
    settlement.farmsPlanted = 0;
    settlement.farmsReady = 0;
    settlement.townHalls = 0;
    settlement.housingCap = 0;
    settlement.influenceRadius = 0;
  }

  for (uint64_t coord : world.BuildingTiles()) {
    int x = static_cast<int>(static_cast<uint32_t>(coord >> 32));
    int y = static_cast<int>(static_cast<uint32_t>(coord & 0xffffffffu));
    const Tile& tile = world.At(x, y);
    if (tile.building == BuildingType::None) continue;
    int ownerId = tile.buildingOwnerId;
    if (ownerId < 0 || ownerId >= static_cast<int>(idToIndex_.size())) continue;
    int idx = idToIndex_[ownerId];
    if (idx < 0 || idx >= static_cast<int>(settlements_.size())) continue;
    Settlement& settlement = settlements_[idx];
    switch (tile.building) {
      case BuildingType::House:
        settlement.houses++;
        break;
      case BuildingType::Farm:
        settlement.farms++;
        if (tile.farmStage > 0) {
          settlement.farmsPlanted++;
        }
        if (tile.farmStage >= Settlement::kFarmReadyStage) {
          settlement.farmsReady++;
        }
        break;
      case BuildingType::Granary:
        settlement.granaries++;
        break;
      case BuildingType::Well:
        settlement.wells++;
        break;
      case BuildingType::Market:
        settlement.markets++;
        break;
      case BuildingType::Forge:
        settlement.forges++;
        break;
      case BuildingType::Monument:
        settlement.monuments++;
        break;
      case BuildingType::Archive:
        settlement.archives++;
        break;
      case BuildingType::Walls:
        settlement.walls++;
        break;
      case BuildingType::Barracks:
        settlement.barracks++;
        break;
      case BuildingType::Mint:
        settlement.mints++;
        break;
      case BuildingType::School:
        settlement.schools++;
        break;
      case BuildingType::Workshop:
        settlement.workshops++;
        break;
      case BuildingType::Bank:
        settlement.banks++;
        break;
      case BuildingType::University:
        settlement.universities++;
        break;
      case BuildingType::Factory:
        settlement.factories++;
        break;
      case BuildingType::PowerPlant:
        settlement.powerPlants++;
        break;
      case BuildingType::Airfield:
        settlement.airfields++;
        break;
      case BuildingType::Reactor:
        settlement.reactors++;
        break;
      case BuildingType::SatelliteArray:
        settlement.satelliteArrays++;
        break;
      case BuildingType::RoboticsLab:
        settlement.roboticsLabs++;
        break;
      case BuildingType::Harbor:
        settlement.harbors++;
        break;
      case BuildingType::RailDepot:
        settlement.railDepots++;
        break;
      case BuildingType::Hospital:
        settlement.hospitals++;
        break;
      case BuildingType::DataCenter:
        settlement.dataCenters++;
        break;
      case BuildingType::MilitaryHQ:
        settlement.militaryHQs++;
        break;
      case BuildingType::TownHall:
        settlement.townHalls++;
        break;
      default:
        break;
    }
    int radius = ClaimRadiusForBuilding(tile.building);
    if (radius > 0) {
      claimSources_[idx].push_back(ClaimSource{x, y, radius});
      settlement.influenceRadius = std::max(settlement.influenceRadius, radius);
    }
  }

  UpdateSettlementCaps();
}

void SettlementManager::UpdateSettlementCaps() {
  for (auto& settlement : settlements_) {
    int houseCap = settlement.houses * HouseCapacityForTier(settlement.techTier);
    int hallCap = settlement.townHalls * TownHallCapacityForTier(settlement.techTier);
    int hospitalCap = settlement.hospitals * std::max(12, 10 + settlement.techTier * 3);
    settlement.housingCap = houseCap + hallCap + hospitalCap;
  }
}

void SettlementManager::ComputeSettlementWaterTargets(const World& world) {
  if (settlements_.empty()) return;
  const int maxDistSq = kWaterSearchRadius * kWaterSearchRadius;

  for (auto& settlement : settlements_) {
    int bestX = -1;
    int bestY = -1;
    int bestDistSq = maxDistSq + 1;
    for (int dy = -kWaterSearchRadius; dy <= kWaterSearchRadius; ++dy) {
      int y = settlement.centerY + dy;
      if (y < 0 || y >= world.height()) continue;
      for (int dx = -kWaterSearchRadius; dx <= kWaterSearchRadius; ++dx) {
        int x = settlement.centerX + dx;
        if (x < 0 || x >= world.width()) continue;
        const Tile& tile = world.At(x, y);
        if (tile.type != TileType::FreshWater && world.WellRadiusAt(x, y) == 0) continue;
        int distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
          bestDistSq = distSq;
          bestX = x;
          bestY = y;
        }
      }
    }
    if (bestX != -1) {
      settlement.hasWaterTarget = true;
      settlement.waterTargetX = bestX;
      settlement.waterTargetY = bestY;
    } else {
      settlement.hasWaterTarget = false;
      settlement.waterTargetX = settlement.centerX;
      settlement.waterTargetY = settlement.centerY;
    }
  }
}

void SettlementManager::RecomputeSettlementPopAndRoles(World& world, Random& rng, int dayCount,
                                                       int dayDelta, HumanManager& humans,
                                                       const FactionManager& factions) {
  if (settlements_.empty()) return;

  if (memberCounts_.size() != settlements_.size()) {
    memberCounts_.assign(settlements_.size(), 0);
  } else {
    std::fill(memberCounts_.begin(), memberCounts_.end(), 0);
  }

  for (const auto& human : humans.Humans()) {
    if (!human.alive) continue;
    if (human.settlementId == -1) continue;
    int idx = (human.settlementId < static_cast<int>(idToIndex_.size()))
                  ? idToIndex_[human.settlementId]
                  : -1;
    if (idx < 0) continue;
    memberCounts_[idx]++;
  }

  int totalMembers = 0;
  if (memberOffsets_.size() != settlements_.size() + 1) {
    memberOffsets_.assign(settlements_.size() + 1, 0);
  }
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    memberOffsets_[i] = totalMembers;
    totalMembers += memberCounts_[i];
  }
  memberOffsets_[settlements_.size()] = totalMembers;

  if (memberIndices_.size() < static_cast<size_t>(totalMembers)) {
    memberIndices_.resize(totalMembers);
  }

  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    settlements_[i].population = memberCounts_[i];
    settlements_[i].gatherers = 0;
    settlements_[i].farmers = 0;
    settlements_[i].builders = 0;
    settlements_[i].guards = 0;
    settlements_[i].soldiers = 0;
    settlements_[i].scouts = 0;
    settlements_[i].idle = 0;
    settlements_[i].ageDays += std::max(1, dayDelta);
    memberCounts_[i] = 0;
  }

  for (int i = 0; i < static_cast<int>(humans.Humans().size()); ++i) {
    const auto& human = humans.Humans()[i];
    if (!human.alive) continue;
    if (human.settlementId == -1) continue;
    int idx = (human.settlementId < static_cast<int>(idToIndex_.size()))
                  ? idToIndex_[human.settlementId]
                  : -1;
    if (idx < 0) continue;
    int write = memberOffsets_[idx] + memberCounts_[idx]++;
    if (write >= 0 && write < static_cast<int>(memberIndices_.size())) {
      memberIndices_[write] = i;
    }
  }

  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    Settlement& settlement = settlements_[i];
    int pop = settlement.population;
    if (pop <= 0) continue;

    int warId = factions.ActiveWarIdForFaction(settlement.factionId);
    const bool isAtWar = (warId > 0);
    const bool isAttacker = (isAtWar && factions.WarIsAttacker(warId, settlement.factionId));
    settlement.debugWarTime = isAtWar;
    settlement.debugWarAttacker = isAttacker;
    settlement.debugWarId = warId;

    int farmers = std::min(pop, settlement.farms * 2);
    int gatherers = (pop * 25) / 100;
    if (pop >= 6 && gatherers < 1) gatherers = 1;
    int builders = (settlement.stockFood > pop * 3) ? (pop * 20) / 100 : (pop * 10) / 100;
    if (settlement.housingCap < pop + kHousingBuffer) {
      builders = std::max(builders, std::max(1, (pop * 25) / 100));
    }

    bool nearFire = false;
    for (int dy = -10; dy <= 10 && !nearFire; ++dy) {
      for (int dx = -10; dx <= 10; ++dx) {
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        if (world.At(x, y).burning) {
          nearFire = true;
          break;
        }
      }
    }

    int guards = pop * (nearFire ? 8 : 2) / 100;
    int soldiers = 0;
    if (settlement.warPressure > 0 || settlement.borderPressure > 2) {
      int borderBonus = std::min(6, settlement.borderPressure);
      soldiers = std::max(1, (pop * (4 + borderBonus)) / 100);
    }
    if (settlement.tier != SettlementTier::Village) {
      soldiers = std::max(soldiers, pop / 15);
    }
    if (settlement.barracks > 0) {
      soldiers = std::max(soldiers, std::max(1, pop / 10));
    }
    if (settlement.airfields > 0) {
      soldiers = std::max(soldiers, std::max(2, pop / 8));
    }
    if (settlement.militaryHQs > 0) {
      soldiers = std::max(soldiers, std::max(3, pop / 6));
    }
    if (settlement.walls > 0 && (settlement.warPressure > 0 || settlement.borderPressure > 2)) {
      soldiers = std::max(soldiers, std::max(1, pop / 12));
    }
    if (isAtWar) {
      int floorSoldiers = std::max(1, pop / (isAttacker ? 4 : 8));
      if (settlement.tier == SettlementTier::Village) {
        floorSoldiers = std::max(1, pop / (isAttacker ? 6 : 10));
      }
      settlement.debugWarSoldierFloor = floorSoldiers;
      soldiers = std::max(soldiers, floorSoldiers);
    } else {
      settlement.debugWarSoldierFloor = 0;
    }
    if (settlement.factionId > 0) {
      AllianceBonus bonus = factions.BonusForFaction(settlement.factionId, dayCount);
      soldiers = static_cast<int>(std::round(static_cast<float>(soldiers) * bonus.soldierCapMult));
    }
    int scouts = 0;
    if (settlement.tier != SettlementTier::Village) {
      scouts = std::max(1, pop / 14);
    } else if (pop >= 12) {
      scouts = 1;
    }

    bool foodEmergency = settlement.stockFood < pop * kEmergencyFoodPerPop;
    settlement.debugFoodEmergency = foodEmergency;
    settlement.debugSoldiersPreEmergency = soldiers;
    if (foodEmergency) {
      int protectedSoldiers = 0;
      if (isAtWar) {
        protectedSoldiers = std::max(0, settlement.debugWarSoldierFloor);
        protectedSoldiers = std::min(protectedSoldiers, soldiers);
      }

      int desiredFarmers = std::max(farmers, (pop * kEmergencyFarmerPct) / 100);
      desiredFarmers = std::min(desiredFarmers, settlement.farms * 2);
      int desiredGatherers = std::max(gatherers, (pop * kEmergencyGathererPct) / 100);

      int neededFarmers = std::max(0, desiredFarmers - farmers);
      int neededGatherers = std::max(0, desiredGatherers - gatherers);
      int needed = neededFarmers + neededGatherers;
      if (needed > 0) {
        int available = std::min(needed, builders + guards + soldiers + scouts);
        int shiftSoldiers = std::min(std::max(0, soldiers - protectedSoldiers), available);
        soldiers -= shiftSoldiers;
        available -= shiftSoldiers;
        int shiftGuards = std::min(guards, available);
        guards -= shiftGuards;
        available -= shiftGuards;
        int shiftScouts = std::min(scouts, available);
        scouts -= shiftScouts;
        available -= shiftScouts;
        int shiftBuilders = std::min(builders, available);
        builders -= shiftBuilders;
        int shifted = shiftSoldiers + shiftGuards + shiftScouts + shiftBuilders;
        int addFarmers = std::min(neededFarmers, shifted);
        farmers += addFarmers;
        shifted -= addFarmers;
        int addGatherers = std::min(neededGatherers, shifted);
        gatherers += addGatherers;
      }
    }

    // During war, once someone is a soldier, keep them a soldier until peace.
    int lockedSoldiers = 0;
    if (isAtWar) {
      int start = memberOffsets_[i];
      int end = memberOffsets_[i + 1];
      for (int mi = start; mi < end; ++mi) {
        int humanIndex = memberIndices_[mi];
        if (humanIndex < 0 || humanIndex >= static_cast<int>(humans.HumansMutable().size())) continue;
        const auto& human = humans.HumansMutable()[humanIndex];
        if (!human.alive) continue;
        if (human.role == Role::Soldier) {
          lockedSoldiers++;
        }
      }
    }
    if (isAtWar && lockedSoldiers > soldiers) {
      soldiers = std::min(pop, lockedSoldiers);
    }

	    int assigned = farmers + gatherers + builders + guards + soldiers + scouts;
	    if (assigned > pop) {
	      int overflow = assigned - pop;
	      int reduce = 0;
	      if (!isAtWar) {
	        reduce = std::min(soldiers, overflow);
	        soldiers -= reduce;
	        overflow -= reduce;
	      } else {
	        // Do not trim below locked soldiers during war.
	        int reducible = std::max(0, soldiers - lockedSoldiers);
	        reduce = std::min(reducible, overflow);
	        soldiers -= reduce;
	        overflow -= reduce;
	      }
	      reduce = std::min(guards, overflow);
	      guards -= reduce;
	      overflow -= reduce;
      reduce = std::min(scouts, overflow);
      scouts -= reduce;
      overflow -= reduce;
      reduce = std::min(builders, overflow);
      builders -= reduce;
      overflow -= reduce;
      reduce = std::min(farmers, overflow);
      farmers -= reduce;
      overflow -= reduce;
      if (overflow > 0) {
        gatherers = std::max(0, gatherers - overflow);
      }
    }
    int idle = pop - (farmers + gatherers + builders + guards + soldiers + scouts);

    // Keep some soldiers allocated during war. The earlier "floorSoldiers" can be undone by overflow
    // trimming when farmers/builders/guards oversubscribe the population.
    if (isAtWar) {
      const int floorSoldiers = std::max(0, settlement.debugWarSoldierFloor);
      if (soldiers < floorSoldiers) {
        int need = floorSoldiers - soldiers;

        auto shift = [&](int& from) {
          if (need <= 0) return;
          int take = std::min(from, need);
          from -= take;
          soldiers += take;
          need -= take;
        };

        shift(idle);
        shift(scouts);
        shift(guards);
        shift(builders);
        shift(gatherers);
        shift(farmers);
      }
    }

    settlement.gatherers = gatherers;
    settlement.farmers = farmers;
    settlement.builders = builders;
    settlement.guards = guards;
    settlement.soldiers = soldiers;
    settlement.scouts = scouts;
    settlement.idle = idle;
    settlement.debugTargetFarmers = farmers;
    settlement.debugTargetGatherers = gatherers;
    settlement.debugTargetBuilders = builders;
    settlement.debugTargetGuards = guards;
    settlement.debugTargetSoldiers = soldiers;
    settlement.debugTargetScouts = scouts;
    settlement.debugTargetIdle = idle;

    int start = memberOffsets_[i];
    int end = memberOffsets_[i + 1];
    if (end <= start) continue;

    int total = end - start;
    uint32_t hash = Hash32(static_cast<uint32_t>(settlement.id), static_cast<uint32_t>(dayCount));
    if (isAtWar) {
      hash = Hash32(static_cast<uint32_t>(settlement.id), 0u);
    }
    int offset = static_cast<int>(hash % static_cast<uint32_t>(total));

    std::vector<int> ordered;
    ordered.reserve(total);
    for (int local = 0; local < total; ++local) {
      int idxInList = start + (offset + local) % total;
      ordered.push_back(memberIndices_[idxInList]);
    }

    std::vector<int> unlocked;
    unlocked.reserve(total);
    int lockedCount = 0;
    if (isAtWar) {
      for (int humanIndex : ordered) {
        if (humanIndex < 0 || humanIndex >= static_cast<int>(humans.HumansMutable().size())) continue;
        auto& human = humans.HumansMutable()[humanIndex];
        if (!human.alive) continue;
        if (human.role == Role::Soldier) {
          human.role = Role::Soldier;
          lockedCount++;
        } else {
          unlocked.push_back(humanIndex);
        }
      }
    } else {
      unlocked = ordered;
    }

    int soldiersToAssign = isAtWar ? std::max(0, soldiers - lockedCount) : soldiers;

    for (int idx = 0; idx < static_cast<int>(unlocked.size()); ++idx) {
      int humanIndex = unlocked[idx];
      if (humanIndex < 0 || humanIndex >= static_cast<int>(humans.HumansMutable().size())) continue;
      auto& human = humans.HumansMutable()[humanIndex];
      if (!human.alive) continue;

      if (idx < farmers) {
        human.role = Role::Farmer;
      } else if (idx < farmers + gatherers) {
        human.role = Role::Gatherer;
      } else if (idx < farmers + gatherers + builders) {
        human.role = Role::Builder;
      } else if (idx < farmers + gatherers + builders + soldiersToAssign) {
        human.role = Role::Soldier;
      } else if (idx < farmers + gatherers + builders + soldiersToAssign + guards) {
        human.role = Role::Guard;
      } else if (idx < farmers + gatherers + builders + soldiersToAssign + guards + scouts) {
        human.role = Role::Scout;
      } else {
        human.role = Role::Idle;
      }
    }

  }
  (void)rng;
}

void SettlementManager::UpdateCapitalStatus(const FactionManager& factions) {
  for (auto& settlement : settlements_) {
    settlement.isCapital = false;
  }
  if (settlements_.empty() || factions.Count() == 0) return;

  std::vector<int> bestPop(factions.Count() + 1, -1);
  std::vector<int> bestAge(factions.Count() + 1, -1);
  std::vector<int> bestSettlement(factions.Count() + 1, -1);

  for (const auto& settlement : settlements_) {
    if (settlement.factionId <= 0) continue;
    if (settlement.factionId >= static_cast<int>(bestPop.size())) continue;
    int pop = settlement.population;
    if (pop > bestPop[settlement.factionId] ||
        (pop == bestPop[settlement.factionId] && settlement.ageDays > bestAge[settlement.factionId])) {
      bestPop[settlement.factionId] = pop;
      bestAge[settlement.factionId] = settlement.ageDays;
      bestSettlement[settlement.factionId] = settlement.id;
    }
  }

  for (auto& settlement : settlements_) {
    if (settlement.factionId <= 0) continue;
    if (settlement.factionId >= static_cast<int>(bestSettlement.size())) continue;
    settlement.isCapital = (settlement.id == bestSettlement[settlement.factionId]);
  }
}

void SettlementManager::UpdateSettlementInfluence(const FactionManager& factions) {
  for (auto& settlement : settlements_) {
    int base = kInfluenceVillage;
    if (settlement.tier == SettlementTier::Town) {
      base = kInfluenceTown;
    } else if (settlement.tier == SettlementTier::City) {
      base = kInfluenceCity;
    }
    if (settlement.isCapital) {
      base += kInfluenceCapitalBonus;
    }

    float popFactor = 1.0f;
    if (settlement.population > 0) {
      popFactor += std::min(0.5f, static_cast<float>(settlement.population) / 220.0f);
    }

    float expansionFactor = 1.0f;
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      expansionFactor = faction->traits.expansionBias + faction->leaderInfluence.expansion;
    }
    float factor = 0.9f + (expansionFactor - 1.0f) * 0.35f;
    factor = std::max(0.7f, std::min(1.6f, factor));

    settlement.influenceRadius = static_cast<int>(std::round(base * factor * popFactor));
    if (settlement.influenceRadius < zoneSize_ * 2) {
      settlement.influenceRadius = zoneSize_ * 2;
    }
  }
}

void SettlementManager::UpdateSettlementStability(const FactionManager& factions, Random& rng) {
  for (auto& settlement : settlements_) {
    if (settlement.population <= 0) {
      settlement.stability = 80;
      settlement.unrest = 0;
      continue;
    }
    UpdateEconomyMood(settlement);
    int pop = settlement.population;
    float foodRatio = static_cast<float>(settlement.stockFood) /
                      static_cast<float>(std::max(1, pop * kDesiredFoodPerPop));
    foodRatio = std::max(0.0f, std::min(1.5f, foodRatio));
    float housingRatio = 0.0f;
    if (settlement.housingCap > 0) {
      housingRatio = static_cast<float>(settlement.housingCap) / static_cast<float>(pop);
      housingRatio = std::max(0.0f, std::min(1.2f, housingRatio));
    }

    float warPenalty = (settlement.warPressure > 0)
                           ? (0.2f + 0.03f * static_cast<float>(settlement.warPressure))
                           : 0.0f;
    float borderPenalty = (settlement.borderPressure > 3) ? 0.08f : 0.0f;

    float leaderBonus = 0.0f;
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      leaderBonus = faction->leaderInfluence.stability;
    }

    float economyBonus = static_cast<float>(settlement.economyProsperity) / 100.0f;
    float economyPenalty = static_cast<float>(settlement.economyStress) / 100.0f;
    float target = 50.0f + 30.0f * foodRatio + 15.0f * housingRatio +
                   20.0f * leaderBonus - 50.0f * warPenalty - 30.0f * borderPenalty;
    target += 10.0f * economyBonus - 15.0f * economyPenalty;
    target += static_cast<float>(settlement.monuments *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Monument));
    target += static_cast<float>(settlement.walls *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Walls));
    target += static_cast<float>(settlement.schools *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::School));
    target += static_cast<float>(settlement.banks *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Bank));
    target += static_cast<float>(settlement.universities *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::University));
    target += static_cast<float>(settlement.powerPlants *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::PowerPlant));
    target += static_cast<float>(settlement.reactors *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Reactor));
    target += static_cast<float>(settlement.satelliteArrays *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::SatelliteArray));
    target += static_cast<float>(settlement.roboticsLabs *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::RoboticsLab));
    target += static_cast<float>(settlement.harbors *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Harbor));
    target += static_cast<float>(settlement.hospitals *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::Hospital));
    target += static_cast<float>(settlement.dataCenters *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::DataCenter));
    target += static_cast<float>(settlement.militaryHQs *
                                 TechSystem::StabilityYieldForBuilding(BuildingType::MilitaryHQ));
    if (settlement.isCapital) {
      target += 6.0f;
    } else if (settlement.borderPressure > 4) {
      target -= 4.0f;
    }
    target = std::max(0.0f, std::min(100.0f, target));

    int delta = static_cast<int>(std::round((target - static_cast<float>(settlement.stability)) * 0.2f));
    if (delta == 0 && target > settlement.stability) delta = 1;
    if (delta == 0 && target < settlement.stability) delta = -1;
    settlement.stability = std::max(0, std::min(100, settlement.stability + delta));

    if (settlement.stability <= kRebellionStabilityThreshold) {
      settlement.unrest++;
    } else {
      settlement.unrest = std::max(0, settlement.unrest - 1);
    }
  }
  (void)rng;
}

void SettlementManager::UpdateSettlementEvolution(const FactionManager& factions, Random& rng) {
  UpdateCapitalStatus(factions);

  for (auto& settlement : settlements_) {
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      settlement.techTier = std::max(0, std::min(kTechMaxTier, faction->techTier));
      const TechSystem::TechDefinition* currentTech =
          TechSystem::DefinitionByIndex(faction->currentTechId);
      settlement.techProgress =
          currentTech && currentTech->cost > 0.0f
              ? std::max(0.0f, std::min(1.0f, faction->currentTechProgress / currentTech->cost))
              : 0.0f;
    }
    if (settlement.population <= 0) continue;
  }

  for (auto& settlement : settlements_) {
    SettlementTier newTier = SettlementTier::Village;
    if (settlement.population >= kTownPopThreshold && settlement.ageDays >= kTownAgeDays) {
      newTier = SettlementTier::Town;
    }
    if (settlement.population >= kCityPopThreshold && settlement.ageDays >= kCityAgeDays &&
        settlement.techTier >= 2) {
      newTier = SettlementTier::City;
    }
    settlement.tier = newTier;
  }

  UpdateSettlementCaps();
  UpdateSettlementStability(factions, rng);
}

void SettlementManager::UpdateSettlementRoleStatsMacro(World& world, const FactionManager& factions,
                                                       int dayCount) {
  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) {
      settlement.gatherers = 0;
      settlement.farmers = 0;
      settlement.builders = 0;
      settlement.guards = 0;
      settlement.soldiers = 0;
      settlement.scouts = 0;
      settlement.idle = 0;
      continue;
    }

    int farmers = std::min(pop, settlement.farms * 2);
    int gatherers = (pop * 25) / 100;
    if (pop >= 6 && gatherers < 1) gatherers = 1;
    int builders = (settlement.stockFood > pop * 3) ? (pop * 20) / 100 : (pop * 10) / 100;
    if (settlement.housingCap < pop + kHousingBuffer) {
      builders = std::max(builders, std::max(1, (pop * 25) / 100));
    }

    bool nearFire = false;
    for (int dy = -10; dy <= 10 && !nearFire; ++dy) {
      for (int dx = -10; dx <= 10; ++dx) {
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        if (world.At(x, y).burning) {
          nearFire = true;
          break;
        }
      }
    }

    int guards = pop * (nearFire ? 8 : 2) / 100;
    int soldiers = 0;
    if (settlement.warPressure > 0 || settlement.borderPressure > 2) {
      int borderBonus = std::min(6, settlement.borderPressure);
      soldiers = std::max(1, (pop * (4 + borderBonus)) / 100);
    }
    if (settlement.tier != SettlementTier::Village) {
      soldiers = std::max(soldiers, pop / 15);
    }
    if (settlement.barracks > 0) {
      soldiers = std::max(soldiers, std::max(1, pop / 10));
    }
    if (settlement.airfields > 0) {
      soldiers = std::max(soldiers, std::max(2, pop / 8));
    }
    if (settlement.militaryHQs > 0) {
      soldiers = std::max(soldiers, std::max(3, pop / 6));
    }
    if (settlement.walls > 0 && (settlement.warPressure > 0 || settlement.borderPressure > 2)) {
      soldiers = std::max(soldiers, std::max(1, pop / 12));
    }
    if (settlement.factionId > 0) {
      AllianceBonus bonus = factions.BonusForFaction(settlement.factionId, dayCount);
      soldiers = static_cast<int>(std::round(static_cast<float>(soldiers) * bonus.soldierCapMult));
    }
    int scouts = 0;
    if (settlement.tier != SettlementTier::Village) {
      scouts = std::max(1, pop / 14);
    } else if (pop >= 12) {
      scouts = 1;
    }

    bool foodEmergency = settlement.stockFood < pop * kEmergencyFoodPerPop;
    if (foodEmergency) {
      int desiredFarmers = std::max(farmers, (pop * kEmergencyFarmerPct) / 100);
      desiredFarmers = std::min(desiredFarmers, settlement.farms * 2);
      int desiredGatherers = std::max(gatherers, (pop * kEmergencyGathererPct) / 100);

      int neededFarmers = std::max(0, desiredFarmers - farmers);
      int neededGatherers = std::max(0, desiredGatherers - gatherers);
      int needed = neededFarmers + neededGatherers;
      if (needed > 0) {
        int available = std::min(needed, builders + guards + soldiers + scouts);
        int shiftSoldiers = std::min(soldiers, available);
        soldiers -= shiftSoldiers;
        available -= shiftSoldiers;
        int shiftGuards = std::min(guards, available);
        guards -= shiftGuards;
        available -= shiftGuards;
        int shiftScouts = std::min(scouts, available);
        scouts -= shiftScouts;
        available -= shiftScouts;
        int shiftBuilders = std::min(builders, available);
        builders -= shiftBuilders;
        int shifted = shiftSoldiers + shiftGuards + shiftScouts + shiftBuilders;
        int addFarmers = std::min(neededFarmers, shifted);
        farmers += addFarmers;
        shifted -= addFarmers;
        int addGatherers = std::min(neededGatherers, shifted);
        gatherers += addGatherers;
      }
    }

    int assigned = farmers + gatherers + builders + guards + soldiers + scouts;
    if (assigned > pop) {
      int overflow = assigned - pop;
      int reduce = std::min(soldiers, overflow);
      soldiers -= reduce;
      overflow -= reduce;
      reduce = std::min(guards, overflow);
      guards -= reduce;
      overflow -= reduce;
      reduce = std::min(scouts, overflow);
      scouts -= reduce;
      overflow -= reduce;
      reduce = std::min(builders, overflow);
      builders -= reduce;
      overflow -= reduce;
      reduce = std::min(farmers, overflow);
      farmers -= reduce;
      overflow -= reduce;
      if (overflow > 0) {
        gatherers = std::max(0, gatherers - overflow);
      }
    }

    settlement.gatherers = gatherers;
    settlement.farmers = farmers;
    settlement.builders = builders;
    settlement.guards = guards;
    settlement.soldiers = soldiers;
    settlement.scouts = scouts;
    settlement.idle = pop - (farmers + gatherers + builders + guards + soldiers + scouts);
  }
}

void SettlementManager::UpdateArmiesAndSieges(World& world, HumanManager& humans, Random& rng,
                                             int dayCount, int dayDelta, FactionManager& factions) {
  UpdateArmyOrders(world, humans, rng, dayCount, dayDelta, factions);

  if (settlements_.empty()) return;
  if (dayDelta < 1) dayDelta = 1;

  auto& humanList = humans.HumansMutable();

  auto factionForHuman = [&](const Human& human) -> int {
    if (human.settlementId <= 0) return -1;
    const Settlement* home = Get(human.settlementId);
    return home ? home->factionId : -1;
  };

  std::vector<std::vector<int>> soldiersByTerritory(settlements_.size());
  for (int humanIndex = 0; humanIndex < static_cast<int>(humanList.size()); ++humanIndex) {
    const Human& human = humanList[humanIndex];
    if (!human.alive) continue;
    if (human.role != Role::Soldier) continue;
    int territorySettlementId = ZoneOwnerForTile(human.x, human.y);
    if (territorySettlementId <= 0) continue;
    int territoryIdx = (territorySettlementId < static_cast<int>(idToIndex_.size()))
                           ? idToIndex_[territorySettlementId]
                           : -1;
    if (territoryIdx < 0 || territoryIdx >= static_cast<int>(soldiersByTerritory.size())) continue;
    soldiersByTerritory[territoryIdx].push_back(humanIndex);
  }

  for (int si = 0; si < static_cast<int>(settlements_.size()); ++si) {
    Settlement& settlement = settlements_[si];
    const int ownerFactionId = settlement.factionId;
    if (ownerFactionId <= 0) continue;

    std::vector<int>& soldierHere = soldiersByTerritory[si];
    if (soldierHere.empty() && settlement.captureProgress <= 0.0f) continue;

    std::vector<int> attackers;
    std::vector<int> defenders;
    int bestEnemyFaction = -1;
    int bestEnemyForce = 0;
    int bestEnemyCoreFaction = -1;
    int bestEnemyCoreForce = 0;
    int defenderSoldiers = 0;
    int defendersInCore = 0;
    int attackersInCore = 0;

    std::unordered_map<int, int> enemyForceByFaction;
    std::unordered_map<int, int> enemyCoreForceByFaction;
    auto inCore = [&](const Human& h) -> bool {
      return (h.x == settlement.centerX && h.y == settlement.centerY);
    };
    for (int humanIndex : soldierHere) {
      const Human& human = humanList[humanIndex];
      int factionId = factionForHuman(human);
      if (factionId <= 0) continue;
      int force = 1;
      if (factionId == ownerFactionId) {
        defenderSoldiers++;
        defenders.push_back(humanIndex);
        if (inCore(human)) defendersInCore += force;
        continue;
      }
      if (!factions.IsAtWar(factionId, ownerFactionId)) continue;
      attackers.push_back(humanIndex);
      enemyForceByFaction[factionId] += force;
      if (inCore(human)) {
        enemyCoreForceByFaction[factionId] += force;
        attackersInCore += force;
      }
    }

    for (const auto& kv : enemyForceByFaction) {
      if (kv.second > bestEnemyForce) {
        bestEnemyForce = kv.second;
        bestEnemyFaction = kv.first;
      }
    }
    for (const auto& kv : enemyCoreForceByFaction) {
      if (kv.second > bestEnemyCoreForce) {
        bestEnemyCoreForce = kv.second;
        bestEnemyCoreFaction = kv.first;
      }
    }

    int leaderFaction =
        (bestEnemyCoreFaction > 0) ? bestEnemyCoreFaction : bestEnemyFaction;
    int warId = (leaderFaction > 0) ? factions.ActiveWarIdBetweenFactions(leaderFaction, ownerFactionId) : -1;
    bool isUnderSiege = !attackers.empty();
    bool coreOccupied = (attackersInCore > 0);
    bool coreDominated = (attackersInCore > defendersInCore);

	    if (isUnderSiege) {
	      long long sumX = 0;
	      long long sumY = 0;
      for (int attackerIndex : attackers) {
        const Human& attacker = humanList[attackerIndex];
        sumX += attacker.x;
        sumY += attacker.y;
      }
      int tx = static_cast<int>(sumX / std::max(1, static_cast<int>(attackers.size())));
      int ty = static_cast<int>(sumY / std::max(1, static_cast<int>(attackers.size())));
      tx = std::clamp(tx, 0, world.width() - 1);
      ty = std::clamp(ty, 0, world.height() - 1);
      settlement.defenseTargetX = tx;
	      settlement.defenseTargetY = ty;
	      settlement.hasDefenseTarget = true;
	    } else {
	      int activeWarId = settlement.warId;
	      if (activeWarId <= 0) {
	        settlement.hasDefenseTarget = false;
	      } else if (factions.WarIsAttacker(activeWarId, ownerFactionId)) {
	        settlement.hasDefenseTarget = false;
	      }
	    }

    if (!isUnderSiege) {
      settlement.siegeDays = 0;
      settlement.occupationDays = 0;
      if (settlement.captureProgress > 0.0f) {
        settlement.captureProgress = std::max(0.0f, settlement.captureProgress - 6.0f * dayDelta);
        if (settlement.captureProgress <= 0.0f) {
          settlement.captureLeaderFactionId = -1;
          settlement.captureWarId = -1;
        }
      }
      continue;
    }

    if (leaderFaction <= 0) continue;

    int recipientFaction = (warId > 0) ? factions.CaptureRecipientFaction(warId, leaderFaction, ownerFactionId)
                                       : leaderFaction;
    settlement.captureWarId = warId;
    if (settlement.captureLeaderFactionId != recipientFaction) {
      if (settlement.captureLeaderFactionId == -1 || settlement.captureProgress < 5.0f) {
        settlement.captureLeaderFactionId = recipientFaction;
      }
    }

    float progress = settlement.captureProgress;
    if (coreOccupied) {
      settlement.siegeDays += dayDelta;
      settlement.occupationDays = coreDominated ? (settlement.occupationDays + dayDelta) : 0;

      int a = std::max(0, attackersInCore);
      int d = std::max(0, defendersInCore);

      float siegeMult = 1.0f + std::min(1.5f, static_cast<float>(settlement.siegeDays) / 30.0f);
      float supplyMult = 1.0f;
      if (settlement.stockFood <= 0) {
        supplyMult += 0.9f;
      } else if (settlement.stockFood < settlement.population * 2) {
        supplyMult += 0.4f;
      }

      const bool defendersClearedFromCore = (d == 0);
      float baseGain = defendersClearedFromCore ? 1.4f : 0.32f;
      float ratio = static_cast<float>(a) / static_cast<float>(std::max(1, d));
      ratio = std::min(10.0f, ratio);

      // No decay at parity; decay only when defenders meaningfully outnumber attackers in the core.
      const int meaningfulOutnumberRatio = 2;
      if (a > 0 && d >= a * meaningfulOutnumberRatio) {
        float decay = 0.35f * static_cast<float>(d) / static_cast<float>(std::max(1, a));
        decay = std::min(2.2f, decay);
        progress -= decay * dayDelta;
      } else {
        progress += baseGain * ratio * siegeMult * supplyMult * dayDelta;
      }

      // Defenders must be eliminated locally (core) to finish the capture.
      if (!defendersClearedFromCore) {
        progress = std::min(progress, 95.0f);
      }

      // Surrender/occupation threshold.
      if (settlement.occupationDays >= 7 && defendersClearedFromCore) {
        progress = 100.0f;
      } else if (settlement.occupationDays >= 14 && a >= std::max(10, d * 10)) {
        progress = 100.0f;
      }
    } else {
      settlement.siegeDays = 0;
      settlement.occupationDays = 0;
      // Attackers are present in the territory but not holding the town center.
      progress -= 0.25f * dayDelta;
    }

    settlement.captureProgress = std::max(0.0f, std::min(100.0f, progress));
    settlement.lastCaptureUpdateDay = dayCount;

	    if (settlement.captureProgress >= 100.0f && settlement.captureLeaderFactionId > 0) {
	      const int resolvedWarId = settlement.captureWarId;
	      int newFactionId = settlement.captureLeaderFactionId;
	      settlement.factionId = newFactionId;
	      settlement.captureProgress = 0.0f;
	      settlement.captureLeaderFactionId = -1;
	      settlement.captureWarId = -1;
      settlement.warId = -1;
      settlement.warTargetSettlementId = -1;
      settlement.generalHumanId = -1;
	      settlement.lastWarOrderDay = dayCount;
	      settlement.siegeDays = 0;
	      settlement.occupationDays = 0;

	      // After a successful capture, focus all invading forces on the next enemy settlement.
	      if (resolvedWarId > 0) {
	        War* war = factions.GetWarMutable(resolvedWarId);
	        if (war && war->active) {
	          const bool invaderSideIsAttacker = factions.WarIsAttacker(resolvedWarId, newFactionId);
	          const std::vector<int>& enemyFactions =
	              invaderSideIsAttacker ? war->defenders.factions : war->attackers.factions;
	          bool preferPopulated = false;
	          for (const auto& candidate : settlements_) {
	            if (candidate.population <= 0) continue;
	            if (std::find(enemyFactions.begin(), enemyFactions.end(), candidate.factionId) == enemyFactions.end()) {
	              continue;
	            }
	            preferPopulated = true;
	            break;
	          }

	          int bestTarget = -1;
	          int bestScore = std::numeric_limits<int>::min();
	          for (const auto& candidate : settlements_) {
	            if (preferPopulated && candidate.population <= 0) continue;
	            if (std::find(enemyFactions.begin(), enemyFactions.end(), candidate.factionId) == enemyFactions.end()) {
	              continue;
	            }
	            int dist = std::abs(candidate.centerX - settlement.centerX) +
	                       std::abs(candidate.centerY - settlement.centerY);
	            int value = candidate.population * 3 + (candidate.isCapital ? 60 : 0) + candidate.techTier * 10;
	            int score = value * 6 - dist * 22;
	            if (score > bestScore) {
	              bestScore = score;
	              bestTarget = candidate.id;
	            }
	          }

	          war->focusTargetSettlementId = bestTarget;
	          war->focusTargetSetDay = dayCount;
	          war->lastMajorEventDay = dayCount;

	          const Settlement* target = (bestTarget > 0) ? Get(bestTarget) : nullptr;
	          if (target) {
	            // Update settlement-level orders immediately.
	            for (auto& s : settlements_) {
	              if (s.population <= 0) continue;
	              if (s.factionId <= 0) continue;
	              if (factions.ActiveWarIdForFaction(s.factionId) != resolvedWarId) continue;
	              bool sideIsAttacker = factions.WarIsAttacker(resolvedWarId, s.factionId);
	              if (sideIsAttacker == invaderSideIsAttacker) {
	                s.warTargetSettlementId = bestTarget;
	                s.lastWarOrderDay = dayCount;
	              } else {
	                s.defenseTargetX = target->centerX;
	                s.defenseTargetY = target->centerY;
	                s.hasDefenseTarget = true;
	              }
	            }

	            // Update soldier orders immediately (so the retarget feels instantaneous).
	            for (auto& h : humanList) {
	              if (!h.alive) continue;
	              if (h.role != Role::Soldier) continue;
	              if (h.warId != resolvedWarId) continue;
	              int hf = factionForHuman(h);
	              if (hf <= 0) continue;
	              bool sideIsAttacker = factions.WarIsAttacker(resolvedWarId, hf);
	              if (sideIsAttacker == invaderSideIsAttacker) {
	                h.warTargetSettlementId = bestTarget;
	                if (h.armyState != ArmyState::Idle) {
	                  h.armyState = ArmyState::March;
	                }
	              } else {
	                if (h.armyState != ArmyState::Idle) {
	                  h.armyState = ArmyState::Defend;
	                }
	              }
	            }
	          }
	        }
	      }

	      int start = (si < static_cast<int>(memberOffsets_.size())) ? memberOffsets_[si] : 0;
	      int end = (si + 1 < static_cast<int>(memberOffsets_.size())) ? memberOffsets_[si + 1] : 0;
	      for (int mi = start; mi < end; ++mi) {
	        int humanIndex = memberIndices_[mi];
        if (humanIndex < 0 || humanIndex >= static_cast<int>(humanList.size())) continue;
        Human& human = humanList[humanIndex];
        if (!human.alive) continue;
        if (human.role == Role::Soldier) {
          human.role = Role::Idle;
          human.armyState = ArmyState::Idle;
          human.warId = -1;
          human.warTargetSettlementId = -1;
          human.hasTask = false;
        }
      }
    }
  }
}

void SettlementManager::MobilizeForWarStart(HumanManager& humans, Random& rng, const FactionManager& factions,
                                            const std::vector<int>& warIdsStarted) {
  if (warIdsStarted.empty()) return;
  if (settlements_.empty()) return;
  if (memberOffsets_.size() < settlements_.size() + 1) return;

  auto isStarted = [&](int warId) -> bool {
    return std::find(warIdsStarted.begin(), warIdsStarted.end(), warId) != warIdsStarted.end();
  };

  auto& humanList = humans.HumansMutable();

  for (int si = 0; si < static_cast<int>(settlements_.size()); ++si) {
    Settlement& settlement = settlements_[si];
    if (settlement.population <= 0) continue;
    if (settlement.factionId <= 0) continue;

    int warId = factions.ActiveWarIdForFaction(settlement.factionId);
    if (warId <= 0 || !isStarted(warId)) continue;

    const bool attacker = factions.WarIsAttacker(warId, settlement.factionId);
    int minSoldiers = std::max(1, settlement.population / (attacker ? 4 : 8));
    if (settlement.tier == SettlementTier::Village) {
      minSoldiers = std::max(1, settlement.population / (attacker ? 6 : 10));
    }
    minSoldiers = std::min(minSoldiers, settlement.population);

    const int start = memberOffsets_[si];
    const int end = memberOffsets_[si + 1];
    if (end <= start) continue;

    std::vector<int> soldierIndices;
    std::vector<int> candidatesIdle;
    std::vector<int> candidatesScout;
    std::vector<int> candidatesBuilder;
    std::vector<int> candidatesGuard;
    std::vector<int> candidatesGatherer;
    std::vector<int> candidatesFarmer;

    for (int mi = start; mi < end; ++mi) {
      int humanIndex = memberIndices_[mi];
      if (humanIndex < 0 || humanIndex >= static_cast<int>(humanList.size())) continue;
      Human& human = humanList[humanIndex];
      if (!human.alive) continue;

      if (human.role == Role::Soldier) {
        soldierIndices.push_back(humanIndex);
        continue;
      }

      switch (human.role) {
        case Role::Idle:
          candidatesIdle.push_back(humanIndex);
          break;
        case Role::Scout:
          candidatesScout.push_back(humanIndex);
          break;
        case Role::Builder:
          candidatesBuilder.push_back(humanIndex);
          break;
        case Role::Guard:
          candidatesGuard.push_back(humanIndex);
          break;
        case Role::Gatherer:
          candidatesGatherer.push_back(humanIndex);
          break;
        case Role::Farmer:
          candidatesFarmer.push_back(humanIndex);
          break;
        default:
          candidatesIdle.push_back(humanIndex);
          break;
      }
    }

    auto promoteFrom = [&](std::vector<int>& list) {
      while (!list.empty() && static_cast<int>(soldierIndices.size()) < minSoldiers) {
        int pick = rng.RangeInt(0, static_cast<int>(list.size()) - 1);
        int humanIndex = list[pick];
        list[pick] = list.back();
        list.pop_back();
        if (humanIndex < 0 || humanIndex >= static_cast<int>(humanList.size())) continue;
        Human& human = humanList[humanIndex];
        if (!human.alive) continue;
        if (human.role == Role::Soldier) continue;
        human.role = Role::Soldier;
        human.hasTask = false;
        soldierIndices.push_back(humanIndex);
      }
    };

    promoteFrom(candidatesIdle);
    promoteFrom(candidatesScout);
    promoteFrom(candidatesBuilder);
    promoteFrom(candidatesGuard);
    promoteFrom(candidatesGatherer);
    promoteFrom(candidatesFarmer);

    if (soldierIndices.empty()) continue;

    settlement.soldiers = static_cast<int>(soldierIndices.size());
    settlement.debugTargetSoldiers = settlement.soldiers;

    settlement.generalHumanId = -1;
  }
}

void SettlementManager::UpdateArmyOrders(World& world, HumanManager& humans, Random& rng, int dayCount,
                                         int dayDelta, FactionManager& factions) {
  if (settlements_.empty()) return;
  if (dayDelta < 1) dayDelta = 1;

  auto& humanList = humans.HumansMutable();
  for (auto& human : humanList) {
    if (!human.alive) continue;
    human.isGeneral = false;
    human.armyState = ArmyState::Idle;
    human.warId = -1;
    human.warTargetSettlementId = -1;
    human.formationSlot = 0;
  }

  auto anyPopulatedEnemySettlement = [&](int warId, int settlementFactionId) -> bool {
    const War* war = factions.GetWar(warId);
    if (!war || !war->active) return false;
    const bool attacker = factions.WarIsAttacker(warId, settlementFactionId);
    const std::vector<int>& enemyFactions = attacker ? war->defenders.factions : war->attackers.factions;
    if (enemyFactions.empty()) return false;
    for (const auto& candidate : settlements_) {
      if (candidate.population <= 0) continue;
      if (std::find(enemyFactions.begin(), enemyFactions.end(), candidate.factionId) == enemyFactions.end()) {
        continue;
      }
      return true;
    }
    return false;
  };

  auto pickWarTarget = [&](const Settlement& from, int warId) -> int {
    const War* war = factions.GetWar(warId);
    if (!war || !war->active) return -1;
    const bool attacker = factions.WarIsAttacker(warId, from.factionId);
    const std::vector<int>& enemyFactions = attacker ? war->defenders.factions : war->attackers.factions;
    if (enemyFactions.empty()) return -1;
    const bool preferPopulated = anyPopulatedEnemySettlement(warId, from.factionId);

    int bestTarget = -1;
    int bestScore = std::numeric_limits<int>::min();
    int bestDist = std::numeric_limits<int>::max();
    for (const auto& candidate : settlements_) {
      if (candidate.id == from.id) continue;
      if (preferPopulated && candidate.population <= 0) continue;
      if (std::find(enemyFactions.begin(), enemyFactions.end(), candidate.factionId) == enemyFactions.end()) {
        continue;
      }
      int dx = candidate.centerX - from.centerX;
      int dy = candidate.centerY - from.centerY;
      int dist = std::abs(dx) + std::abs(dy);
      if (from.borderPressure > 0) {
        if (dist < bestDist) {
          bestDist = dist;
          bestTarget = candidate.id;
        }
        continue;
      }
      int value = candidate.population * 3 + (candidate.isCapital ? 60 : 0) + candidate.techTier * 10;
      int score = value * 6 - dist * 25;
      if (score > bestScore) {
        bestScore = score;
        bestTarget = candidate.id;
      }
    }
    return bestTarget;
  };

  auto isValidWarFocusTarget = [&](int warId, int settlementFactionId, int targetSettlementId) -> bool {
    if (targetSettlementId <= 0) return false;
    const War* war = factions.GetWar(warId);
    if (!war || !war->active) return false;
    const Settlement* target = Get(targetSettlementId);
    if (!target) return false;
    const bool attacker = factions.WarIsAttacker(warId, settlementFactionId);
    const std::vector<int>& enemyFactions = attacker ? war->defenders.factions : war->attackers.factions;
    if (enemyFactions.empty()) return false;
    const bool preferPopulated = anyPopulatedEnemySettlement(warId, settlementFactionId);
    if (preferPopulated && target->population <= 0) return false;
    return std::find(enemyFactions.begin(), enemyFactions.end(), target->factionId) != enemyFactions.end();
  };

  auto ensureWarFocusTarget = [&](int warId, const Settlement& refSettlement) -> int {
    War* war = factions.GetWarMutable(warId);
    if (!war || !war->active) return -1;
    if (war->focusTargetSettlementId > 0 &&
        isValidWarFocusTarget(warId, refSettlement.factionId, war->focusTargetSettlementId)) {
      return war->focusTargetSettlementId;
    }
    int next = pickWarTarget(refSettlement, warId);
    war->focusTargetSettlementId = next;
    war->focusTargetSetDay = dayCount;
    return next;
  };

  auto isValidWarTarget = [&](const Settlement& from, int warId, int targetSettlementId) -> bool {
    if (targetSettlementId <= 0) return false;
    const War* war = factions.GetWar(warId);
    if (!war || !war->active) return false;
    const Settlement* target = Get(targetSettlementId);
    if (!target) return false;
    if (target->id == from.id) return false;
    const bool attacker = factions.WarIsAttacker(warId, from.factionId);
    const std::vector<int>& enemyFactions = attacker ? war->defenders.factions : war->attackers.factions;
    if (enemyFactions.empty()) return false;
    const bool preferPopulated = anyPopulatedEnemySettlement(warId, from.factionId);
    if (preferPopulated && target->population <= 0) return false;
    return std::find(enemyFactions.begin(), enemyFactions.end(), target->factionId) != enemyFactions.end();
  };

  for (int si = 0; si < static_cast<int>(settlements_.size()); ++si) {
    Settlement& settlement = settlements_[si];
    int warId = factions.ActiveWarIdForFaction(settlement.factionId);
    settlement.warId = warId;

    if (warId <= 0) {
      settlement.warTargetSettlementId = -1;
      settlement.hasDefenseTarget = false;
    } else {
      const bool attacker = factions.WarIsAttacker(warId, settlement.factionId);
      if (!attacker) {
        settlement.warTargetSettlementId = -1;
        settlement.hasDefenseTarget = false;
        // Defenders rally to the current battleground (war focus target).
        const War* war = factions.GetWar(warId);
        if (war && war->active && war->focusTargetSettlementId > 0) {
          const Settlement* target = Get(war->focusTargetSettlementId);
          if (target) {
            settlement.defenseTargetX = target->centerX;
            settlement.defenseTargetY = target->centerY;
            settlement.hasDefenseTarget = true;
          }
        }
      } else if (!isValidWarTarget(settlement, warId, settlement.warTargetSettlementId)) {
        int focus = ensureWarFocusTarget(warId, settlement);
        settlement.warTargetSettlementId = (focus > 0) ? focus : pickWarTarget(settlement, warId);
        settlement.lastWarOrderDay = dayCount;
      } else {
        // Force attacker settlements to share a single war focus target once it's set.
        int focus = ensureWarFocusTarget(warId, settlement);
        if (focus > 0 && settlement.warTargetSettlementId != focus) {
          settlement.warTargetSettlementId = focus;
          settlement.lastWarOrderDay = dayCount;
        }
      }
    }

    std::vector<int> soldierIndices;
    int start = (si < static_cast<int>(memberOffsets_.size())) ? memberOffsets_[si] : 0;
    int end = (si + 1 < static_cast<int>(memberOffsets_.size())) ? memberOffsets_[si + 1] : 0;
    for (int mi = start; mi < end; ++mi) {
      int humanIndex = memberIndices_[mi];
      if (humanIndex < 0 || humanIndex >= static_cast<int>(humanList.size())) continue;
      Human& human = humanList[humanIndex];
      if (!human.alive) continue;
      // Allow soldiers to target civilians belonging to a warring settlement.
      // (Combat uses warId to identify enemies without needing faction graph access.)
      human.warId = warId;
      if (human.role != Role::Soldier) continue;
      soldierIndices.push_back(humanIndex);
    }

    std::sort(soldierIndices.begin(), soldierIndices.end(),
              [&](int a, int b) { return humanList[a].id < humanList[b].id; });

    for (int pos = 0; pos < static_cast<int>(soldierIndices.size()); ++pos) {
      int idx = soldierIndices[pos];
      Human& human = humanList[idx];
      human.warId = warId;
      human.warTargetSettlementId = settlement.warTargetSettlementId;
      human.formationSlot = pos;

      if (warId <= 0) {
        human.armyState = ArmyState::Idle;
        continue;
      }

      const bool attacker = factions.WarIsAttacker(warId, settlement.factionId);
      if (attacker) {
        if (settlement.warTargetSettlementId <= 0) {
          human.armyState = ArmyState::Defend;
          continue;
        }
        int daysSinceOrder = std::max(0, dayCount - settlement.lastWarOrderDay);
        int rallyDays = (dayDelta > 1) ? 0 : 2;
        human.armyState = (daysSinceOrder < rallyDays) ? ArmyState::Rally : ArmyState::March;
        int owner = ZoneOwnerForTile(human.x, human.y);
        if (owner == settlement.warTargetSettlementId) {
          human.armyState = ArmyState::Siege;
        }
      } else {
        human.armyState = ArmyState::Defend;
      }
    }
  }
  (void)world;
  (void)rng;
}

void SettlementManager::UpdateArmiesAndSiegesMacro(World& world, Random& rng, int dayCount, int dayDelta,
                                                  FactionManager& factions) {
  if (settlements_.empty()) return;
  if (dayDelta < 1) dayDelta = 1;

  auto pickTarget = [&](const Settlement& from, int warId) -> int {
    const War* war = factions.GetWar(warId);
    if (!war || !war->active) return -1;
    const bool attacker = factions.WarIsAttacker(warId, from.factionId);
    const std::vector<int>& enemyFactions = attacker ? war->defenders.factions : war->attackers.factions;
    if (enemyFactions.empty()) return -1;

    int bestTarget = -1;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto& candidate : settlements_) {
      if (candidate.id == from.id) continue;
      if (candidate.population <= 0) continue;
      if (std::find(enemyFactions.begin(), enemyFactions.end(), candidate.factionId) == enemyFactions.end()) {
        continue;
      }
      int dist = std::abs(candidate.centerX - from.centerX) + std::abs(candidate.centerY - from.centerY);
      int value = candidate.population * 3 + (candidate.isCapital ? 60 : 0);
      int score = value * 10 - dist * 12;
      if (score > bestScore) {
        bestScore = score;
        bestTarget = candidate.id;
      }
    }
    return bestTarget;
  };

  for (auto& settlement : settlements_) {
    int warId = factions.ActiveWarIdForFaction(settlement.factionId);
    settlement.warId = warId;
    if (warId <= 0) {
      settlement.macroArmyTargetSettlementId = -1;
      settlement.macroArmyEtaDays = 0;
      settlement.macroArmySieging = false;
      continue;
    }

    const bool attacker = factions.WarIsAttacker(warId, settlement.factionId);
    if (!attacker) {
      settlement.macroArmySieging = false;
      continue;
    }

    if (settlement.macroArmyTargetSettlementId <= 0 || !Get(settlement.macroArmyTargetSettlementId)) {
      settlement.macroArmyTargetSettlementId = pickTarget(settlement, warId);
      settlement.macroArmyEtaDays = 0;
      settlement.macroArmySieging = false;
    }
    if (settlement.macroArmyTargetSettlementId <= 0) continue;

    Settlement* target = GetMutable(settlement.macroArmyTargetSettlementId);
    if (!target) continue;
    if (target->factionId == settlement.factionId) continue;

    if (!settlement.macroArmySieging) {
      if (settlement.macroArmyEtaDays <= 0) {
        int dist = std::abs(target->centerX - settlement.centerX) + std::abs(target->centerY - settlement.centerY);
        settlement.macroArmyEtaDays = std::max(2, dist / 6);
      }
      settlement.macroArmyEtaDays = std::max(0, settlement.macroArmyEtaDays - dayDelta);
      if (settlement.macroArmyEtaDays == 0) {
        settlement.macroArmySieging = true;
      }
      continue;
    }

    int attackers = std::max(0, settlement.soldiers);
    int defenders = std::max(0, target->soldiers);
    int defenderForce = defenders;

    int warBetween = factions.ActiveWarIdBetweenFactions(settlement.factionId, target->factionId);
    int recipient = (warBetween > 0)
                        ? factions.CaptureRecipientFaction(warBetween, settlement.factionId, target->factionId)
                        : settlement.factionId;

    bool defendersEliminated = (defenders == 0);
    if (attackers > 0) {
      float baseGain = defendersEliminated ? 2.2f : 0.4f;
      if (target->captureProgress >= 5.0f && !defendersEliminated) {
        target->captureProgress = 5.0f;
      } else {
        float ratio = static_cast<float>(attackers) / static_cast<float>(std::max(1, defenderForce));
        target->captureProgress = std::min(100.0f, target->captureProgress + baseGain * ratio * dayDelta);
        if (target->captureProgress < 5.0f) target->captureProgress = std::min(5.0f, target->captureProgress);
      }
      target->captureLeaderFactionId = recipient;
      target->captureWarId = warBetween;
    }

    if (attackers > 0 && defenders > 0) {
      int killDef = std::min(defenders, std::max(1, attackers / 20));
      int killAtt = std::min(attackers, std::max(0, defenders / 25));
      target->soldiers = std::max(0, target->soldiers - killDef);
      settlement.soldiers = std::max(0, settlement.soldiers - killAtt);
      warDeathsPending_ += (killDef + killAtt);
      if (warBetween > 0) {
        War* war = factions.GetWarMutable(warBetween);
        if (war && war->active) {
          bool attackerSide = factions.WarIsAttacker(warBetween, settlement.factionId);
          if (attackerSide) {
            war->deathsAttackers += killAtt;
            war->deathsDefenders += killDef;
          } else {
            war->deathsAttackers += killDef;
            war->deathsDefenders += killAtt;
          }
        }
      }
    }

    if (target->captureProgress >= 100.0f && target->captureLeaderFactionId > 0) {
      target->factionId = target->captureLeaderFactionId;
      target->captureProgress = 0.0f;
      target->captureLeaderFactionId = -1;
      target->captureWarId = -1;
      target->generalHumanId = -1;
      target->warTargetSettlementId = -1;
      target->warId = -1;
      settlement.macroArmySieging = false;
      settlement.macroArmyEtaDays = 0;
    }
  }

  (void)rng;
  (void)world;
}

void SettlementManager::ApplyConflictImpact(World& world, HumanManager& humans, Random& rng,
                                             int dayCount, FactionManager& factions) {
  if (settlements_.empty()) return;

  for (size_t i = 0; i < settlements_.size(); ++i) {
    Settlement& settlement = settlements_[i];
    int pop = settlement.population;
    if (pop <= 0) continue;

    int warPressure = settlement.warPressure;
    if (warPressure > 0) {
      settlement.stockFood = std::max(0, settlement.stockFood - warPressure);
      settlement.stockWood = std::max(0, settlement.stockWood - (warPressure / 2));
    }

    if (rebellionsEnabled_ &&
        settlement.unrest >= kRebellionUnrestDays &&
        settlement.stability <= kRebellionStabilityThreshold &&
        settlement.population >= kRebellionMinPop && settlement.factionId > 0) {
      float chance = static_cast<float>(kRebellionStabilityThreshold - settlement.stability) / 200.0f;
      if (rng.Chance(chance)) {
        int parentFaction = settlement.factionId;
        int newFaction = factions.CreateFaction(rng);
        settlement.factionId = newFaction;
        settlement.unrest = 0;
        settlement.stability = 60;
        factions.SetWar(parentFaction, newFaction, true, dayCount, parentFaction);
      }
    }
  }
  (void)world;
  (void)humans;
}

void SettlementManager::ApplyConflictImpactMacro(World& world, Random& rng, int dayCount,
                                                  FactionManager& factions) {
  if (settlements_.empty()) return;

  for (auto& settlement : settlements_) {
    int pop = settlement.MacroTotal();
    if (pop <= 0) continue;
    int warPressure = settlement.warPressure;
    if (warPressure > 0) {
      settlement.stockFood = std::max(0, settlement.stockFood - warPressure);
      settlement.stockWood = std::max(0, settlement.stockWood - (warPressure / 2));
    }

    if (rebellionsEnabled_ &&
        settlement.unrest >= kRebellionUnrestDays &&
        settlement.stability <= kRebellionStabilityThreshold &&
        settlement.MacroTotal() >= kRebellionMinPop && settlement.factionId > 0) {
      float chance = static_cast<float>(kRebellionStabilityThreshold - settlement.stability) / 200.0f;
      if (rng.Chance(chance)) {
        int parentFaction = settlement.factionId;
        int newFaction = factions.CreateFaction(rng);
        settlement.factionId = newFaction;
        settlement.unrest = 0;
        settlement.stability = 60;
        factions.SetWar(parentFaction, newFaction, true, dayCount, parentFaction);
      }
    }
    settlement.population = settlement.MacroTotal();
  }
  (void)dayCount;
  (void)world;
}

void SettlementManager::GenerateTasks(World& world, Random& rng, const FactionManager& factions, int dayCount) {
  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) continue;

    int taskCount = settlement.TaskCount();
    int available = Settlement::kTaskCap - 1 - taskCount;
    if (available <= 0) continue;

    bool foodEmergency = settlement.stockFood < pop * kEmergencyFoodPerPop;
    if (taskCount > Settlement::kTaskCap / 2 && !foodEmergency) continue;

    int desiredFood = pop * kDesiredFoodPerPop;
    int desiredWood = pop * kDesiredWoodPerPop;
    int farmsPerPop = FarmsPerPopForTier(settlement.techTier);
    int desiredFarms = std::max(1, (pop + farmsPerPop - 1) / farmsPerPop);
    int desiredHousing = pop + kHousingBuffer;

    if (settlement.farms > 0 && available > 0) {
      for (int dy = -kFarmWorkRadius; dy <= kFarmWorkRadius && available > 0; ++dy) {
        int y = settlement.centerY + dy;
        if (y < 0 || y >= world.height()) continue;
        for (int dx = -kFarmWorkRadius; dx <= kFarmWorkRadius && available > 0; ++dx) {
          int x = settlement.centerX + dx;
          if (x < 0 || x >= world.width()) continue;
          const Tile& tile = world.At(x, y);
          if (tile.building != BuildingType::Farm || tile.buildingOwnerId != settlement.id) continue;
          if (tile.farmStage < Settlement::kFarmReadyStage) continue;

          Task task;
          task.type = TaskType::HarvestFarm;
          task.x = x;
          task.y = y;
          task.amount = FarmYieldForTier(settlement.techTier);
          task.settlementId = settlement.id;
          if (!settlement.PushTask(task)) {
            available = 0;
            break;
          }
          available--;
        }
      }
    }

    if (settlement.farms > 0 && available > 0 &&
        settlement.stockWood >= Settlement::kGranaryWoodCost) {
      int builderBudget = settlement.builders + 1;
      if (foodEmergency) {
        builderBudget = std::max(builderBudget, settlement.builders + settlement.idle / 2);
      }
      int tasksToPush = std::min(available, builderBudget);

      auto hasPlannedGranaryNear = [&](int cx, int cy) {
        for (int idx = settlement.taskHead; idx != settlement.taskTail;
             idx = (idx + 1) % Settlement::kTaskCap) {
          const Task& task = settlement.tasks[idx];
          if (task.type != TaskType::BuildStructure || task.buildType != BuildingType::Granary) {
            continue;
          }
          int dist = std::abs(task.x - cx) + std::abs(task.y - cy);
          if (dist <= kGranaryDropRadius) return true;
        }
        return false;
      };

      for (int dy = -kFarmWorkRadius; dy <= kFarmWorkRadius && available > 0 && tasksToPush > 0;
           ++dy) {
        int y = settlement.centerY + dy;
        if (y < 0 || y >= world.height()) continue;
        for (int dx = -kFarmWorkRadius;
             dx <= kFarmWorkRadius && available > 0 && tasksToPush > 0; ++dx) {
          int x = settlement.centerX + dx;
          if (x < 0 || x >= world.width()) continue;
          const Tile& tile = world.At(x, y);
          if (tile.building != BuildingType::Farm || tile.buildingOwnerId != settlement.id) continue;
          int distToTown = std::abs(x - settlement.centerX) + std::abs(y - settlement.centerY);
          if (distToTown <= kGranaryDropRadius) continue;

          bool hasGranary = false;
          for (int gdy = -kGranaryDropRadius; gdy <= kGranaryDropRadius && !hasGranary; ++gdy) {
            for (int gdx = -kGranaryDropRadius; gdx <= kGranaryDropRadius; ++gdx) {
              int gdist = std::abs(gdx) + std::abs(gdy);
              if (gdist > kGranaryDropRadius) continue;
              int tx = x + gdx;
              int ty = y + gdy;
              if (!world.InBounds(tx, ty)) continue;
              const Tile& check = world.At(tx, ty);
              if (check.building == BuildingType::Granary &&
                  check.buildingOwnerId == settlement.id) {
                hasGranary = true;
                break;
              }
            }
          }
          if (hasGranary || hasPlannedGranaryNear(x, y)) continue;

          int bestX = -1;
          int bestY = -1;
          int bestScore = std::numeric_limits<int>::min();
          for (int gdy = -kGranaryBuildRadius; gdy <= kGranaryBuildRadius; ++gdy) {
            for (int gdx = -kGranaryBuildRadius; gdx <= kGranaryBuildRadius; ++gdx) {
              int gdist = std::abs(gdx) + std::abs(gdy);
              if (gdist > kGranaryBuildRadius) continue;
              int tx = x + gdx;
              int ty = y + gdy;
              if (!IsBuildableTileForSettlement(world, *this, settlement.id, tx, ty)) continue;
              const Tile& candidate = world.At(tx, ty);
              int score = -gdist * 20 - candidate.trees * 3 - candidate.food * 2;
              if (score > bestScore) {
                bestScore = score;
                bestX = tx;
                bestY = ty;
              }
            }
          }

          if (bestX == -1 || bestY == -1) continue;
          Task task;
          task.type = TaskType::BuildStructure;
          task.x = bestX;
          task.y = bestY;
          task.amount = 0;
          task.settlementId = settlement.id;
          task.buildType = BuildingType::Granary;
          if (!settlement.PushTask(task)) {
            available = 0;
            break;
          }
          available--;
          tasksToPush--;
        }
      }
    }

    if (available > 0 && settlement.stockWood >= Settlement::kWellWoodCost) {
      bool needsWater = !settlement.hasWaterTarget ||
                        world.WaterScentAt(settlement.centerX, settlement.centerY) <
                            kWellWaterScentThreshold;
      if (needsWater) {
        int plannedWells = 0;
        for (int idx = settlement.taskHead; idx != settlement.taskTail;
             idx = (idx + 1) % Settlement::kTaskCap) {
          const Task& task = settlement.tasks[idx];
          if (task.type == TaskType::BuildStructure && task.buildType == BuildingType::Well) {
            plannedWells++;
          }
        }
        int desiredWells = std::max(1, pop / 40);
        int wellsNeeded = desiredWells - (settlement.wells + plannedWells);
        if (wellsNeeded > 0) {
          int builderBudget = settlement.builders + std::max(1, settlement.idle / 4);
          int tasksToPush = std::min(available, std::min(wellsNeeded, builderBudget));

          auto hasFreshWaterWithin = [&](int cx, int cy, int radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
              int y = cy + dy;
              if (y < 0 || y >= world.height()) continue;
              for (int dx = -radius; dx <= radius; ++dx) {
                int x = cx + dx;
                if (x < 0 || x >= world.width()) continue;
                int dist = std::abs(dx) + std::abs(dy);
                if (dist > radius) continue;
                if (world.At(x, y).type == TileType::FreshWater) return true;
              }
            }
            return false;
          };

          auto hasWellWithin = [&](int cx, int cy, int radius, int requiredRadius) {
            for (int dy = -radius; dy <= radius; ++dy) {
              int y = cy + dy;
              if (y < 0 || y >= world.height()) continue;
              for (int dx = -radius; dx <= radius; ++dx) {
                int x = cx + dx;
                if (x < 0 || x >= world.width()) continue;
                int dist = std::abs(dx) + std::abs(dy);
                if (dist > radius) continue;
                if (world.WellRadiusAt(x, y) == requiredRadius) return true;
              }
            }
            return false;
          };

          auto wellRadiusForNewWell = [&](int cx, int cy) {
            if (hasFreshWaterWithin(cx, cy, kWellSourceRadius)) {
              return kWellRadiusStrong;
            }
            if (hasWellWithin(cx, cy, kWellRadiusStrong, kWellRadiusStrong)) {
              return kWellRadiusMedium;
            }
            if (hasWellWithin(cx, cy, kWellRadiusMedium, kWellRadiusMedium)) {
              return kWellRadiusWeak;
            }
            if (hasWellWithin(cx, cy, kWellRadiusWeak, kWellRadiusWeak)) {
              return kWellRadiusTiny;
            }
            return 0;
          };

          for (int i = 0; i < tasksToPush; ++i) {
            int bestX = -1;
            int bestY = -1;
            int bestScore = std::numeric_limits<int>::min();

            for (int sample = 0; sample < 16; ++sample) {
              int dx = rng.RangeInt(-kWaterSearchRadius, kWaterSearchRadius);
              int dy = rng.RangeInt(-kWaterSearchRadius, kWaterSearchRadius);
              int x = settlement.centerX + dx;
              int y = settlement.centerY + dy;
              if (!IsBuildableTileForSettlement(world, *this, settlement.id, x, y)) continue;
              int newRadius = wellRadiusForNewWell(x, y);
              if (newRadius == 0) continue;
              const Tile& tile = world.At(x, y);
              int dist = std::abs(dx) + std::abs(dy);
              int score = newRadius * 120 - dist * 8 - tile.trees * 2 - tile.food * 2;
              if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
              }
            }

            if (bestX == -1 || bestY == -1) break;
            Task task;
            task.type = TaskType::BuildStructure;
            task.x = bestX;
            task.y = bestY;
            task.amount = 0;
            task.settlementId = settlement.id;
            task.buildType = BuildingType::Well;
            if (!settlement.PushTask(task)) break;
            available--;
            if (available <= 0) break;
          }
        }
      }
    }

    if (available > 0) {
      int foodNeed = std::max(0, desiredFood - settlement.stockFood);
      int tasksToPush = std::min(available, std::max(pop * 4, foodNeed));
      for (int i = 0; i < tasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 8; ++sample) {
          int dx = rng.RangeInt(-kGatherRadius, kGatherRadius);
          int dy = rng.RangeInt(-kGatherRadius, kGatherRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!world.InBounds(x, y)) continue;
          const Tile& tile = world.At(x, y);
          if ((tile.type != TileType::Land && tile.type != TileType::Sand) || tile.burning) continue;
          if (tile.food <= 0) continue;

          int score = static_cast<int>(world.FoodScentAt(x, y)) + tile.food * 200;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::CollectFood;
        task.x = bestX;
        task.y = bestY;
        task.amount = GatherYieldForTier(settlement.techTier);
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (available > 0 && settlement.gatherers > 0) {
      int farTasksToPush = std::min(available, std::max(1, settlement.gatherers / 2));
      for (int i = 0; i < farTasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 12; ++sample) {
          int dx = rng.RangeInt(-kFarGatherRadius, kFarGatherRadius);
          int dy = rng.RangeInt(-kFarGatherRadius, kFarGatherRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!world.InBounds(x, y)) continue;
          const Tile& tile = world.At(x, y);
          if ((tile.type != TileType::Land && tile.type != TileType::Sand) || tile.burning) continue;
          if (tile.food <= 0) continue;

          int dist = std::abs(dx) + std::abs(dy);
          int score =
              static_cast<int>(world.FoodScentAt(x, y)) + tile.food * 200 + dist * 10;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::CollectFood;
        task.x = bestX;
        task.y = bestY;
        task.amount = GatherYieldForTier(settlement.techTier);
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.farms > 0 && available > 0) {
      int tasksToPush = std::min(available, std::max(2, settlement.farmers * 4));
      for (int i = 0; i < tasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 10; ++sample) {
          int dx = rng.RangeInt(-kFarmWorkRadius, kFarmWorkRadius);
          int dy = rng.RangeInt(-kFarmWorkRadius, kFarmWorkRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!world.InBounds(x, y)) continue;
          const Tile& tile = world.At(x, y);
          if (tile.building != BuildingType::Farm || tile.buildingOwnerId != settlement.id) continue;
          if (tile.farmStage != 0) continue;

          int score = static_cast<int>(world.WaterScentAt(x, y));
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::PlantFarm;
        task.x = bestX;
        task.y = bestY;
        task.amount = 0;
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.stockWood < desiredWood && available > 0) {
      int need = desiredWood - settlement.stockWood;
      int tasksToPush = std::min(need, std::min(available, pop));
      for (int i = 0; i < tasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 8; ++sample) {
          int dx = rng.RangeInt(-kWoodRadius, kWoodRadius);
          int dy = rng.RangeInt(-kWoodRadius, kWoodRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!world.InBounds(x, y)) continue;
          const Tile& tile = world.At(x, y);
          if (tile.type != TileType::Land || tile.burning) continue;
          if (tile.trees <= 0) continue;

          int score = tile.trees * 150;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::CollectWood;
        task.x = bestX;
        task.y = bestY;
        task.amount = GatherYieldForTier(settlement.techTier);
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.farms < desiredFarms && available > 0 &&
        settlement.stockWood >= Settlement::kFarmWoodCost) {
      int farmsNeeded = desiredFarms - settlement.farms;
      int builderBudget = settlement.builders + 1;
      if (foodEmergency) {
        builderBudget = std::max(builderBudget, settlement.builders + settlement.idle / 2);
      }
      int tasksToPush = std::min(farmsNeeded, std::min(available, builderBudget));
      for (int i = 0; i < tasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 12; ++sample) {
          int dx = rng.RangeInt(-kFarmBuildRadius, kFarmBuildRadius);
          int dy = rng.RangeInt(-kFarmBuildRadius, kFarmBuildRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!IsBuildableTileForSettlement(world, *this, settlement.id, x, y)) continue;
          const Tile& tile = world.At(x, y);
          int score = static_cast<int>(world.WaterScentAt(x, y)) - tile.trees * 4;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::BuildStructure;
        task.x = bestX;
        task.y = bestY;
        task.amount = 0;
        task.settlementId = settlement.id;
        task.buildType = BuildingType::Farm;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.housingCap < desiredHousing && available > 0 &&
        settlement.stockWood >= Settlement::kHouseWoodCost) {
      int needed = desiredHousing - settlement.housingCap;
      int housesNeeded = (needed + Settlement::kHouseCapacity - 1) / Settlement::kHouseCapacity;
      int builderBudget = settlement.builders + std::max(1, settlement.idle / 2);
      if (settlement.housingCap < pop) {
        builderBudget = std::max(builderBudget, settlement.builders + settlement.idle);
      }
      int tasksToPush = std::min(housesNeeded, std::min(available, builderBudget));
      for (int i = 0; i < tasksToPush; ++i) {
        int bestX = -1;
        int bestY = -1;
        int bestScore = std::numeric_limits<int>::min();

        for (int sample = 0; sample < 12; ++sample) {
          int dx = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
          int dy = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
          int x = settlement.centerX + dx;
          int y = settlement.centerY + dy;
          if (!IsBuildableTileForSettlement(world, *this, settlement.id, x, y)) continue;
          const Tile& tile = world.At(x, y);
          int dist = std::abs(dx) + std::abs(dy);
          int score = -dist * 10 - tile.trees * 3 - tile.food * 2;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) break;
        Task task;
        task.type = TaskType::BuildStructure;
        task.x = bestX;
        task.y = bestY;
        task.amount = 0;
        task.settlementId = settlement.id;
        task.buildType = BuildingType::House;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    auto hasPlannedBuilding = [&](BuildingType type) {
      for (int idx = settlement.taskHead; idx != settlement.taskTail;
           idx = (idx + 1) % Settlement::kTaskCap) {
        const Task& task = settlement.tasks[idx];
        if (task.type == TaskType::BuildStructure && task.buildType == type) return true;
      }
      return false;
    };

    auto pushEconomyBuilding = [&](BuildingType type, int radius) {
      int bestX = -1;
      int bestY = -1;
      int bestScore = std::numeric_limits<int>::min();
      for (int sample = 0; sample < 18; ++sample) {
        int dx = rng.RangeInt(-radius, radius);
        int dy = rng.RangeInt(-radius, radius);
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!IsBuildableTileForSettlement(world, *this, settlement.id, x, y)) continue;
        const Tile& tile = world.At(x, y);
        int dist = std::abs(dx) + std::abs(dy);
        int score = -dist * 12 - tile.trees * 3 - tile.food * 2;
        if (type == BuildingType::Market) {
          score += settlement.isCapital ? 40 : 0;
        } else if (type == BuildingType::Forge) {
          score += settlement.stockMetal;
        } else if (TechSystem::IsTechBuilding(type)) {
          score += settlement.isCapital ? 35 : 0;
          if (type == BuildingType::Harbor) {
            int waterScore = WaterAdjacencyScore(world, x, y, 5);
            if (waterScore == 0) continue;
            score += waterScore * 35;
          }
          if (type == BuildingType::Walls || type == BuildingType::Barracks) {
            score += settlement.borderPressure * 12 + settlement.warPressure * 16;
          }
        }
        if (score > bestScore) {
          bestScore = score;
          bestX = x;
          bestY = y;
        }
      }
      if (bestX == -1 || bestY == -1) return false;
      Task task;
      task.type = TaskType::BuildStructure;
      task.x = bestX;
      task.y = bestY;
      task.amount = 0;
      task.settlementId = settlement.id;
      task.buildType = type;
      return settlement.PushTask(task);
    };

    if (available > 0 && !foodEmergency && settlement.markets == 0 &&
        !hasPlannedBuilding(BuildingType::Market) &&
        (settlement.tier != SettlementTier::Village || settlement.population >= 45 ||
         settlement.isCapital) &&
        settlement.economyProsperity >= 25 &&
        settlement.stockWood >= Settlement::kMarketWoodCost &&
        settlement.stockStone >= Settlement::kMarketStoneCost) {
      if (pushEconomyBuilding(BuildingType::Market, kMarketBuildRadius)) {
        available--;
      }
    }

    if (available > 0 && !foodEmergency && settlement.forges == 0 &&
        !hasPlannedBuilding(BuildingType::Forge) &&
        (settlement.techTier >= 1 || settlement.population >= 55 || settlement.soldiers >= 8) &&
        settlement.stockWood >= Settlement::kForgeWoodCost &&
        settlement.stockStone >= Settlement::kForgeStoneCost &&
        settlement.stockMetal >= Settlement::kForgeMetalCost) {
      if (pushEconomyBuilding(BuildingType::Forge, kForgeBuildRadius)) {
        available--;
      }
    }

    const Faction* faction = factions.Get(settlement.factionId);
    constexpr BuildingType kTechBuildings[] = {
        BuildingType::Monument, BuildingType::Archive, BuildingType::Walls,
        BuildingType::Barracks, BuildingType::Mint, BuildingType::School,
        BuildingType::Workshop, BuildingType::Bank, BuildingType::University,
        BuildingType::Factory, BuildingType::PowerPlant, BuildingType::Airfield,
        BuildingType::Reactor, BuildingType::SatelliteArray, BuildingType::RoboticsLab,
        BuildingType::Harbor, BuildingType::RailDepot, BuildingType::Hospital,
        BuildingType::DataCenter, BuildingType::MilitaryHQ};
    for (BuildingType building : kTechBuildings) {
      if (available <= 0 || foodEmergency) break;
      if (hasPlannedBuilding(building)) continue;
      if (!CanAffordBuilding(settlement, building)) continue;
      if (!ShouldBuildTechBuilding(world, settlement, faction, building)) continue;
      int buildRadius = (building == BuildingType::Harbor) ? 22 : kMarketBuildRadius;
      if (pushEconomyBuilding(building, buildRadius)) {
        available--;
      }
    }

    int patrols = std::min(settlement.guards + settlement.soldiers, available);
    for (int i = 0; i < patrols; ++i) {
      int bestX = settlement.centerX;
      int bestY = settlement.centerY;
      for (int attempt = 0; attempt < 6; ++attempt) {
        int dx = rng.RangeInt(-kClaimRadiusTownHall / 2, kClaimRadiusTownHall / 2);
        int dy = rng.RangeInt(-kClaimRadiusTownHall / 2, kClaimRadiusTownHall / 2);
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        if (world.At(x, y).type == TileType::Ocean) continue;
        bestX = x;
        bestY = y;
        break;
      }

      Task task;
      task.type = TaskType::PatrolEdge;
      task.x = bestX;
      task.y = bestY;
      task.amount = 0;
      task.settlementId = settlement.id;
      if (!settlement.PushTask(task)) break;
      available--;
      if (available <= 0) break;
    }
  }
}

void SettlementManager::UpdateCaravans() {
  for (auto& caravan : caravans_) {
    const Settlement* from = Get(caravan.fromSettlementId);
    const Settlement* to = Get(caravan.toSettlementId);
    if (!to) {
      caravan.progress = 1.0f;
      continue;
    }
    caravan.progress = std::min(1.0f, caravan.progress + caravan.travelPerDay);
    float fromX = from ? static_cast<float>(from->centerX) + 0.5f : caravan.x;
    float fromY = from ? static_cast<float>(from->centerY) + 0.5f : caravan.y;
    float toX = static_cast<float>(to->centerX) + 0.5f;
    float toY = static_cast<float>(to->centerY) + 0.5f;
    caravan.x = fromX + (toX - fromX) * caravan.progress;
    caravan.y = fromY + (toY - fromY) * caravan.progress;
  }

  for (auto it = caravans_.begin(); it != caravans_.end();) {
    if (it->progress < 1.0f) {
      ++it;
      continue;
    }
    Settlement* target = GetMutable(it->toSettlementId);
    if (target && it->amount > 0) {
      StockForResource(*target, it->resource) += it->amount;
      UpdateEconomyMood(*target);
    }
    it = caravans_.erase(it);
  }
}

void SettlementManager::TryCreateTradeCaravans(int dayCount, const FactionManager& factions) {
  if (dayCount % kTradeCheckIntervalDays != 0) return;
  if (settlements_.size() < 2) return;

  auto hasActiveSourceCaravan = [&](int settlementId) {
    for (const auto& caravan : caravans_) {
      if (caravan.fromSettlementId == settlementId) return true;
    }
    return false;
  };

  constexpr TradeResource kTradeResources[] = {
      TradeResource::Food, TradeResource::Wood, TradeResource::Stone,
      TradeResource::Metal, TradeResource::Gold};

  for (auto& source : settlements_) {
    if (source.population <= 0 || source.factionId <= 0) continue;
    if (hasActiveSourceCaravan(source.id)) continue;

    int bestScore = std::numeric_limits<int>::min();
    int bestTargetId = -1;
    TradeResource bestResource = TradeResource::Food;
    int bestAmount = 0;

    for (const auto& target : settlements_) {
      if (target.id == source.id || target.population <= 0 || target.factionId <= 0) continue;
      if (source.factionId != target.factionId) {
        if (factions.IsAtWar(source.factionId, target.factionId)) continue;
        if (factions.RelationType(source.factionId, target.factionId) != FactionRelation::Ally) {
          continue;
        }
      }

      int dist = std::abs(source.centerX - target.centerX) + std::abs(source.centerY - target.centerY);
      int maxTradeDistance = kTradeMaxDistanceTiles;
      if (source.harbors > 0 || target.harbors > 0) maxTradeDistance += 70;
      if (source.railDepots > 0 || target.railDepots > 0) maxTradeDistance += 110;
      if (dist <= 0 || dist > maxTradeDistance) continue;

      for (TradeResource resource : kTradeResources) {
        const ResourcePressure& sourcePressure = source.resourcePressure[TradeResourceIndex(resource)];
        const ResourcePressure& targetPressure = target.resourcePressure[TradeResourceIndex(resource)];
        int surplus = sourcePressure.surplus;
        int shortage = targetPressure.shortage;
        if (resource == TradeResource::Gold) {
          if (!target.isCapital && target.tier == SettlementTier::Village) continue;
          surplus = std::max(0, StockForResource(source, resource) -
                                    std::max(sourcePressure.desired, 12));
          shortage = targetPressure.shortage;
        }
        if (surplus < kTradeMinShipment || shortage < kTradeMinShipment) continue;

        int maxShipment = kTradeMaxShipment;
        if (source.markets > 0 || target.markets > 0) maxShipment += 45;
        if (source.harbors > 0 || target.harbors > 0) maxShipment += 55;
        if (source.railDepots > 0 || target.railDepots > 0) maxShipment += 80;
        int amount = std::min(maxShipment, std::min(surplus / 2, shortage));
        if (amount < kTradeMinShipment) continue;

        int score = targetPressure.demandScore * 6 + sourcePressure.supplyScore * 3 +
                    amount * 2 - dist;
        if (source.factionId == target.factionId) score += 35;
        if (resource == TradeResource::Food && target.stockFood < target.population * kEmergencyFoodPerPop) {
          score += 120;
        }
        if (score > bestScore) {
          bestScore = score;
          bestTargetId = target.id;
          bestResource = resource;
          bestAmount = amount;
        }
      }
    }

    if (bestTargetId <= 0 || bestAmount <= 0) continue;
    Settlement* sourceNow = GetMutable(source.id);
    const Settlement* target = Get(bestTargetId);
    if (!sourceNow || !target) continue;
    int& sourceStock = StockForResource(*sourceNow, bestResource);
    if (sourceStock < bestAmount) continue;
    sourceStock -= bestAmount;

    int dist = std::abs(sourceNow->centerX - target->centerX) +
               std::abs(sourceNow->centerY - target->centerY);
    float speedTilesPerDay = (sourceNow->markets > 0 || target->markets > 0) ? 24.0f : 18.0f;
    if (sourceNow->harbors > 0 || target->harbors > 0) speedTilesPerDay += 8.0f;
    if (sourceNow->railDepots > 0 || target->railDepots > 0) speedTilesPerDay += 16.0f;
    float travelPerDay = speedTilesPerDay / static_cast<float>(std::max(18, dist));
    travelPerDay = std::max(0.035f, std::min(0.22f, travelPerDay));

    TradeCaravan caravan;
    caravan.id = nextCaravanId_++;
    caravan.fromSettlementId = sourceNow->id;
    caravan.toSettlementId = target->id;
    caravan.factionId = sourceNow->factionId;
    caravan.resource = bestResource;
    caravan.amount = bestAmount;
    caravan.progress = 0.0f;
    caravan.travelPerDay = travelPerDay;
    caravan.x = static_cast<float>(sourceNow->centerX) + 0.5f;
    caravan.y = static_cast<float>(sourceNow->centerY) + 0.5f;
    caravans_.push_back(caravan);
    UpdateEconomyMood(*sourceNow);
  }
}

void SettlementManager::RunSettlementEconomy(World& world, Random& rng, int dayCount,
                                             const FactionManager& factions) {
  UpdateCaravans();

  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    settlement.lastStoneProduced = 0;
    settlement.lastMetalProduced = 0;
    settlement.lastGoldProduced = 0;
    if (pop <= 0) continue;
    if (settlement.stoneDeposit == 0 && settlement.metalDeposit == 0 &&
        settlement.goldDeposit == 0 && settlement.ageDays <= 2) {
      InitializeSettlementDeposits(settlement);
    }
    if (settlement.harbors > 0 && settlement.stockFood >= pop * kEmergencyFoodPerPop) {
      settlement.stockFood += std::max(4, std::min(18, pop / 8));
      settlement.stockGold += settlement.harbors;
    }

    int desiredWood = pop * kDesiredWoodPerPop;
    if (settlement.stockFood > pop * 4 && settlement.stockWood < desiredWood) {
      int plantAttempts = std::min(settlement.builders + settlement.idle / 2, 60);
      for (int i = 0; i < plantAttempts; ++i) {
        int dx = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
        int dy = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        const Tile& tile = world.At(x, y);
        if (tile.type != TileType::Land || tile.burning) continue;
        if (tile.building != BuildingType::None) continue;
        if (tile.trees >= 12) continue;
        if (rng.Chance(0.25f)) {
          world.EditTile(x, y, [&](Tile& t) {
            int trees = static_cast<int>(t.trees);
            trees = std::min(255, trees + 1);
            t.trees = static_cast<uint8_t>(trees);
          });
        }
      }
    }

    const bool foodEmergency = settlement.stockFood < pop * kEmergencyFoodPerPop;
    int miningLabor = settlement.idle + settlement.gatherers / 4 + settlement.builders / 5;
    if (settlement.tier != SettlementTier::Village) {
      miningLabor += settlement.population / 18;
    }
    miningLabor += settlement.techTier;
    miningLabor += settlement.workshops * 3 + settlement.factories * 8 +
                   settlement.powerPlants * 4 + settlement.reactors * 6 +
                   settlement.roboticsLabs * 10;
    if (foodEmergency) {
      miningLabor /= 4;
    }
    miningLabor = std::max(0, std::min(80, miningLabor));

    int desiredStone = pop * kDesiredStonePerPop + settlement.houses + settlement.townHalls * 8;
    int desiredMetal = std::max(8, pop * kDesiredMetalPerPop / 2 + settlement.soldiers);
    if (miningLabor > 0 && settlement.stoneDeposit > 0 &&
        (settlement.stockStone < desiredStone || settlement.tier != SettlementTier::Village)) {
      int yield = std::max(1, (miningLabor + 3 + settlement.techTier) / 4);
      yield = std::min(yield, settlement.stoneDeposit);
      settlement.stoneDeposit -= yield;
      settlement.stockStone += yield;
      settlement.lastStoneProduced = yield;
    }

    if (miningLabor > 1 && settlement.metalDeposit > 0 &&
        (settlement.techTier > 0 || settlement.soldiers > 0 || settlement.population >= 30) &&
        settlement.stockMetal < desiredMetal * 2) {
      int yield = std::max(1, (miningLabor + settlement.techTier * 2) / 8);
      yield = std::min(yield, settlement.metalDeposit);
      settlement.metalDeposit -= yield;
      settlement.stockMetal += yield;
      settlement.lastMetalProduced = yield;
    }

    if (miningLabor > 2 && settlement.goldDeposit > 0 &&
        (settlement.tier != SettlementTier::Village || settlement.techTier > 0 ||
         settlement.population >= 35)) {
      float chance = 0.10f + std::min(0.45f, static_cast<float>(miningLabor) * 0.012f);
      if (settlement.isCapital) chance += 0.08f;
      if (rng.Chance(chance)) {
        int yield = 1 + ((settlement.techTier >= 2 && miningLabor > 20) ? 1 : 0);
        yield = std::min(yield, settlement.goldDeposit);
        settlement.goldDeposit -= yield;
        settlement.stockGold += yield;
        settlement.lastGoldProduced = yield;
      }
    }

    if (settlement.forges > 0 && settlement.stockMetal > 0) {
      int toolOutput = std::min(settlement.stockMetal,
                                std::max(1, settlement.builders / 8 + settlement.workshops +
                                                settlement.factories * 2 + settlement.roboticsLabs * 2));
      settlement.stockMetal -= toolOutput;
      settlement.stockWood += toolOutput;
      settlement.stockStone += toolOutput / 2 + settlement.factories;
    }

    if (settlement.soldiers > 0 && settlement.stockMetal > 0 && settlement.warPressure > 0) {
      int used = std::min(settlement.stockMetal, std::max(1, settlement.soldiers / 25));
      settlement.stockMetal -= used;
    }

    UpdateEconomyMood(settlement);
  }

  TryCreateTradeCaravans(dayCount, factions);
}

void SettlementManager::UpdateDaily(World& world, HumanManager& humans, Random& rng, int dayCount,
                                    int dayDelta,
                                    std::vector<VillageMarker>& markers, FactionManager& factions) {
  CrashContextSetStage("Settlements::UpdateDaily");
  if (dayDelta < 1) dayDelta = 1;
  EnsureZoneBuffers(world);
  EnsureSettlementFactions(factions, rng);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  RecomputeZoneOwners(world);
  AssignHumansToSettlements(humans);
  RecomputeZonePop(world, humans, dayDelta);
  RunMigration(&humans, rng, dayCount);
  TryFoundNewSettlements(world, &humans, rng, dayCount, markers, factions);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  RecomputeZoneOwners(world);
  AssignHumansToSettlements(humans);
  ComputeSettlementWaterTargets(world);
  UpdateBorderPressure(factions);
  RecomputeSettlementPopAndRoles(world, rng, dayCount, dayDelta, humans, factions);
  UpdateArmiesAndSieges(world, humans, rng, dayCount, dayDelta, factions);
  factions.AdvanceResearch(*this, rng, dayCount, dayDelta, markers);
  UpdateSettlementEvolution(factions, rng);
  ApplyConflictImpact(world, humans, rng, dayCount, factions);
  GenerateTasks(world, rng, factions, dayCount);
  RunSettlementEconomy(world, rng, dayCount, factions);
  if (homeFieldDirty_) {
    world.RecomputeHomeField(*this);
    homeFieldDirty_ = false;
  }
}

void SettlementManager::UpdateMacro(World& world, Random& rng, int dayCount,
                                    std::vector<VillageMarker>& markers, FactionManager& factions) {
  CrashContextSetStage("Settlements::UpdateMacro");
  EnsureZoneBuffers(world);
  EnsureSettlementFactions(factions, rng);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  RecomputeZoneOwners(world);
  RecomputeZonePopMacro();
  RunMigration(nullptr, rng, dayCount);
  TryFoundNewSettlements(world, nullptr, rng, dayCount, markers, factions);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  RecomputeZoneOwners(world);
  idToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    idToIndex_[settlements_[i].id] = i;
  }
  ComputeSettlementWaterTargets(world);
  UpdateBorderPressure(factions);
  factions.AdvanceResearch(*this, rng, dayCount, 1, markers);
  UpdateSettlementEvolution(factions, rng);
  ApplyConflictImpactMacro(world, rng, dayCount, factions);
  UpdateSettlementRoleStatsMacro(world, factions, dayCount);
  UpdateArmiesAndSiegesMacro(world, rng, dayCount, 1, factions);

	  auto placeBuilding = [&](Settlement& settlement, BuildingType type, int radius) {
	    int bestX = -1;
	    int bestY = -1;
	    int bestScore = std::numeric_limits<int>::min();
	    for (int attempt = 0; attempt < 20; ++attempt) {
	      int dx = rng.RangeInt(-radius, radius);
	      int dy = rng.RangeInt(-radius, radius);
	      int x = settlement.centerX + dx;
	      int y = settlement.centerY + dy;
	      if (!IsBuildableTileForSettlement(world, *this, settlement.id, x, y)) continue;
	      if (type == BuildingType::TownHall && !world.TownHallFootprintAllLand(x, y)) continue;
	      int score = 0;
	      if (type == BuildingType::Farm) {
	        score = static_cast<int>(world.WaterScentAt(x, y));
	      } else {
        score = -(std::abs(dx) + std::abs(dy)) * 10;
        if (type == BuildingType::Harbor) {
          int waterScore = WaterAdjacencyScore(world, x, y, 5);
          if (waterScore == 0) continue;
          score += waterScore * 35;
        }
      }
      if (score > bestScore) {
        bestScore = score;
        bestX = x;
        bestY = y;
      }
	    }
	    if (bestX == -1 || bestY == -1) return false;
	    uint8_t farmStage = (type == BuildingType::Farm) ? 1u : 0u;
	    world.PlaceBuilding(bestX, bestY, type, settlement.id, farmStage);
	    if (world.At(bestX, bestY).building != type) return false;
	    return true;
	  };

  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) continue;
    int desiredHousing = pop + kHousingBuffer;
    int farmsPerPop = FarmsPerPopForTier(settlement.techTier);
    int desiredFarms = std::max(1, (pop + farmsPerPop - 1) / farmsPerPop);

    if (settlement.townHalls == 0 && settlement.stockWood >= Settlement::kTownHallWoodCost) {
      if (placeBuilding(settlement, BuildingType::TownHall, kHouseBuildRadius)) {
        settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kTownHallWoodCost);
      }
    }

    int houseBudget = 6;
    while (houseBudget > 0 && settlement.housingCap < desiredHousing &&
           settlement.stockWood >= Settlement::kHouseWoodCost) {
      if (!placeBuilding(settlement, BuildingType::House, kHouseBuildRadius)) break;
      settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kHouseWoodCost);
      settlement.housingCap += Settlement::kHouseCapacity;
      houseBudget--;
    }

    int farmBudget = 2;
    while (farmBudget > 0 && settlement.farms < desiredFarms &&
           settlement.stockWood >= Settlement::kFarmWoodCost) {
      if (!placeBuilding(settlement, BuildingType::Farm, kFarmBuildRadius)) break;
      settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kFarmWoodCost);
      settlement.farms++;
      farmBudget--;
    }

    if (settlement.markets == 0 && !settlement.debugFoodEmergency &&
        (settlement.tier != SettlementTier::Village || settlement.population >= 45 ||
         settlement.isCapital) &&
        settlement.economyProsperity >= 25 &&
        settlement.stockWood >= Settlement::kMarketWoodCost &&
        settlement.stockStone >= Settlement::kMarketStoneCost) {
      if (placeBuilding(settlement, BuildingType::Market, kMarketBuildRadius)) {
        settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kMarketWoodCost);
        settlement.stockStone = std::max(0, settlement.stockStone - Settlement::kMarketStoneCost);
        settlement.markets++;
      }
    }

    if (settlement.forges == 0 && !settlement.debugFoodEmergency &&
        (settlement.techTier >= 1 || settlement.population >= 55 || settlement.soldiers >= 8) &&
        settlement.stockWood >= Settlement::kForgeWoodCost &&
        settlement.stockStone >= Settlement::kForgeStoneCost &&
        settlement.stockMetal >= Settlement::kForgeMetalCost) {
      if (placeBuilding(settlement, BuildingType::Forge, kForgeBuildRadius)) {
        settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kForgeWoodCost);
        settlement.stockStone = std::max(0, settlement.stockStone - Settlement::kForgeStoneCost);
        settlement.stockMetal = std::max(0, settlement.stockMetal - Settlement::kForgeMetalCost);
        settlement.forges++;
      }
    }

    const Faction* faction = factions.Get(settlement.factionId);
    constexpr BuildingType kTechBuildings[] = {
        BuildingType::Monument, BuildingType::Archive, BuildingType::Walls,
        BuildingType::Barracks, BuildingType::Mint, BuildingType::School,
        BuildingType::Workshop, BuildingType::Bank, BuildingType::University,
        BuildingType::Factory, BuildingType::PowerPlant, BuildingType::Airfield,
        BuildingType::Reactor, BuildingType::SatelliteArray, BuildingType::RoboticsLab,
        BuildingType::Harbor, BuildingType::RailDepot, BuildingType::Hospital,
        BuildingType::DataCenter, BuildingType::MilitaryHQ};
    for (BuildingType building : kTechBuildings) {
      if (settlement.debugFoodEmergency) break;
      if (!CanAffordBuilding(settlement, building)) continue;
      if (!ShouldBuildTechBuilding(world, settlement, faction, building)) continue;
      int buildRadius = (building == BuildingType::Harbor) ? 22 : kMarketBuildRadius;
      if (placeBuilding(settlement, building, buildRadius)) {
        PayBuildingCost(settlement, building);
        switch (building) {
          case BuildingType::Monument:
            settlement.monuments++;
            break;
          case BuildingType::Archive:
            settlement.archives++;
            break;
          case BuildingType::Walls:
            settlement.walls++;
            break;
          case BuildingType::Barracks:
            settlement.barracks++;
            break;
          case BuildingType::Mint:
            settlement.mints++;
            break;
          case BuildingType::School:
            settlement.schools++;
            break;
          case BuildingType::Workshop:
            settlement.workshops++;
            break;
          case BuildingType::Bank:
            settlement.banks++;
            break;
          case BuildingType::University:
            settlement.universities++;
            break;
          case BuildingType::Factory:
            settlement.factories++;
            break;
          case BuildingType::PowerPlant:
            settlement.powerPlants++;
            break;
          case BuildingType::Airfield:
            settlement.airfields++;
            break;
          case BuildingType::Reactor:
            settlement.reactors++;
            break;
          case BuildingType::SatelliteArray:
            settlement.satelliteArrays++;
            break;
          case BuildingType::RoboticsLab:
            settlement.roboticsLabs++;
            break;
          case BuildingType::Harbor:
            settlement.harbors++;
            break;
          case BuildingType::RailDepot:
            settlement.railDepots++;
            break;
          case BuildingType::Hospital:
            settlement.hospitals++;
            break;
          case BuildingType::DataCenter:
            settlement.dataCenters++;
            break;
          case BuildingType::MilitaryHQ:
            settlement.militaryHQs++;
            break;
          default:
            break;
        }
      }
    }
  }

  for (uint64_t coord : world.BuildingTiles()) {
    int x = static_cast<int>(static_cast<uint32_t>(coord >> 32));
    int y = static_cast<int>(static_cast<uint32_t>(coord & 0xffffffffu));
    const Tile& tile = world.At(x, y);
    if (tile.building == BuildingType::Farm && tile.farmStage == 0) {
      world.EditTile(x, y, [&](Tile& t) { t.farmStage = 1; });
    }
  }

  RunSettlementEconomy(world, rng, dayCount, factions);

  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  if (homeFieldDirty_) {
    world.RecomputeHomeField(*this);
    homeFieldDirty_ = false;
  }
}
