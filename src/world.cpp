#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
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

constexpr char kMapMagic[8] = {'F', 'S', 'M', 'A', 'P', '0', '1', '\0'};

struct MapHeader {
  char magic[8];
  uint32_t width = 0;
  uint32_t height = 0;
};

struct Offset {
  int8_t dx = 0;
  int8_t dy = 0;
  uint8_t dist = 0;
};

std::vector<Offset> BuildDiamondOffsets(int radius) {
  std::vector<Offset> offsets;
  offsets.reserve(1 + 2 * radius * (radius + 1));
  for (int dy = -radius; dy <= radius; ++dy) {
    int rem = radius - std::abs(dy);
    for (int dx = -rem; dx <= rem; ++dx) {
      if (dx == 0 && dy == 0) continue;
      int dist = std::abs(dx) + std::abs(dy);
      offsets.push_back(Offset{static_cast<int8_t>(dx), static_cast<int8_t>(dy),
                               static_cast<uint8_t>(dist)});
    }
  }
  return offsets;
}

const std::vector<Offset>& OffsetsR6() {
  static const std::vector<Offset> offsets = BuildDiamondOffsets(World::kScentIters);
  return offsets;
}

const std::vector<Offset>& OffsetsR10() {
  static const std::vector<Offset> offsets = BuildDiamondOffsets(World::kWaterScentIters);
  return offsets;
}

constexpr int kDecayMaxBase = 60000;
constexpr int kDecayStride = kDecayMaxBase + 1;
constexpr int kDecayMaxDist = World::kWaterScentIters;

const std::vector<uint16_t>& DecayTable() {
  static const std::vector<uint16_t> table = [] {
    std::vector<uint16_t> out;
    out.resize((kDecayMaxDist + 1) * kDecayStride);
    for (int dist = 0; dist <= kDecayMaxDist; ++dist) {
      for (int base = 0; base <= kDecayMaxBase; ++base) {
        uint16_t v = static_cast<uint16_t>(base);
        for (int i = 0; i < dist && v > 0; ++i) {
          v = static_cast<uint16_t>((static_cast<uint32_t>(v) * 19u) / 20u);
        }
        out[dist * kDecayStride + base] = v;
      }
    }
    return out;
  }();
  return table;
}

uint16_t DecayLut(uint16_t base, int dist) {
  if (dist <= 0) return base;
  if (dist > kDecayMaxDist) dist = kDecayMaxDist;
  if (base > kDecayMaxBase) base = kDecayMaxBase;
  return DecayTable()[dist * kDecayStride + base];
}

uint8_t ClampU8(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

inline uint16_t BaseFoodFromTile(const Tile& tile) {
  if (tile.type != TileType::Land || tile.burning) return 0;
  int value = static_cast<int>(tile.food) * 120 + static_cast<int>(tile.trees) * 8;
  if (tile.building == BuildingType::Farm && tile.farmStage >= Settlement::kFarmReadyStage) {
    value += 600;
  }
  return static_cast<uint16_t>(value);
}
}  // namespace

World::World(int width, int height) : width_(width), height_(height) {
  ResizeStorage();
  MarkTerrainDirtyAll();
}

bool World::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

void World::ResizeStorage() {
  chunksX_ = (width_ + kChunkTiles - 1) / kChunkTiles;
  chunksY_ = (height_ + kChunkTiles - 1) / kChunkTiles;
  if (chunksX_ < 0) chunksX_ = 0;
  if (chunksY_ < 0) chunksY_ = 0;
  chunks_.assign(static_cast<size_t>(std::max(0, chunksX_ * chunksY_)), Chunk{});
  homeSourceStampByTile_.assign(
      static_cast<size_t>(std::max<int64_t>(0, static_cast<int64_t>(width_) * height_)), 0u);
  homeSourceGeneration_ = 1;
}

Tile& World::AtUnchecked(int x, int y) {
  int cx = x / kChunkTiles;
  int cy = y / kChunkTiles;
  int idx = cy * chunksX_ + cx;
  Chunk& chunk = chunks_[static_cast<size_t>(idx)];
  int lx = x - cx * kChunkTiles;
  int ly = y - cy * kChunkTiles;
  return chunk.tiles[ly * kChunkTiles + lx];
}

