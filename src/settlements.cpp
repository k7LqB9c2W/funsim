#include "settlements.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "factions.h"
#include "humans.h"
#include "util.h"
#include "world.h"

namespace {
constexpr int kZonePopThreshold = 10;
constexpr int kZoneRequiredDays = 3;
constexpr int kMinVillageDistTiles = 16;
constexpr int kClaimRadiusTiles = 40;
constexpr int kInfluenceVillage = 36;
constexpr int kInfluenceTown = 48;
constexpr int kInfluenceCity = 64;
constexpr int kInfluenceCapitalBonus = 10;
constexpr int kTownPopThreshold = 40;
constexpr int kCityPopThreshold = 120;
constexpr int kTownAgeDays = 60;
constexpr int kCityAgeDays = 180;
constexpr int kTechMaxTier = 3;
constexpr int kTechPopBase = 30;
constexpr int kTechPopStep = 55;
constexpr float kTechBaseGain = 0.006f;
constexpr float kTechFoodGain = 0.012f;
constexpr float kTechLeaderGain = 0.006f;
constexpr int kRebellionMinPop = 20;
constexpr int kRebellionStabilityThreshold = 25;
constexpr int kRebellionUnrestDays = 7;
constexpr int kWarLossFoodFactor = 6;
constexpr int kWarLossWoodFactor = 3;

constexpr int kGatherRadius = 12;
constexpr int kWoodRadius = 12;
constexpr int kHouseBuildRadius = 16;
constexpr int kFarmBuildRadius = 12;
constexpr int kFarmWorkRadius = 14;
constexpr int kGranaryDropRadius = 4;
constexpr int kGranaryBuildRadius = 4;
constexpr int kFarGatherRadius = 24;
constexpr int kHousingBuffer = 10;
constexpr int kDesiredFoodPerPop = 60;
constexpr int kDesiredWoodPerPop = 4;
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

bool IsBuildableTile(const World& world, int x, int y) {
  if (!world.InBounds(x, y)) return false;
  const Tile& tile = world.At(x, y);
  if (tile.type != TileType::Land) return false;
  if (tile.burning) return false;
  if (tile.building != BuildingType::None) return false;
  return true;
}
}  // namespace

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
  return zoneOwner_[zy * zonesX_ + zx];
}

int SettlementManager::ZoneOwnerAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return -1;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return -1;
  return zoneOwner_[zy * zonesX_ + zx];
}

int SettlementManager::ZonePopAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return 0;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return 0;
  return zonePop_[zy * zonesX_ + zx];
}

int SettlementManager::ZoneConflictAt(int zx, int zy) const {
  if (zonesX_ == 0 || zonesY_ == 0) return 0;
  if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) return 0;
  if (zoneConflict_.empty()) return 0;
  return zoneConflict_[zy * zonesX_ + zx];
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
  zonePop_.assign(zonesX_ * zonesY_, 0);
  zoneDenseDays_.assign(zonesX_ * zonesY_, 0);
  zoneOwner_.assign(zonesX_ * zonesY_, -1);
  zoneConflict_.assign(zonesX_ * zonesY_, 0);
}

void SettlementManager::RecomputeZonePop(const World& world, const HumanManager& humans) {
  std::fill(zonePop_.begin(), zonePop_.end(), 0);

  for (const auto& human : humans.Humans()) {
    if (!human.alive) continue;
    int zx = human.x / zoneSize_;
    int zy = human.y / zoneSize_;
    if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) continue;
    zonePop_[zy * zonesX_ + zx]++;
  }

  for (int i = 0; i < static_cast<int>(zonePop_.size()); ++i) {
    if (zonePop_[i] >= kZonePopThreshold) {
      zoneDenseDays_[i]++;
    } else {
      zoneDenseDays_[i] = 0;
    }
  }
  (void)world;
}

void SettlementManager::RecomputeZonePopMacro() {
  std::fill(zonePop_.begin(), zonePop_.end(), 0);

  for (const auto& settlement : settlements_) {
    int zx = settlement.centerX / zoneSize_;
    int zy = settlement.centerY / zoneSize_;
    if (zx < 0 || zy < 0 || zx >= zonesX_ || zy >= zonesY_) continue;
    zonePop_[zy * zonesX_ + zx] += settlement.population;
  }

  for (int i = 0; i < static_cast<int>(zonePop_.size()); ++i) {
    if (zonePop_[i] >= kZonePopThreshold) {
      zoneDenseDays_[i]++;
    } else {
      zoneDenseDays_[i] = 0;
    }
  }
}

