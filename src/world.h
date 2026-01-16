#pragma once

#include <vector>

#include "util.h"

enum class TileType {
  Ocean,
  Land,
  FreshWater,
};

struct Tile {
  TileType type = TileType::Ocean;
  int trees = 0;
  int food = 0;
  bool burning = false;
  int burnDaysRemaining = 0;
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

  void EraseAt(int x, int y);

  int TotalTrees() const;
  int TotalFood() const;

 private:
  bool HasAdjacentFreshWater(int x, int y) const;

  int width_ = 0;
  int height_ = 0;
  std::vector<Tile> tiles_;
};
