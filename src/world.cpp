#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include "settlements.h"

namespace {
constexpr int kFireDuration = 4;
constexpr float kFarmGrowBaseChance = 0.85f;
constexpr float kFarmGrowWaterBonus = 0.95f;
constexpr int kWellSourceRadius = 6;
constexpr int kWellRadiusStrong = 12;
constexpr int kWellRadiusMedium = 6;
constexpr int kWellRadiusWeak = 3;
constexpr int kWellRadiusTiny = 1;
constexpr int kScentIters = 6;
constexpr int kWaterScentIters = 10;

constexpr char kMapMagic[8] = {'F', 'S', 'M', 'A', 'P', '0', '1', '\0'};

struct MapHeader {
  char magic[8];
  uint32_t width = 0;
  uint32_t height = 0;
};
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
      scentScratch_(width * height, 0),
      wellRadius_(width * height, 0) {}

bool World::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

Tile& World::At(int x, int y) { return tiles_[y * width_ + x]; }
const Tile& World::At(int x, int y) const { return tiles_[y * width_ + x]; }

void World::UpdateDaily(Random& rng) {
  CrashContextSetStage("World::UpdateDaily");
  std::vector<int> ignitions;
  ignitions.reserve(128);
  RecomputeWellRadius();

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
        if (waterAdj == 0) {
          bool hasWellWater = false;
          for (int dy = -kWellRadiusStrong; dy <= kWellRadiusStrong && !hasWellWater; ++dy) {
            int wy = y + dy;
            if (wy < 0 || wy >= height_) continue;
            for (int dx = -kWellRadiusStrong; dx <= kWellRadiusStrong; ++dx) {
              int wx = x + dx;
              if (wx < 0 || wx >= width_) continue;
              int dist = std::abs(dx) + std::abs(dy);
              if (dist > kWellRadiusStrong) continue;
              uint8_t radius = wellRadius_[wy * width_ + wx];
              if (radius > 0 && dist <= radius) {
                hasWellWater = true;
                break;
              }
            }
          }
          if (hasWellWater) {
            waterAdj = 4;
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

uint8_t World::WellRadiusAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  return wellRadius_[y * width_ + x];
}

void World::RecomputeWellRadius() {
  const int size = width_ * height_;
  if (static_cast<int>(wellRadius_.size()) != size) {
    wellRadius_.assign(size, 0);
  } else {
    std::fill(wellRadius_.begin(), wellRadius_.end(), 0);
  }

  struct WellPos {
    int x = 0;
    int y = 0;
    int idx = 0;
  };
  std::vector<WellPos> wells;
  wells.reserve(128);

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      int idx = y * width_ + x;
      if (tiles_[idx].building == BuildingType::Well) {
        wells.push_back(WellPos{x, y, idx});
      }
    }
  }

  auto hasFreshWaterWithin = [&](int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
      int y = cy + dy;
      if (y < 0 || y >= height_) continue;
      for (int dx = -radius; dx <= radius; ++dx) {
        int x = cx + dx;
        if (x < 0 || x >= width_) continue;
        int dist = std::abs(dx) + std::abs(dy);
        if (dist > radius) continue;
        if (tiles_[y * width_ + x].type == TileType::FreshWater) return true;
      }
    }
    return false;
  };

  auto hasWellWithin = [&](int cx, int cy, int radius, int requiredRadius) {
    for (int dy = -radius; dy <= radius; ++dy) {
      int y = cy + dy;
      if (y < 0 || y >= height_) continue;
      for (int dx = -radius; dx <= radius; ++dx) {
        int x = cx + dx;
        if (x < 0 || x >= width_) continue;
        int dist = std::abs(dx) + std::abs(dy);
        if (dist > radius) continue;
        if (wellRadius_[y * width_ + x] == requiredRadius) return true;
      }
    }
    return false;
  };