void SettlementManager::TryFoundNewSettlements(World& world, Random& rng, int dayCount,
                                               std::vector<VillageMarker>& markers,
                                               FactionManager& factions) {
  const int minDistSq = kMinVillageDistTiles * kMinVillageDistTiles;

  for (int zoneIndex = 0; zoneIndex < static_cast<int>(zoneDenseDays_.size()); ++zoneIndex) {
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
    if (tooClose) continue;

    int zonePop = zonePop_[zoneIndex];
    int sourceFactionId = 0;
    const Settlement* nearestSettlement = Get(nearestId);
    if (nearestSettlement) {
      sourceFactionId = nearestSettlement->factionId;
    }
    const Faction* sourceFaction = factions.Get(sourceFactionId);
    float expansionBias = sourceFaction ? sourceFaction->traits.expansionBias + sourceFaction->leaderInfluence.expansion
                                        : 1.0f;
    expansionBias = std::max(0.6f, std::min(1.6f, expansionBias));
    int popThreshold = std::max(6, static_cast<int>(std::round(kZonePopThreshold / expansionBias)));
    int requiredDays = std::max(2, static_cast<int>(std::round(kZoneRequiredDays / expansionBias)));
    if (zonePop < popThreshold) continue;
    if (zoneDenseDays_[zoneIndex] < requiredDays) continue;

    int ownerId = zoneOwner_[zoneIndex];
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
          continue;
        }
      }
    }

    int bestScore = std::numeric_limits<int>::min();
    int bestX = -1;
    int bestY = -1;

    for (int y = startY; y < endY; ++y) {
      for (int x = startX; x < endX; ++x) {
        const Tile& tile = world.At(x, y);
        if (tile.type != TileType::Land) continue;
        if (tile.burning) continue;

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

    if (bestX == -1 || bestY == -1) continue;

    Tile& centerTile = world.At(bestX, bestY);
    int starterFood = centerTile.food;
    centerTile.building = BuildingType::TownHall;
    centerTile.buildingOwnerId = nextId_;
    centerTile.farmStage = 0;
    centerTile.trees = 0;
    centerTile.food = 0;
    centerTile.burning = false;
    centerTile.burnDaysRemaining = 0;
    world.MarkBuildingDirty();

    Settlement settlement;
    settlement.id = nextId_++;
    settlement.centerX = bestX;
    settlement.centerY = bestY;
    settlement.factionId = 0;
    settlement.stockFood = 50 + starterFood;
    settlement.stockWood = 0;
    settlement.population = 0;
    settlement.ageDays = 0;
    settlement.tier = SettlementTier::Village;
    settlement.techTier = 0;
    settlement.techProgress = 0.0f;
    settlement.stability = 70;
    settlement.unrest = 0;
    settlement.borderPressure = 0;
    settlement.warPressure = 0;
    settlement.influenceRadius = kInfluenceVillage;
    settlement.isCapital = false;

    int factionId = 0;
    if (nearestSettlement && sourceFactionId > 0) {
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
    homeFieldDirty_ = true;

    markers.push_back(VillageMarker{bestX, bestY, 25});
    zoneDenseDays_[zoneIndex] = 0;

  }
  (void)dayCount;
}

void SettlementManager::RecomputeZoneOwners(const World& world) {
  if (settlements_.empty()) {
    std::fill(zoneOwner_.begin(), zoneOwner_.end(), -1);
    return;
  }

  for (int zy = 0; zy < zonesY_; ++zy) {
    for (int zx = 0; zx < zonesX_; ++zx) {
      int zoneIndex = zy * zonesX_ + zx;
      int centerX = std::min(world.width() - 1, zx * zoneSize_ + zoneSize_ / 2);
      int centerY = std::min(world.height() - 1, zy * zoneSize_ + zoneSize_ / 2);

      int bestId = -1;
      int bestDist = std::numeric_limits<int>::max();
      for (const auto& settlement : settlements_) {
        int dx = settlement.centerX - centerX;
        int dy = settlement.centerY - centerY;
        int dist = dx * dx + dy * dy;
        int radius = settlement.influenceRadius > 0 ? settlement.influenceRadius : kClaimRadiusTiles;
        int radiusSq = radius * radius;
        if (dist <= radiusSq && dist < bestDist) {
          bestDist = dist;
          bestId = settlement.id;
        }
      }
      zoneOwner_[zoneIndex] = bestId;
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
  if (zonesX_ <= 0 || zonesY_ <= 0 || zoneOwner_.empty()) return;
  const bool warEnabled = factions.WarEnabled();

  if (zoneConflict_.size() != zoneOwner_.size()) {
    zoneConflict_.assign(zoneOwner_.size(), 0);
  } else {
    std::fill(zoneConflict_.begin(), zoneConflict_.end(), 0);
  }

  auto bumpConflict = [&](int zoneIndex, int amount) {
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zoneConflict_.size())) return;
    if (amount > zoneConflict_[zoneIndex]) {
      zoneConflict_[zoneIndex] = amount;
    }
  };

  for (int zy = 0; zy < zonesY_; ++zy) {
    for (int zx = 0; zx < zonesX_; ++zx) {
      int zoneIndex = zy * zonesX_ + zx;
      int ownerId = zoneOwner_[zoneIndex];
      if (ownerId <= 0) continue;
      Settlement* ownerSettlement = GetMutable(ownerId);
      if (!ownerSettlement) continue;
      int factionA = ownerSettlement->factionId;
      if (factionA <= 0) continue;

      auto handleNeighbor = [&](int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= zonesX_ || ny >= zonesY_) return;
        int neighborIndex = ny * zonesX_ + nx;
        int neighborOwner = zoneOwner_[neighborIndex];
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
          bumpConflict(neighborIndex, 200);
        } else if (hostile) {
          ownerSettlement->warPressure += 1;
          neighborSettlement->warPressure += 1;
          bumpConflict(zoneIndex, 120);
          bumpConflict(neighborIndex, 120);
        }
      };

      handleNeighbor(zx + 1, zy);
      handleNeighbor(zx, zy + 1);
    }
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
    int ownerId = ZoneOwnerForTile(human.x, human.y);
    human.settlementId = ownerId;
    if (ownerId == -1) {
      human.role = Role::Idle;
      continue;
    }
    int idx = idToIndex_[ownerId];
    if (idx < 0) continue;
    human.homeX = settlements_[idx].centerX;
    human.homeY = settlements_[idx].centerY;
  }
}