const Tile& World::AtUnchecked(int x, int y) const {
  int cx = x / kChunkTiles;
  int cy = y / kChunkTiles;
  int idx = cy * chunksX_ + cx;
  const Chunk& chunk = chunks_[static_cast<size_t>(idx)];
  int lx = x - cx * kChunkTiles;
  int ly = y - cy * kChunkTiles;
  return chunk.tiles[ly * kChunkTiles + lx];
}

const Tile& World::At(int x, int y) const {
  if (!InBounds(x, y)) {
    static const Tile kDefault{};
    return kDefault;
  }
  return AtUnchecked(x, y);
}

void World::ApplyTotalsDelta(const Tile& before, const Tile& after) {
  totalTrees_ += static_cast<int64_t>(after.trees) - static_cast<int64_t>(before.trees);
  totalFood_ += static_cast<int64_t>(after.food) - static_cast<int64_t>(before.food);
}

void World::UpdateIndicesForTile(int x, int y, const Tile& before, const Tile& after) {
  uint64_t key = PackCoord(x, y);

  if (before.burning != after.burning) {
    if (after.burning) {
      burningTiles_.insert(key);
    } else {
      burningTiles_.erase(key);
    }
  }

  auto beforeHasBuilding = before.building != BuildingType::None;
  auto afterHasBuilding = after.building != BuildingType::None;
  if (beforeHasBuilding != afterHasBuilding) {
    if (afterHasBuilding) {
      buildingTiles_.insert(key);
    } else {
      buildingTiles_.erase(key);
    }
  }

  auto beforeWell = before.building == BuildingType::Well;
  auto afterWell = after.building == BuildingType::Well;
  if (beforeWell != afterWell) {
    if (afterWell) {
      wellTiles_.insert(key);
    } else {
      wellTiles_.erase(key);
      wellRadiusByTile_.erase(key);
    }
    wellRadiusDirty_ = true;
  }

  auto needsFarmGrow = [&](const Tile& t) {
    return t.building == BuildingType::Farm && t.farmStage > 0 &&
           t.farmStage < Settlement::kFarmReadyStage;
  };
  bool beforeGrow = needsFarmGrow(before);
  bool afterGrow = needsFarmGrow(after);
  if (beforeGrow != afterGrow) {
    if (afterGrow) {
      farmGrowTiles_.insert(key);
    } else {
      farmGrowTiles_.erase(key);
    }
  }

  if (before.type != after.type) {
    bool beforeLand = before.type == TileType::Land;
    bool afterLand = after.type == TileType::Land;
    if (beforeLand != afterLand) {
      MarkTerrainDirty(x, y);
    }
    if (before.type == TileType::FreshWater || after.type == TileType::FreshWater) {
      wellRadiusDirty_ = true;
    }
  }
}

bool World::ConsumeBuildingDirty() {
  bool was = buildingDirty_;
  buildingDirty_ = false;
  return was;
}

void World::MarkTerrainDirty(int x, int y) {
  if (!terrainDirty_) {
    terrainDirty_ = true;
    terrainMinX_ = x;
    terrainMaxX_ = x;
    terrainMinY_ = y;
    terrainMaxY_ = y;
    return;
  }
  terrainMinX_ = std::min(terrainMinX_, x);
  terrainMaxX_ = std::max(terrainMaxX_, x);
  terrainMinY_ = std::min(terrainMinY_, y);
  terrainMaxY_ = std::max(terrainMaxY_, y);
}

void World::MarkTerrainDirtyAll() {
  terrainDirty_ = true;
  terrainMinX_ = 0;
  terrainMinY_ = 0;
  terrainMaxX_ = std::max(0, width_ - 1);
  terrainMaxY_ = std::max(0, height_ - 1);
}

