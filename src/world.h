#pragma once

#include <cstdint>
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
};

struct Tile {
  TileType type = TileType::Ocean;
  int trees = 0;
  int food = 0;
  bool burning = false;
  int burnDaysRemaining = 0;
  BuildingType building = BuildingType::None;
  uint8_t farmStage = 0;
  int buildingOwnerId = -1;
};

class World {
 public:
  World(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }

  bool InBounds(int x, int y) const;
  Tile& At(int x, int y);
  const Tile& At(int x, int y) const;

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

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<Tile> tiles_;
  std::vector<uint16_t> foodScent_;
  std::vector<uint16_t> waterScent_;
  std::vector<uint16_t> fireRisk_;
  std::vector<uint16_t> homeScent_;
  std::vector<uint16_t> baseFood_;
  std::vector<uint16_t> baseWater_;
  std::vector<uint16_t> baseFire_;
  std::vector<uint16_t> baseHome_;
  std::vector<uint16_t> scentScratch_;
};