void SettlementManager::RecomputeSettlementBuildings(const World& world) {
  if (settlements_.empty()) return;
  for (auto& settlement : settlements_) {
    settlement.houses = 0;
    settlement.farms = 0;
    settlement.granaries = 0;
    settlement.wells = 0;
    settlement.farmsPlanted = 0;
    settlement.farmsReady = 0;
    settlement.townHalls = 0;
    settlement.housingCap = 0;
  }

  for (int y = 0; y < world.height(); ++y) {
    for (int x = 0; x < world.width(); ++x) {
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
        case BuildingType::TownHall:
          settlement.townHalls++;
          break;
        default:
          break;
      }
    }
  }

  UpdateSettlementCaps();
}

void SettlementManager::UpdateSettlementCaps() {
  for (auto& settlement : settlements_) {
    int houseCap = settlement.houses * HouseCapacityForTier(settlement.techTier);
    int hallCap = settlement.townHalls * TownHallCapacityForTier(settlement.techTier);
    settlement.housingCap = houseCap + hallCap;
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
                                                       HumanManager& humans) {
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
    settlements_[i].ageDays++;
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
    int idle = pop - (farmers + gatherers + builders + guards + soldiers + scouts);

    settlement.gatherers = gatherers;
    settlement.farmers = farmers;
    settlement.builders = builders;
    settlement.guards = guards;
    settlement.soldiers = soldiers;
    settlement.scouts = scouts;
    settlement.idle = idle;

    int start = memberOffsets_[i];
    int end = memberOffsets_[i + 1];
    if (end <= start) continue;

    int total = end - start;
    uint32_t hash = Hash32(static_cast<uint32_t>(settlement.id), static_cast<uint32_t>(dayCount));
    int offset = static_cast<int>(hash % static_cast<uint32_t>(total));

    for (int local = 0; local < total; ++local) {
      int idxInList = start + (offset + local) % total;
      int humanIndex = memberIndices_[idxInList];
      if (humanIndex < 0 || humanIndex >= static_cast<int>(humans.HumansMutable().size())) continue;
      auto& human = humans.HumansMutable()[humanIndex];

      if (local < farmers) {
        human.role = Role::Farmer;
      } else if (local < farmers + gatherers) {
        human.role = Role::Gatherer;
      } else if (local < farmers + gatherers + builders) {
        human.role = Role::Builder;
      } else if (local < farmers + gatherers + builders + soldiers) {
        human.role = Role::Soldier;
      } else if (local < farmers + gatherers + builders + soldiers + guards) {
        human.role = Role::Guard;
      } else if (local < farmers + gatherers + builders + soldiers + guards + scouts) {
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

    float target = 50.0f + 30.0f * foodRatio + 15.0f * housingRatio +
                   20.0f * leaderBonus - 50.0f * warPenalty - 30.0f * borderPenalty;
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
    if (settlement.population <= 0) continue;
    if (settlement.techTier >= kTechMaxTier) continue;
    int requiredPop = kTechPopBase + settlement.techTier * kTechPopStep;
    float popFactor =
        std::min(1.0f, static_cast<float>(settlement.population) / static_cast<float>(requiredPop));
    float foodRatio = static_cast<float>(settlement.stockFood) /
                      static_cast<float>(std::max(1, settlement.population * kDesiredFoodPerPop));
    foodRatio = std::max(0.0f, std::min(1.4f, foodRatio));

    float leaderBoost = 0.0f;
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      leaderBoost = faction->leaderInfluence.tech;
    }

    float gain = kTechBaseGain + kTechFoodGain * foodRatio * popFactor +
                 leaderBoost * kTechLeaderGain;
    if (settlement.tier == SettlementTier::City) {
      gain += 0.004f;
    }
    gain = std::max(0.0f, std::min(0.05f, gain));
    settlement.techProgress += gain;
    if (settlement.techProgress >= 1.0f) {
      settlement.techProgress -= 1.0f;
      settlement.techTier = std::min(kTechMaxTier, settlement.techTier + 1);
    }
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

  UpdateSettlementInfluence(factions);
  UpdateSettlementCaps();
  UpdateSettlementStability(factions, rng);
}

void SettlementManager::UpdateSettlementRoleStatsMacro(World& world) {
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

void SettlementManager::ApplyConflictImpact(World& world, HumanManager& humans, Random& rng,
                                             int dayCount, FactionManager& factions) {
  if (settlements_.empty()) return;

  for (size_t i = 0; i < settlements_.size(); ++i) {
    Settlement& settlement = settlements_[i];
    int pop = settlement.population;
    if (pop <= 0) continue;

    int warPressure = settlement.warPressure;
    if (warPressure > 0) {
      const Faction* faction = factions.Get(settlement.factionId);
      float aggression = faction ? (faction->traits.aggressionBias + faction->leaderInfluence.aggression)
                                 : 0.5f;
      float defense = 1.0f + settlement.techTier * 0.15f +
                      static_cast<float>(settlement.soldiers) * 0.01f +
                      static_cast<float>(settlement.guards) * 0.006f;
      int baseLoss = std::max(1, warPressure + static_cast<int>(std::round(aggression * 3.0f)));
      int maxLoss = std::max(1, pop / 10 + 1);
      int casualties = std::min(baseLoss, maxLoss);
      casualties = static_cast<int>(
          std::round(static_cast<float>(casualties) / std::max(0.5f, defense)));

      int start = (i < memberOffsets_.size()) ? memberOffsets_[i] : 0;
      int end = (i + 1 < memberOffsets_.size()) ? memberOffsets_[i + 1] : start;
      int available = end - start;

      casualties = std::min(casualties, available);
      warDeathsPending_ += casualties;
      int remaining = casualties;
      while (remaining > 0 && available > 0) {
        int pick = rng.RangeInt(0, available - 1);
        int idx = memberIndices_[start + pick];
        humans.MarkDeadByIndex(idx, dayCount, DeathReason::War);
        memberIndices_[start + pick] = memberIndices_[start + available - 1];
        available--;
        remaining--;
      }

      settlement.stockFood = std::max(0, settlement.stockFood - warPressure * kWarLossFoodFactor);
      settlement.stockWood = std::max(0, settlement.stockWood - warPressure * kWarLossWoodFactor);

      int losses = casualties;
      int take = std::min(settlement.soldiers, losses);
      settlement.soldiers -= take;
      losses -= take;
      take = std::min(settlement.guards, losses);
      settlement.guards -= take;
      losses -= take;
      take = std::min(settlement.builders, losses);
      settlement.builders -= take;
      losses -= take;
      take = std::min(settlement.farmers, losses);
      settlement.farmers -= take;
      losses -= take;
      take = std::min(settlement.gatherers, losses);
      settlement.gatherers -= take;
      losses -= take;
      if (losses > 0) {
        settlement.idle = std::max(0, settlement.idle - losses);
      }
      settlement.population = std::max(0, settlement.population - casualties);
    }

    if (settlement.unrest >= kRebellionUnrestDays &&
        settlement.stability <= kRebellionStabilityThreshold &&
        settlement.population >= kRebellionMinPop && settlement.factionId > 0) {
      float chance = static_cast<float>(kRebellionStabilityThreshold - settlement.stability) / 200.0f;
      if (rng.Chance(chance)) {
        int parentFaction = settlement.factionId;
        int newFaction = factions.CreateFaction(rng);
        settlement.factionId = newFaction;
        settlement.unrest = 0;
        settlement.stability = 60;
        factions.SetWar(parentFaction, newFaction, true);
      }
    }
  }
  (void)world;
}

void SettlementManager::ApplyConflictImpactMacro(World& world, Random& rng, int dayCount,
                                                  FactionManager& factions) {
  if (settlements_.empty()) return;
  const int binOrder[6] = {3, 4, 2, 5, 1, 0};

  for (auto& settlement : settlements_) {
    int pop = settlement.MacroTotal();
    if (pop <= 0) continue;
    int warPressure = settlement.warPressure;
    if (warPressure > 0) {
      const Faction* faction = factions.Get(settlement.factionId);
      float aggression = faction ? (faction->traits.aggressionBias + faction->leaderInfluence.aggression)
                                 : 0.5f;
      float defense = 1.0f + settlement.techTier * 0.15f;
      int baseLoss = std::max(1, warPressure + static_cast<int>(std::round(aggression * 3.0f)));
      int maxLoss = std::max(1, pop / 10 + 1);
      int casualties = std::min(baseLoss, maxLoss);
      casualties = static_cast<int>(
          std::round(static_cast<float>(casualties) / std::max(0.5f, defense)));
      warDeathsPending_ += casualties;
      int remaining = casualties;
      for (int binIndex = 0; binIndex < 6 && remaining > 0; ++binIndex) {
        int bin = binOrder[binIndex];
        int binTotal = settlement.macroPopM[bin] + settlement.macroPopF[bin];
        if (binTotal <= 0) continue;
        int take = std::min(binTotal, remaining);
        int takeM = std::min(settlement.macroPopM[bin], take / 2);
        int takeF = std::min(settlement.macroPopF[bin], take - takeM);
        settlement.macroPopM[bin] -= takeM;
        settlement.macroPopF[bin] -= takeF;
        remaining -= (takeM + takeF);
      }
      settlement.stockFood = std::max(0, settlement.stockFood - warPressure * kWarLossFoodFactor);
      settlement.stockWood = std::max(0, settlement.stockWood - warPressure * kWarLossWoodFactor);
    }

    if (settlement.unrest >= kRebellionUnrestDays &&
        settlement.stability <= kRebellionStabilityThreshold &&
        settlement.MacroTotal() >= kRebellionMinPop && settlement.factionId > 0) {
      float chance = static_cast<float>(kRebellionStabilityThreshold - settlement.stability) / 200.0f;
      if (rng.Chance(chance)) {
        int parentFaction = settlement.factionId;
        int newFaction = factions.CreateFaction(rng);
        settlement.factionId = newFaction;
        settlement.unrest = 0;
        settlement.stability = 60;
        factions.SetWar(parentFaction, newFaction, true);
      }
    }
    settlement.population = settlement.MacroTotal();
  }
  (void)dayCount;
  (void)world;
}

void SettlementManager::GenerateTasks(World& world, Random& rng) {
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
              if (!IsBuildableTile(world, tx, ty)) continue;
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
              if (!IsBuildableTile(world, x, y)) continue;
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
          if (tile.type != TileType::Land || tile.burning) continue;
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
          if (tile.type != TileType::Land || tile.burning) continue;
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
          if (!IsBuildableTile(world, x, y)) continue;
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
          if (!IsBuildableTile(world, x, y)) continue;
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

    int patrols = std::min(settlement.guards + settlement.soldiers, available);
    for (int i = 0; i < patrols; ++i) {
      int bestX = settlement.centerX;
      int bestY = settlement.centerY;
      for (int attempt = 0; attempt < 6; ++attempt) {
        int dx = rng.RangeInt(-kClaimRadiusTiles / 2, kClaimRadiusTiles / 2);
        int dy = rng.RangeInt(-kClaimRadiusTiles / 2, kClaimRadiusTiles / 2);
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

void SettlementManager::RunSettlementEconomy(World& world, Random& rng) {
  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) continue;

    int desiredWood = pop * kDesiredWoodPerPop;
    if (settlement.stockFood > pop * 4 && settlement.stockWood < desiredWood) {
      int plantAttempts = std::min(settlement.builders + settlement.idle / 2, 60);
      for (int i = 0; i < plantAttempts; ++i) {
        int dx = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
        int dy = rng.RangeInt(-kHouseBuildRadius, kHouseBuildRadius);
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        Tile& tile = world.At(x, y);
        if (tile.type != TileType::Land || tile.burning) continue;
        if (tile.building != BuildingType::None) continue;
        if (tile.trees >= 12) continue;
        if (rng.Chance(0.25f)) {
          tile.trees++;
        }
      }
    }
  }
}

void SettlementManager::UpdateDaily(World& world, HumanManager& humans, Random& rng, int dayCount,
                                    std::vector<VillageMarker>& markers, FactionManager& factions) {
  CrashContextSetStage("Settlements::UpdateDaily");
  EnsureZoneBuffers(world);
  EnsureSettlementFactions(factions, rng);
  UpdateSettlementInfluence(factions);
  RecomputeZoneOwners(world);
  RecomputeZonePop(world, humans);
  TryFoundNewSettlements(world, rng, dayCount, markers, factions);
  UpdateSettlementInfluence(factions);
  RecomputeZoneOwners(world);
  AssignHumansToSettlements(humans);
  ComputeSettlementWaterTargets(world);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  UpdateBorderPressure(factions);
  RecomputeSettlementPopAndRoles(world, rng, dayCount, humans);
  UpdateSettlementEvolution(factions, rng);
  ApplyConflictImpact(world, humans, rng, dayCount, factions);
  GenerateTasks(world, rng);
  RunSettlementEconomy(world, rng);
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
  UpdateSettlementInfluence(factions);
  RecomputeZoneOwners(world);
  RecomputeZonePopMacro();
  TryFoundNewSettlements(world, rng, dayCount, markers, factions);
  UpdateSettlementInfluence(factions);
  RecomputeZoneOwners(world);
  idToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    idToIndex_[settlements_[i].id] = i;
  }
  ComputeSettlementWaterTargets(world);
  if (world.ConsumeBuildingDirty()) {
    RecomputeSettlementBuildings(world);
  } else {
    UpdateSettlementCaps();
  }
  UpdateBorderPressure(factions);
  UpdateSettlementEvolution(factions, rng);
  ApplyConflictImpactMacro(world, rng, dayCount, factions);
  UpdateSettlementRoleStatsMacro(world);

  auto placeBuilding = [&](Settlement& settlement, BuildingType type, int radius) {
    int bestX = -1;
    int bestY = -1;
    int bestScore = std::numeric_limits<int>::min();
    for (int attempt = 0; attempt < 20; ++attempt) {
      int dx = rng.RangeInt(-radius, radius);
      int dy = rng.RangeInt(-radius, radius);
      int x = settlement.centerX + dx;
      int y = settlement.centerY + dy;
      if (!IsBuildableTile(world, x, y)) continue;
      int score = 0;
      if (type == BuildingType::Farm) {
        score = static_cast<int>(world.WaterScentAt(x, y));
      } else {
        score = -(std::abs(dx) + std::abs(dy)) * 10;
      }
      if (score > bestScore) {
        bestScore = score;
        bestX = x;
        bestY = y;
      }
    }
    if (bestX == -1 || bestY == -1) return false;
    Tile& tile = world.At(bestX, bestY);
    tile.building = type;
    tile.buildingOwnerId = settlement.id;
    tile.farmStage = (type == BuildingType::Farm) ? 1 : 0;
    tile.trees = 0;
    tile.food = 0;
    tile.burning = false;
    tile.burnDaysRemaining = 0;
    world.MarkBuildingDirty();
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
  }

  for (int y = 0; y < world.height(); ++y) {
    for (int x = 0; x < world.width(); ++x) {
      Tile& tile = world.At(x, y);
      if (tile.building == BuildingType::Farm && tile.farmStage == 0) {
        tile.farmStage = 1;
      }
    }
  }

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