bool World::ConsumeTerrainDirty(int& minX, int& minY, int& maxX, int& maxY) {
  if (!terrainDirty_) return false;
  minX = terrainMinX_;
  minY = terrainMinY_;
  maxX = terrainMaxX_;
  maxY = terrainMaxY_;
  terrainDirty_ = false;
  return true;
}

void World::EraseAt(int x, int y) {
  if (!InBounds(x, y)) return;
  EditTile(x, y, [&](Tile& tile) {
    tile = Tile{};
  });
  MarkBuildingDirty();
  MarkTerrainDirty(x, y);
}

int World::TotalTrees() const { return static_cast<int>(std::max<int64_t>(0, totalTrees_)); }
int World::TotalFood() const { return static_cast<int>(std::max<int64_t>(0, totalFood_)); }

uint16_t World::Decay(uint16_t value, int dist) {
  uint16_t out = value;
  for (int i = 0; i < dist && out > 0; ++i) {
    out = static_cast<uint16_t>((static_cast<uint32_t>(out) * 19u) / 20u);
  }
  return out;
}

uint16_t World::BaseFoodAt(int x, int y) const {
  if (static_cast<unsigned>(x) >= static_cast<unsigned>(width_) ||
      static_cast<unsigned>(y) >= static_cast<unsigned>(height_)) {
    return 0;
  }
  return BaseFoodFromTile(AtUnchecked(x, y));
}

uint16_t World::BaseFireAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  const Tile& tile = AtUnchecked(x, y);
  return tile.burning ? 60000u : 0u;
}

uint16_t World::BaseWaterAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  const Tile& tile = AtUnchecked(x, y);
  uint16_t base = (tile.type == TileType::FreshWater) ? 60000u : 0u;
  if (tile.building == BuildingType::Well) {
    uint8_t radius = WellRadiusAt(x, y);
    if (radius > 0) {
      int value = (static_cast<int>(radius) * 60000) / kWellRadiusStrong;
      value = std::max(value, 12000);
      base = std::max<uint16_t>(base, static_cast<uint16_t>(std::min(60000, value)));
    }
  }
  return base;
}

uint16_t World::FoodScentAt(int x, int y) const {
  uint16_t best = 0;
  if (static_cast<unsigned>(x) < static_cast<unsigned>(width_) &&
      static_cast<unsigned>(y) < static_cast<unsigned>(height_)) {
    best = BaseFoodFromTile(AtUnchecked(x, y));
  }
  for (const auto& off : OffsetsR6()) {
    int nx = x + off.dx;
    int ny = y + off.dy;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(width_) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(height_)) {
      continue;
    }
    uint16_t base = BaseFoodFromTile(AtUnchecked(nx, ny));
    uint16_t val = DecayLut(base, off.dist);
    if (val > best) best = val;
  }
  return best;
}

uint16_t World::WaterScentAt(int x, int y) const {
  uint16_t best = BaseWaterAt(x, y);
  for (const auto& off : OffsetsR10()) {
    int nx = x + off.dx;
    int ny = y + off.dy;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(width_) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(height_)) {
      continue;
    }
    uint16_t base = BaseWaterAt(nx, ny);
    uint16_t val = DecayLut(base, off.dist);
    if (val > best) best = val;
  }
  return best;
}

uint16_t World::FireRiskAt(int x, int y) const {
  uint16_t best = BaseFireAt(x, y);
  for (const auto& off : OffsetsR6()) {
    int nx = x + off.dx;
    int ny = y + off.dy;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(width_) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(height_)) {
      continue;
    }
    uint16_t base = BaseFireAt(nx, ny);
    uint16_t val = DecayLut(base, off.dist);
    if (val > best) best = val;
  }
  return best;
}

void World::EnsureHomeSourceGrid() {
  const int64_t needed = static_cast<int64_t>(width_) * static_cast<int64_t>(height_);
  if (needed <= 0) {
    homeSourceStampByTile_.clear();
    homeSourceGeneration_ = 1;
    return;
  }
  if (homeSourceStampByTile_.size() != static_cast<size_t>(needed)) {
    homeSourceStampByTile_.assign(static_cast<size_t>(needed), 0u);
    homeSourceGeneration_ = 1;
  }
}

