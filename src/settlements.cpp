#include "settlements.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "humans.h"
#include "util.h"
#include "world.h"

namespace {
constexpr int kZonePopThreshold = 14;
constexpr int kZoneRequiredDays = 6;
constexpr int kMinVillageDistTiles = 24;
constexpr int kClaimRadiusTiles = 40;

constexpr int kGatherRadius = 10;
constexpr int kFarmRadius = 8;

uint32_t Hash32(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B9u;
  h ^= b * 0x85EBCA6Bu;
  h ^= (h >> 13);
  h *= 0xC2B2AE35u;
  h ^= (h >> 16);
  return h;
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
                                               std::vector<VillageMarker>& markers) {
  const int minDistSq = kMinVillageDistTiles * kMinVillageDistTiles;

  for (int zoneIndex = 0; zoneIndex < static_cast<int>(zoneDenseDays_.size()); ++zoneIndex) {
    if (zoneDenseDays_[zoneIndex] < kZoneRequiredDays) continue;

    int zx = zoneIndex % zonesX_;
    int zy = zoneIndex / zonesX_;
    int startX = zx * zoneSize_;
    int startY = zy * zoneSize_;
    int endX = std::min(world.width(), startX + zoneSize_);
    int endY = std::min(world.height(), startY + zoneSize_);

    bool tooClose = false;
    for (const auto& settlement : settlements_) {
      int dx = settlement.centerX - (startX + zoneSize_ / 2);
      int dy = settlement.centerY - (startY + zoneSize_ / 2);
      if (dx * dx + dy * dy <= minDistSq) {
        tooClose = true;
        break;
      }
    }
    if (tooClose) continue;

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

    Settlement settlement;
    settlement.id = nextId_++;
    settlement.centerX = bestX;
    settlement.centerY = bestY;
    settlement.factionId = 0;
    settlement.stockFood = 50 + world.At(bestX, bestY).food;
    settlement.stockWood = 0;
    settlement.population = 0;
    settlement.ageDays = 0;
    settlements_.push_back(settlement);

    markers.push_back(VillageMarker{bestX, bestY, 25});
    zoneDenseDays_[zoneIndex] = 0;

  }
  (void)rng;
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

    int gatherers = (pop * 35) / 100;
    if (pop >= 6 && gatherers < 1) gatherers = 1;
    int builders = (settlement.stockFood > pop * 3) ? (pop * 15) / 100 : (pop * 5) / 100;

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

    int assigned = gatherers + builders + guards;
    if (assigned > pop) {
      int overflow = assigned - pop;
      if (guards >= overflow) {
        guards -= overflow;
      } else if (builders >= overflow) {
        builders -= overflow;
      } else {
        gatherers = std::max(0, gatherers - overflow);
      }
    }
    int idle = pop - (gatherers + builders + guards);

    settlement.gatherers = gatherers;
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

      if (local < gatherers) {
        human.role = Role::Gatherer;
      } else if (local < gatherers + builders) {
        human.role = Role::Builder;
      } else if (local < gatherers + builders + guards) {
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

    int desiredFood = pop * 3;
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
          Tile& tile = world.At(x, y);
          if (tile.type != TileType::Land || tile.burning) continue;

          int score = static_cast<int>(world.FoodScentAt(x, y)) + tile.food * 200;
          if (score > bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }

        if (bestX == -1 || bestY == -1) continue;
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

    if (settlement.stockFood > pop * 4) {
      int buildAttempts = std::min(settlement.builders, 80);
      for (int i = 0; i < buildAttempts; ++i) {
        int dx = rng.RangeInt(-kFarmRadius, kFarmRadius);
        int dy = rng.RangeInt(-kFarmRadius, kFarmRadius);
        int x = settlement.centerX + dx;
        int y = settlement.centerY + dy;
        if (!world.InBounds(x, y)) continue;
        Tile& tile = world.At(x, y);
        if (tile.type != TileType::Land || tile.burning) continue;

        tile.food = std::min(50, tile.food + 1);
        if (tile.trees < 20 && rng.Chance(0.05f)) {
          tile.trees++;
        }
      }
    }
  }
}

void SettlementManager::UpdateDaily(World& world, HumanManager& humans, Random& rng, int dayCount,
                                    std::vector<VillageMarker>& markers) {
  CrashContextSetStage("Settlements::UpdateDaily");
  EnsureZoneBuffers(world);
  RecomputeZonePop(world, humans);
  TryFoundNewSettlements(world, rng, dayCount, markers);
  RecomputeZoneOwners(world);
  AssignHumansToSettlements(humans);
  RecomputeSettlementPopAndRoles(world, rng, dayCount, humans);
  GenerateTasks(world, rng);
  RunSettlementEconomy(world, rng);
  world.RecomputeHomeField(*this);
}

void SettlementManager::UpdateMacro(World& world, Random& rng, int dayCount,
                                    std::vector<VillageMarker>& markers) {
  CrashContextSetStage("Settlements::UpdateMacro");
  EnsureZoneBuffers(world);
  RecomputeZonePopMacro();
  TryFoundNewSettlements(world, rng, dayCount, markers);
  RecomputeZoneOwners(world);
  idToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(settlements_.size()); ++i) {
    idToIndex_[settlements_[i].id] = i;
  }
  world.RecomputeHomeField(*this);
}