  for (const auto& well : wells) {
    if (hasFreshWaterWithin(well.x, well.y, kWellSourceRadius)) {
      wellRadius_[well.idx] = kWellRadiusStrong;
    }
  }
  for (const auto& well : wells) {
    if (wellRadius_[well.idx] == 0 &&
        hasWellWithin(well.x, well.y, kWellRadiusStrong, kWellRadiusStrong)) {
      wellRadius_[well.idx] = kWellRadiusMedium;
    }
  }
  for (const auto& well : wells) {
    if (wellRadius_[well.idx] == 0 &&
        hasWellWithin(well.x, well.y, kWellRadiusMedium, kWellRadiusMedium)) {
      wellRadius_[well.idx] = kWellRadiusWeak;
    }
  }
  for (const auto& well : wells) {
    if (wellRadius_[well.idx] == 0 &&
        hasWellWithin(well.x, well.y, kWellRadiusWeak, kWellRadiusWeak)) {
      wellRadius_[well.idx] = kWellRadiusTiny;
    }
  }
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

  RecomputeWellRadius();

  auto wellStrength = [&](uint8_t radius) -> uint16_t {
    if (radius <= 0) return 0;
    int value = (static_cast<int>(radius) * 60000) / kWellRadiusStrong;
    if (value < 12000) value = 12000;
    return static_cast<uint16_t>(value);
  };

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

      uint16_t water = (tile.type == TileType::FreshWater) ? 60000u : 0u;
      if (tile.building == BuildingType::Well && wellRadius_[idx] > 0) {
        water = std::max(water, wellStrength(wellRadius_[idx]));
      }
      baseWater_[idx] = water;
      baseFire_[idx] = tile.burning ? 60000u : 0u;
    }
  }

  auto relaxField = [&](const std::vector<uint16_t>& base, std::vector<uint16_t>& field,
                        int iters) {
    field = base;
    for (int iter = 0; iter < iters; ++iter) {
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

  relaxField(baseFood_, foodScent_, kScentIters);
  relaxField(baseWater_, waterScent_, kWaterScentIters);
  relaxField(baseFire_, fireRisk_, kScentIters);
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

bool World::SaveMap(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  MapHeader header{};
  std::memcpy(header.magic, kMapMagic, sizeof(header.magic));
  header.width = static_cast<uint32_t>(width_);
  header.height = static_cast<uint32_t>(height_);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  if (!out.good()) {
    return false;
  }

  for (const auto& tile : tiles_) {
    uint8_t type = static_cast<uint8_t>(tile.type);
    uint8_t trees = static_cast<uint8_t>(std::min(tile.trees, 255));
    uint8_t food = static_cast<uint8_t>(std::min(tile.food, 255));
    out.write(reinterpret_cast<const char*>(&type), sizeof(type));
    out.write(reinterpret_cast<const char*>(&trees), sizeof(trees));
    out.write(reinterpret_cast<const char*>(&food), sizeof(food));
    if (!out.good()) {
      return false;
    }
  }

  return true;
}

bool World::LoadMap(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }

  MapHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!in.good()) {
    return false;
  }
  if (std::memcmp(header.magic, kMapMagic, sizeof(header.magic)) != 0) {
    return false;
  }
  if (header.width == 0 || header.height == 0) {
    return false;
  }
  if (header.width > std::numeric_limits<size_t>::max() / header.height) {
    return false;
  }

  World loaded(static_cast<int>(header.width), static_cast<int>(header.height));
  const size_t total = static_cast<size_t>(header.width) * static_cast<size_t>(header.height);
  for (size_t i = 0; i < total; ++i) {
    uint8_t type = 0;
    uint8_t trees = 0;
    uint8_t food = 0;
    in.read(reinterpret_cast<char*>(&type), sizeof(type));
    in.read(reinterpret_cast<char*>(&trees), sizeof(trees));
    in.read(reinterpret_cast<char*>(&food), sizeof(food));
    if (!in.good()) {
      return false;
    }

    Tile& tile = loaded.tiles_[i];
    if (type <= static_cast<uint8_t>(TileType::FreshWater)) {
      tile.type = static_cast<TileType>(type);
    } else {
      tile.type = TileType::Ocean;
    }

    if (tile.type == TileType::Land) {
      tile.trees = trees;
      tile.food = food;
    } else {
      tile.trees = 0;
      tile.food = 0;
    }
    tile.burning = false;
    tile.burnDaysRemaining = 0;
    tile.building = BuildingType::None;
    tile.farmStage = 0;
    tile.buildingOwnerId = -1;
  }

  loaded.RecomputeScentFields();
  *this = std::move(loaded);
  return true;
}
