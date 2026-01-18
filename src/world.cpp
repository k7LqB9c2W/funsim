#include "world.h"

#include <algorithm>
#include <vector>

#include "settlements.h"

namespace {
constexpr int kFireDuration = 4;
constexpr float kFarmGrowBaseChance = 0.25f;
constexpr float kFarmGrowWaterBonus = 0.45f;
}  // namespace

World::World(int width, int height)
    : width_(width),
      height_(height),
      tiles_(width * height),
      foodScent_(width * height, 0),
      waterScent_(width * height, 0),
      fireRisk_(width * height, 0),
      homeScent_(width * height, 0),
      baseFood_(width * height, 0),
      baseWater_(width * height, 0),
      baseFire_(width * height, 0),
      baseHome_(width * height, 0),
      scentScratch_(width * height, 0) {}

bool World::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

Tile& World::At(int x, int y) { return tiles_[y * width_ + x]; }
const Tile& World::At(int x, int y) const { return tiles_[y * width_ + x]; }

void World::UpdateDaily(Random& rng) {
  CrashContextSetStage("World::UpdateDaily");
  std::vector<int> ignitions;
  ignitions.reserve(128);

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      Tile& tile = At(x, y);

      if (tile.type != TileType::Land) {
        tile.trees = 0;
        tile.food = 0;
        tile.burning = false;
        tile.burnDaysRemaining = 0;
        tile.building = BuildingType::None;
        tile.farmStage = 0;
        tile.buildingOwnerId = -1;
        continue;
      }

      if (tile.burning) {
        if (tile.building != BuildingType::None) {
          tile.building = BuildingType::None;
          tile.farmStage = 0;
          tile.buildingOwnerId = -1;
        }
        if (tile.trees > 0) {
          tile.trees = std::max(0, tile.trees - 2);
        }
        tile.burnDaysRemaining--;

        if (tile.trees <= 0 || tile.burnDaysRemaining <= 0) {
          tile.burning = false;
          tile.burnDaysRemaining = 0;
        } else {
          const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (const auto& d : dirs) {
            int nx = x + d[0];
            int ny = y + d[1];
            if (!InBounds(nx, ny)) continue;
            Tile& neighbor = At(nx, ny);
            if (neighbor.type != TileType::Land) continue;
            if (neighbor.burning) continue;
            if (neighbor.trees <= 0) continue;
            if (rng.Chance(0.12f)) {
              ignitions.push_back(ny * width_ + nx);
            }
          }
        }
        continue;
      }
      if (tile.building == BuildingType::Farm && tile.farmStage > 0 &&
          tile.farmStage < Settlement::kFarmReadyStage) {
        int waterAdj = 0;
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
          int nx = x + d[0];
          int ny = y + d[1];
          if (!InBounds(nx, ny)) continue;
          if (At(nx, ny).type == TileType::FreshWater) {
            waterAdj++;
          }
        }
        float waterFactor = static_cast<float>(waterAdj) / 4.0f;
        float chance = kFarmGrowBaseChance + waterFactor * kFarmGrowWaterBonus;
        if (chance > 0.95f) chance = 0.95f;
        if (rng.Chance(chance)) {
          tile.farmStage++;
          if (tile.farmStage > Settlement::kFarmReadyStage) {
            tile.farmStage = Settlement::kFarmReadyStage;
          }
        }
      }
      if (tile.building != BuildingType::Farm && tile.farmStage != 0) {
        tile.farmStage = 0;
      }
    }
  }

  for (int idx : ignitions) {
    Tile& tile = tiles_[idx];
    if (!tile.burning) {
      tile.burning = true;
      tile.burnDaysRemaining = kFireDuration;
    }
  }

  RecomputeScentFields();
}

void World::EraseAt(int x, int y) {
  if (!InBounds(x, y)) return;
  Tile& tile = At(x, y);
  if (tile.type == TileType::Land) {
    tile.type = TileType::Ocean;
  } else if (tile.type == TileType::FreshWater) {
    tile.type = TileType::Land;
  }
  tile.trees = 0;
  tile.food = 0;
  tile.burning = false;
  tile.burnDaysRemaining = 0;
  tile.building = BuildingType::None;
  tile.farmStage = 0;
  tile.buildingOwnerId = -1;
}

