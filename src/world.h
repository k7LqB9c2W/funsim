#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "util.h"

class SettlementManager;

enum class TileType {
  Ocean,
  Land,
  FreshWater,
};

enum class BuildingType : uint8_t {
  None,
  House,
  TownHall,
  Farm,
  Granary,
  Well,
};

struct Tile {
  TileType type = TileType::Ocean;
  uint8_t trees = 0;
  uint8_t food = 0;
  bool burning = false;
  uint8_t burnDaysRemaining = 0;
  BuildingType building = BuildingType::None;
  uint8_t farmStage = 0;
  int buildingOwnerId = -1;
};

class World {
 public:
  static constexpr int kScentIters = 6;
  static constexpr int kWaterScentIters = 10;
  static constexpr int kChunkTiles = 32;

  World(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }

  bool InBounds(int x, int y) const;
  const Tile& At(int x, int y) const;

  template <typename Fn>
  void EditTile(int x, int y, Fn&& fn) {
    if (!InBounds(x, y)) return;
    Tile& tile = AtUnchecked(x, y);
    Tile before = tile;
    fn(tile);
    ApplyTotalsDelta(before, tile);
    UpdateIndicesForTile(x, y, before, tile);
  }

  int TakeFood(int x, int y, int amount) {
    int taken = 0;
    EditTile(x, y, [&](Tile& tile) {
      int have = static_cast<int>(tile.food);
      taken = std::min(amount, have);
      tile.food = static_cast<uint8_t>(have - taken);
    });
    return taken;
  }

  int TakeTrees(int x, int y, int amount) {
    int taken = 0;
    EditTile(x, y, [&](Tile& tile) {
      int have = static_cast<int>(tile.trees);
      taken = std::min(amount, have);
      tile.trees = static_cast<uint8_t>(have - taken);
    });
    return taken;
  }

  void SetTileType(int x, int y, TileType type) {
    EditTile(x, y, [&](Tile& tile) { tile.type = type; });
  }

  void SetBurning(int x, int y, bool burning, int durationDays) {
    EditTile(x, y, [&](Tile& tile) {
      tile.burning = burning;
      tile.burnDaysRemaining =
          burning ? static_cast<uint8_t>(std::max(0, std::min(255, durationDays))) : 0u;
    });
  }

  void ClearBuilding(int x, int y) {
    EditTile(x, y, [&](Tile& tile) {
      tile.building = BuildingType::None;
      tile.buildingOwnerId = -1;
      tile.farmStage = 0;
    });
    MarkBuildingDirty();
  }

  void PlaceBuilding(int x, int y, BuildingType type, int ownerId, uint8_t farmStage) {
    EditTile(x, y, [&](Tile& tile) {
      tile.building = type;
      tile.buildingOwnerId = ownerId;
      tile.farmStage = farmStage;
      tile.trees = 0;
      tile.food = 0;
      tile.burning = false;
      tile.burnDaysRemaining = 0;
    });
    MarkBuildingDirty();
  }

  void UpdateDaily(Random& rng);
  void RecomputeScentFields();
  void RecomputeHomeField(const SettlementManager& settlements);

  void EraseAt(int x, int y);

  int TotalTrees() const;
  int TotalFood() const;

  uint16_t FoodScentAt(int x, int y) const;
  uint16_t WaterScentAt(int x, int y) const;
  uint16_t FireRiskAt(int x, int y) const;
  uint16_t HomeScentAt(int x, int y) const;
  uint8_t WellRadiusAt(int x, int y) const;
  void MarkBuildingDirty() { buildingDirty_ = true; }
  bool ConsumeBuildingDirty();
  void MarkTerrainDirty(int x, int y);
  void MarkTerrainDirtyAll();
  bool ConsumeTerrainDirty(int& minX, int& minY, int& maxX, int& maxY);

  bool SaveMap(const std::string& path) const;
  bool LoadMap(const std::string& path);

  const std::unordered_set<uint64_t>& BuildingTiles() const { return buildingTiles_; }

 private:
  struct Chunk {
    std::array<Tile, kChunkTiles * kChunkTiles> tiles{};
  };

  static uint64_t PackCoord(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(y));
  }
  static void UnpackCoord(uint64_t key, int& x, int& y) {
    x = static_cast<int>(static_cast<uint32_t>(key >> 32));
    y = static_cast<int>(static_cast<uint32_t>(key & 0xffffffffu));
  }

  void ResizeStorage();
  Tile& AtUnchecked(int x, int y);
  const Tile& AtUnchecked(int x, int y) const;

  void RecomputeWellRadius();
  void UpdateIndicesForTile(int x, int y, const Tile& before, const Tile& after);
  void ApplyTotalsDelta(const Tile& before, const Tile& after);
  static uint16_t Decay(uint16_t value, int dist);
  uint16_t BaseFoodAt(int x, int y) const;
  uint16_t BaseWaterAt(int x, int y) const;
  uint16_t BaseFireAt(int x, int y) const;
  void EnsureWellRadius() const;
  void EnsureHomeSourceGrid();
  bool IsHomeSourceAt(int x, int y) const;

  int width_ = 0;
  int height_ = 0;

  int chunksX_ = 0;
  int chunksY_ = 0;
  std::vector<Chunk> chunks_;

  std::unordered_set<uint64_t> burningTiles_;
  std::unordered_set<uint64_t> buildingTiles_;
  std::unordered_set<uint64_t> farmGrowTiles_;
  std::unordered_set<uint64_t> wellTiles_;
  std::vector<uint32_t> homeSourceStampByTile_;
  uint32_t homeSourceGeneration_ = 1;
  mutable std::unordered_map<uint64_t, uint8_t> wellRadiusByTile_;
  mutable bool wellRadiusDirty_ = true;

  int64_t totalTrees_ = 0;
  int64_t totalFood_ = 0;
  bool buildingDirty_ = true;
  bool terrainDirty_ = true;
  int terrainMinX_ = 0;
  int terrainMinY_ = 0;
  int terrainMaxX_ = 0;
  int terrainMaxY_ = 0;
};