bool World::IsHomeSourceAt(int x, int y) const {
  if (!InBounds(x, y)) return false;
  const int idx = y * width_ + x;
  return homeSourceStampByTile_[static_cast<size_t>(idx)] == homeSourceGeneration_;
}

uint16_t World::HomeScentAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  uint16_t best = IsHomeSourceAt(x, y) ? 60000u : 0u;
  for (const auto& off : OffsetsR6()) {
    int nx = x + off.dx;
    int ny = y + off.dy;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(width_) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(height_)) {
      continue;
    }
    if (!IsHomeSourceAt(nx, ny)) continue;
    uint16_t val = DecayLut(60000u, off.dist);
    if (val > best) best = val;
  }
  return best;
}

uint8_t World::WellRadiusAt(int x, int y) const {
  if (!InBounds(x, y)) return 0;
  const Tile& tile = AtUnchecked(x, y);
  if (tile.building != BuildingType::Well) return 0;
  EnsureWellRadius();
  uint64_t key = PackCoord(x, y);
  auto it = wellRadiusByTile_.find(key);
  if (it == wellRadiusByTile_.end()) return 0;
  return it->second;
}

void World::EnsureWellRadius() const {
  if (!wellRadiusDirty_) return;
  const_cast<World*>(this)->RecomputeWellRadius();
  wellRadiusDirty_ = false;
}

void World::RecomputeWellRadius() {
  wellRadiusByTile_.clear();
  wellRadiusDirty_ = false;
  if (wellTiles_.empty()) return;

  std::unordered_set<uint64_t> strong;
  std::unordered_set<uint64_t> medium;
  std::unordered_set<uint64_t> weak;

  auto hasFreshWaterWithin = [&](int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
      int y = cy + dy;
      if (y < 0 || y >= height_) continue;
      int rem = radius - std::abs(dy);
      for (int dx = -rem; dx <= rem; ++dx) {
        int x = cx + dx;
        if (x < 0 || x >= width_) continue;
        if (AtUnchecked(x, y).type == TileType::FreshWater) return true;
      }
    }
    return false;
  };

  auto hasWellInSetWithin = [&](int cx, int cy, int radius, const std::unordered_set<uint64_t>& set) {
    for (int dy = -radius; dy <= radius; ++dy) {
      int y = cy + dy;
      if (y < 0 || y >= height_) continue;
      int rem = radius - std::abs(dy);
      for (int dx = -rem; dx <= rem; ++dx) {
        int x = cx + dx;
        if (x < 0 || x >= width_) continue;
        if (set.find(PackCoord(x, y)) != set.end()) return true;
      }
    }
    return false;
  };

  for (uint64_t key : wellTiles_) {
    int x = 0;
    int y = 0;
    UnpackCoord(key, x, y);
    if (hasFreshWaterWithin(x, y, kWellSourceRadius)) {
      strong.insert(key);
      wellRadiusByTile_[key] = static_cast<uint8_t>(kWellRadiusStrong);
    }
  }

  for (uint64_t key : wellTiles_) {
    if (strong.find(key) != strong.end()) continue;
    int x = 0;
    int y = 0;
    UnpackCoord(key, x, y);
    if (hasWellInSetWithin(x, y, kWellRadiusStrong, strong)) {
      medium.insert(key);
      wellRadiusByTile_[key] = static_cast<uint8_t>(kWellRadiusMedium);
    }
  }

  for (uint64_t key : wellTiles_) {
    if (strong.find(key) != strong.end() || medium.find(key) != medium.end()) continue;
    int x = 0;
    int y = 0;
    UnpackCoord(key, x, y);
    if (hasWellInSetWithin(x, y, kWellRadiusMedium, medium)) {
      weak.insert(key);
      wellRadiusByTile_[key] = static_cast<uint8_t>(kWellRadiusWeak);
    }
  }

  for (uint64_t key : wellTiles_) {
    if (strong.find(key) != strong.end() || medium.find(key) != medium.end() ||
        weak.find(key) != weak.end()) {
      continue;
    }
    int x = 0;
    int y = 0;
    UnpackCoord(key, x, y);
    if (hasWellInSetWithin(x, y, kWellRadiusWeak, weak)) {
      wellRadiusByTile_[key] = static_cast<uint8_t>(kWellRadiusTiny);
    }
  }
}

