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
constexpr int kZonePopThreshold = 14;
constexpr int kZoneRequiredDays = 6;
constexpr int kMinVillageDistTiles = 24;
constexpr int kClaimRadiusTiles = 40;

constexpr int kGatherRadius = 12;
constexpr int kWoodRadius = 12;
constexpr int kHouseBuildRadius = 10;
constexpr int kFarmBuildRadius = 12;
constexpr int kFarmWorkRadius = 14;
constexpr int kHousingBuffer = 2;
constexpr int kDesiredFoodPerPop = 5;
constexpr int kDesiredWoodPerPop = 2;
constexpr int kFarmsPerPop = 3;
constexpr int kWaterSearchRadius = 28;
constexpr int kFactionLinkRadiusTiles = 96;

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

void SettlementManager::EnsureZoneBuffers(const World& world) {
  int neededZonesX = (world.width() + zoneSize_ - 1) / zoneSize_;
  int neededZonesY = (world.height() + zoneSize_ - 1) / zoneSize_;
  if (neededZonesX == zonesX_ && neededZonesY == zonesY_) return;

  zonesX_ = neededZonesX;
  zonesY_ = neededZonesY;
  zonePop_.assign(zonesX_ * zonesY_, 0);
  zoneDenseDays_.assign(zonesX_ * zonesY_, 0);
  zoneOwner_.assign(zonesX_ * zonesY_, -1);
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
    float expansionBias = sourceFaction ? sourceFaction->traits.expansionBias : 1.0f;
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

    Settlement settlement;
    settlement.id = nextId_++;
    settlement.centerX = bestX;
    settlement.centerY = bestY;
    settlement.factionId = 0;
    settlement.stockFood = 50 + starterFood;
    settlement.stockWood = 0;
    settlement.population = 0;
    settlement.ageDays = 0;

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

  const int claimDistSq = kClaimRadiusTiles * kClaimRadiusTiles;

  for (int zy = 0; zy < zonesY_; ++zy) {
    for (int zx = 0; zx < zonesX_; ++zx) {
      int zoneIndex = zy * zonesX_ + zx;
      int centerX = std::min(world.width() - 1, zx * zoneSize_ + zoneSize_ / 2);
      int centerY = std::min(world.height() - 1, zy * zoneSize_ + zoneSize_ / 2);

      int bestId = -1;
      int bestDist = claimDistSq + 1;
      for (const auto& settlement : settlements_) {
        int dx = settlement.centerX - centerX;
        int dy = settlement.centerY - centerY;
        int dist = dx * dx + dy * dy;
        if (dist <= claimDistSq && dist < bestDist) {
          bestDist = dist;
          bestId = settlement.id;
        }
      }
      zoneOwner_[zoneIndex] = bestId;
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
          break;
        case BuildingType::TownHall:
          settlement.townHalls++;
          break;
        default:
          break;
      }
    }
  }

  for (auto& settlement : settlements_) {
    settlement.housingCap =
        settlement.townHalls * Settlement::kTownHallCapacity + settlement.houses * Settlement::kHouseCapacity;
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
        if (world.At(x, y).type != TileType::FreshWater) continue;
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
    int builders = (settlement.stockFood > pop * 3) ? (pop * 12) / 100 : (pop * 5) / 100;

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

    int assigned = farmers + gatherers + builders + guards;
    if (assigned > pop) {
      int overflow = assigned - pop;
      if (guards >= overflow) {
        guards -= overflow;
      } else if (builders >= overflow) {
        builders -= overflow;
      } else if (farmers >= overflow) {
        farmers -= overflow;
      } else {
        gatherers = std::max(0, gatherers - overflow);
      }
    }
    int idle = pop - (farmers + gatherers + builders + guards);

    settlement.gatherers = gatherers;
    settlement.farmers = farmers;
    settlement.builders = builders;
    settlement.guards = guards;
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
      } else if (local < farmers + gatherers + builders + guards) {
        human.role = Role::Guard;
      } else {
        human.role = Role::Idle;
      }
    }

  }
  (void)rng;
}

