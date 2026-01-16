#include "world.h"

#include <algorithm>
#include <vector>

namespace {
constexpr int kMaxTrees = 20;
constexpr int kMaxFood = 50;
constexpr int kFireDuration = 4;
}  // namespace

World::World(int width, int height) : width_(width), height_(height), tiles_(width * height) {}

bool World::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width_ && y < height_;
}

Tile& World::At(int x, int y) { return tiles_[y * width_ + x]; }
const Tile& World::At(int x, int y) const { return tiles_[y * width_ + x]; }

bool World::HasAdjacentFreshWater(int x, int y) const {
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const auto& d : dirs) {
    int nx = x + d[0];
    int ny = y + d[1];
    if (!InBounds(nx, ny)) continue;
    if (At(nx, ny).type == TileType::FreshWater) return true;
  }
  return false;
}

void World::UpdateDaily(Random& rng) {
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
        continue;
      }

      if (tile.burning) {
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

      if (tile.trees < kMaxTrees && rng.Chance(0.01f)) {
        tile.trees++;
      }

      if (tile.food < kMaxFood) {
        float chance = 0.02f;
        if (tile.trees > 0) chance += 0.03f;
        if (HasAdjacentFreshWater(x, y)) chance += 0.05f;
        if (rng.Chance(chance)) {
          tile.food++;
        }
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