void World::UpdateDaily(Random& rng, int dayDelta) {
  if (dayDelta < 1) dayDelta = 1;
  CrashContextSetStage("World::UpdateDaily");
  RecomputeWellRadius();

  auto updateOneDay = [&]() {
    std::vector<uint64_t> ignite;
    ignite.reserve(128);

    std::vector<uint64_t> burningSnapshot;
    burningSnapshot.reserve(burningTiles_.size());
    for (uint64_t key : burningTiles_) {
      burningSnapshot.push_back(key);
    }

    for (uint64_t key : burningSnapshot) {
      int x = 0;
      int y = 0;
      UnpackCoord(key, x, y);
      if (!InBounds(x, y)) continue;
      const Tile& cur = AtUnchecked(x, y);
      if (!cur.burning) {
        burningTiles_.erase(key);
        continue;
      }

      EditTile(x, y, [&](Tile& tile) {
        if (tile.building != BuildingType::None) {
          tile.building = BuildingType::None;
          tile.buildingOwnerId = -1;
          tile.farmStage = 0;
          MarkBuildingDirty();
        }

        int trees = static_cast<int>(tile.trees);
        if (trees > 0) {
          trees = std::max(0, trees - 2);
          tile.trees = static_cast<uint8_t>(trees);
        }
        int burn = static_cast<int>(tile.burnDaysRemaining);
        burn = std::max(0, burn - 1);
        tile.burnDaysRemaining = static_cast<uint8_t>(burn);

        if (trees <= 0 || burn <= 0) {
          tile.burning = false;
          tile.burnDaysRemaining = 0;
        }
      });

      const Tile& after = AtUnchecked(x, y);
      if (!after.burning) continue;

      const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& d : dirs) {
        int nx = x + d[0];
        int ny = y + d[1];
        if (!InBounds(nx, ny)) continue;
        const Tile& neighbor = AtUnchecked(nx, ny);
        if (neighbor.type != TileType::Land) continue;
        if (neighbor.burning) continue;
        if (neighbor.trees == 0) continue;
        if (rng.Chance(0.12f)) {
          ignite.push_back(PackCoord(nx, ny));
        }
      }
    }

    for (uint64_t key : ignite) {
      int x = 0;
      int y = 0;
      UnpackCoord(key, x, y);
      if (!InBounds(x, y)) continue;
      const Tile& tile = AtUnchecked(x, y);
      if (tile.burning) continue;
      if (tile.type != TileType::Land) continue;
      if (tile.trees == 0) continue;
      SetBurning(x, y, true, kFireDuration);
    }

    std::vector<uint64_t> farmsSnapshot;
    farmsSnapshot.reserve(farmGrowTiles_.size());
    for (uint64_t key : farmGrowTiles_) {
      farmsSnapshot.push_back(key);
    }

    for (uint64_t key : farmsSnapshot) {
      int x = 0;
      int y = 0;
      UnpackCoord(key, x, y);
      if (!InBounds(x, y)) continue;
      const Tile& tile = AtUnchecked(x, y);
      if (tile.building != BuildingType::Farm || tile.farmStage == 0 ||
          tile.farmStage >= Settlement::kFarmReadyStage) {
        farmGrowTiles_.erase(key);
        continue;
      }

      int waterAdj = 0;
      const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& d : dirs) {
        int nx = x + d[0];
        int ny = y + d[1];
        if (!InBounds(nx, ny)) continue;
        if (AtUnchecked(nx, ny).type == TileType::FreshWater) {
          waterAdj++;
        }
      }

      if (waterAdj == 0) {
        bool hasWellWater = false;
        for (int dy = -kWellRadiusStrong; dy <= kWellRadiusStrong && !hasWellWater; ++dy) {
          int wy = y + dy;
          if (wy < 0 || wy >= height_) continue;
          int rem = kWellRadiusStrong - std::abs(dy);
          for (int dx = -rem; dx <= rem; ++dx) {
            int wx = x + dx;
            if (wx < 0 || wx >= width_) continue;
            int dist = std::abs(dx) + std::abs(dy);
            uint8_t radius = WellRadiusAt(wx, wy);
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
        EditTile(x, y, [&](Tile& t) {
          int stage = static_cast<int>(t.farmStage);
          stage = std::min(stage + 1, Settlement::kFarmReadyStage);
          t.farmStage = static_cast<uint8_t>(stage);
        });
      }
    }
  };

  for (int i = 0; i < dayDelta; ++i) {
    if (burningTiles_.empty() && farmGrowTiles_.empty()) break;
    updateOneDay();
  }
}