int World::TotalTrees() const {
  int total = 0;
  for (const auto& tile : tiles_) {
    total += tile.trees;
  }
  return total;
}

int World::TotalFood() const {
  int total = 0;
  for (const auto& tile : tiles_) {
    total += tile.food;
  }
  return total;
}

uint16_t World::FoodScentAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  return foodScent_[y * width_ + x];
}

uint16_t World::WaterScentAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  return waterScent_[y * width_ + x];
}

uint16_t World::FireRiskAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  return fireRisk_[y * width_ + x];
}

uint16_t World::HomeScentAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  return homeScent_[y * width_ + x];
}

void World::RecomputeScentFields() {
  const int size = width_ * height_;
  if (static_cast<int>(foodScent_.size()) != size) {
    foodScent_.assign(size, 0);
    waterScent_.assign(size, 0);
    fireRisk_.assign(size, 0);
    homeScent_.assign(size, 0);
    baseFood_.assign(size, 0);
    baseWater_.assign(size, 0);
    baseFire_.assign(size, 0);
    baseHome_.assign(size, 0);
    scentScratch_.assign(size, 0);
  }

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      int idx = y * width_ + x;
      const Tile& tile = tiles_[idx];

      uint16_t food = 0;
      if (tile.type == TileType::Land && !tile.burning) {
        int value = tile.food * 120 + tile.trees * 8;
        if (tile.building == BuildingType::Farm &&
            tile.farmStage >= Settlement::kFarmReadyStage) {
          value += 600;
        }
        if (value > 60000) value = 60000;
        if (value < 0) value = 0;
        food = static_cast<uint16_t>(value);
      }
      baseFood_[idx] = food;

      baseWater_[idx] = (tile.type == TileType::FreshWater) ? 60000u : 0u;
      baseFire_[idx] = tile.burning ? 60000u : 0u;
    }
  }

  auto relaxField = [&](const std::vector<uint16_t>& base, std::vector<uint16_t>& field) {
    field = base;
    constexpr int kIters = 6;
    for (int iter = 0; iter < kIters; ++iter) {
      for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
          int idx = y * width_ + x;
          uint16_t best = base[idx];
          uint16_t maxNeighbor = 0;
          if (x > 0) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx - 1]);
          if (x + 1 < width_) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx + 1]);
          if (y > 0) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx - width_]);
          if (y + 1 < height_) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx + width_]);
          uint16_t decayed = static_cast<uint16_t>((maxNeighbor * 19) / 20);
          if (decayed > best) best = decayed;
          scentScratch_[idx] = best;
        }
      }
      field.swap(scentScratch_);
    }
  };

  relaxField(baseFood_, foodScent_);
  relaxField(baseWater_, waterScent_);
  relaxField(baseFire_, fireRisk_);
}

void World::RecomputeHomeField(const SettlementManager& settlements) {
  const int size = width_ * height_;
  if (static_cast<int>(homeScent_.size()) != size) {
    homeScent_.assign(size, 0);
    baseHome_.assign(size, 0);
    scentScratch_.assign(size, 0);
  } else {
    std::fill(homeScent_.begin(), homeScent_.end(), 0);
    std::fill(baseHome_.begin(), baseHome_.end(), 0);
  }

  for (const auto& settlement : settlements.Settlements()) {
    if (!InBounds(settlement.centerX, settlement.centerY)) continue;
    baseHome_[settlement.centerY * width_ + settlement.centerX] = 60000u;
  }

  auto relaxField = [&](const std::vector<uint16_t>& base, std::vector<uint16_t>& field) {
    field = base;
    constexpr int kIters = 6;
    for (int iter = 0; iter < kIters; ++iter) {
      for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
          int idx = y * width_ + x;
          uint16_t best = base[idx];
          uint16_t maxNeighbor = 0;
          if (x > 0) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx - 1]);
          if (x + 1 < width_) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx + 1]);
          if (y > 0) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx - width_]);
          if (y + 1 < height_) maxNeighbor = std::max<uint16_t>(maxNeighbor, field[idx + width_]);
          uint16_t decayed = static_cast<uint16_t>((maxNeighbor * 19) / 20);
          if (decayed > best) best = decayed;
          scentScratch_[idx] = best;
        }
      }
      field.swap(scentScratch_);
    }
  };

  relaxField(baseHome_, homeScent_);
}