void SettlementManager::GenerateTasks(World& world, Random& rng) {
  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) continue;

    int available = Settlement::kTaskCap - 1 - settlement.TaskCount();
    if (available <= 0) continue;

    if (settlement.TaskCount() > Settlement::kTaskCap / 2) continue;

    int desiredFood = pop * kDesiredFoodPerPop;
    int desiredWood = pop * kDesiredWoodPerPop;
    int desiredFarms = std::max(1, (pop + kFarmsPerPop - 1) / kFarmsPerPop);
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
          task.amount = Settlement::kFarmYield;
          task.settlementId = settlement.id;
          if (!settlement.PushTask(task)) {
            available = 0;
            break;
          }
          available--;
        }
      }
    }

    if (settlement.stockFood < desiredFood && available > 0) {
      int need = desiredFood - settlement.stockFood;
      int tasksToPush = std::min(need, std::min(available, pop * 2));
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
        task.amount = 1;
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.farms > 0 && available > 0) {
      int tasksToPush = std::min(available, std::max(1, settlement.farmers));
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
        task.amount = 1;
        task.settlementId = settlement.id;
        if (!settlement.PushTask(task)) break;
        available--;
        if (available <= 0) break;
      }
    }

    if (settlement.housingCap < desiredHousing && available > 0 &&
        settlement.stockWood >= Settlement::kHouseWoodCost) {
      int needed = desiredHousing - settlement.housingCap;
      int housesNeeded = (needed + Settlement::kHouseCapacity - 1) / Settlement::kHouseCapacity;
      int tasksToPush = std::min(housesNeeded, std::min(available, settlement.builders + 1));
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

    if (settlement.farms < desiredFarms && available > 0 &&
        settlement.stockWood >= Settlement::kFarmWoodCost) {
      int farmsNeeded = desiredFarms - settlement.farms;
      int tasksToPush = std::min(farmsNeeded, std::min(available, settlement.builders + 1));
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

    int patrols = std::min(settlement.guards, available);
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
  RecomputeZoneOwners(world);
  RecomputeZonePop(world, humans);
  TryFoundNewSettlements(world, rng, dayCount, markers, factions);
  RecomputeZoneOwners(world);
  AssignHumansToSettlements(humans);
  ComputeSettlementWaterTargets(world);
  RecomputeSettlementBuildings(world);
  RecomputeSettlementPopAndRoles(world, rng, dayCount, humans);
  GenerateTasks(world, rng);
  RunSettlementEconomy(world, rng);
  world.RecomputeHomeField(*this);
}

void SettlementManager::UpdateMacro(World& world, Random& rng, int dayCount,
                                    std::vector<VillageMarker>& markers, FactionManager& factions) {
  CrashContextSetStage("Settlements::UpdateMacro");
  EnsureZoneBuffers(world);
  EnsureSettlementFactions(factions, rng);
  RecomputeZoneOwners(world);
  RecomputeZonePopMacro();
  TryFoundNewSettlements(world, rng, dayCount, markers, factions);
  RecomputeZoneOwners(world);
  idToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    idToIndex_[settlements_[i].id] = i;
  }
  ComputeSettlementWaterTargets(world);
  RecomputeSettlementBuildings(world);

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
    tile.farmStage = 0;
    tile.trees = 0;
    tile.food = 0;
    tile.burning = false;
    tile.burnDaysRemaining = 0;
    return true;
  };

  for (auto& settlement : settlements_) {
    int pop = settlement.population;
    if (pop <= 0) continue;
    int desiredHousing = pop + kHousingBuffer;
    int desiredFarms = std::max(1, (pop + kFarmsPerPop - 1) / kFarmsPerPop);

    if (settlement.townHalls == 0 && settlement.stockWood >= Settlement::kTownHallWoodCost) {
      if (placeBuilding(settlement, BuildingType::TownHall, kHouseBuildRadius)) {
        settlement.stockWood = std::max(0, settlement.stockWood - Settlement::kTownHallWoodCost);
      }
    }

    int houseBudget = 2;
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

  RecomputeSettlementBuildings(world);
  world.RecomputeHomeField(*this);
}