void World::RecomputeScentFields() {
  // Intentionally a no-op: scent values are computed on-demand locally (bounded by kScentIters).
}

void World::RecomputeHomeField(const SettlementManager& settlements) {
  EnsureHomeSourceGrid();
  if (homeSourceStampByTile_.empty()) return;

  homeSourceGeneration_++;
  if (homeSourceGeneration_ == 0) {
    std::fill(homeSourceStampByTile_.begin(), homeSourceStampByTile_.end(), 0u);
    homeSourceGeneration_ = 1;
  }

  for (const auto& settlement : settlements.Settlements()) {
    if (!InBounds(settlement.centerX, settlement.centerY)) continue;
    const int idx = settlement.centerY * width_ + settlement.centerX;
    homeSourceStampByTile_[static_cast<size_t>(idx)] = homeSourceGeneration_;
  }
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

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const Tile& tile = AtUnchecked(x, y);
      uint8_t type = static_cast<uint8_t>(tile.type);
      uint8_t trees = tile.trees;
      uint8_t food = tile.food;
      out.write(reinterpret_cast<const char*>(&type), sizeof(type));
      out.write(reinterpret_cast<const char*>(&trees), sizeof(trees));
      out.write(reinterpret_cast<const char*>(&food), sizeof(food));
      if (!out.good()) {
        return false;
      }
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

  width_ = static_cast<int>(header.width);
  height_ = static_cast<int>(header.height);
  ResizeStorage();
  burningTiles_.clear();
  buildingTiles_.clear();
  farmGrowTiles_.clear();
  wellTiles_.clear();
  EnsureHomeSourceGrid();
  std::fill(homeSourceStampByTile_.begin(), homeSourceStampByTile_.end(), 0u);
  homeSourceGeneration_ = 1;
  wellRadiusByTile_.clear();
  totalTrees_ = 0;
  totalFood_ = 0;
  buildingDirty_ = true;
  MarkTerrainDirtyAll();

  const size_t total = static_cast<size_t>(header.width) * static_cast<size_t>(header.height);
  for (size_t i = 0; i < total; ++i) {
    uint8_t typeRaw = 0;
    uint8_t trees = 0;
    uint8_t food = 0;
    in.read(reinterpret_cast<char*>(&typeRaw), sizeof(typeRaw));
    in.read(reinterpret_cast<char*>(&trees), sizeof(trees));
    in.read(reinterpret_cast<char*>(&food), sizeof(food));
    if (!in.good()) {
      return false;
    }

    int x = static_cast<int>(i % header.width);
    int y = static_cast<int>(i / header.width);
    Tile tile{};
    if (typeRaw <= static_cast<uint8_t>(TileType::FreshWater)) {
      tile.type = static_cast<TileType>(typeRaw);
    } else {
      tile.type = TileType::Ocean;
    }
    if (tile.type == TileType::Land) {
      tile.trees = trees;
      tile.food = food;
    }

    if (tile.type != TileType::Ocean || tile.trees != 0 || tile.food != 0) {
      Tile& dst = AtUnchecked(x, y);
      dst = tile;
      totalTrees_ += dst.trees;
      totalFood_ += dst.food;
    }
  }

  return true;
}
