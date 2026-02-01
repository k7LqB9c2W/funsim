#pragma once

#include <cstdint>

#include "util.h"
#include "world.h"

namespace TreeRules {
// Large tree sprite uses a sparse anchor selection so the 7x8-tile sprite doesn't overlap heavily.
constexpr uint32_t kLargeTreeSeed = 0x1F83D9ABu;

// A tile with trees becomes a "large tree" anchor only if it wins a local-max contest within this radius.
// Larger radius => fewer visible trees and less overlap.
constexpr int kAnchorRadiusTiles = 3;

// Approximate ground/trunk footprint in tiles (blocks building placement inside this area).
// Footprint covers [ax-1..ax+1] x [ay-1..ay].
constexpr int kFootprintHalfWidthTiles = 1;
constexpr int kFootprintUpTiles = 1;

inline uint32_t Priority(int x, int y) {
  return Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kLargeTreeSeed);
}

inline uint64_t CoordKey(int x, int y) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
         static_cast<uint64_t>(static_cast<uint32_t>(y));
}

inline bool IsLargeTreeAnchor(const World& world, int x, int y) {
  if (!world.InBounds(x, y)) return false;
  const Tile& tile = world.At(x, y);
  if (tile.type != TileType::Land) return false;
  if (tile.trees == 0) return false;

  const uint32_t selfP = Priority(x, y);
  const uint64_t selfK = CoordKey(x, y);
  for (int dy = -kAnchorRadiusTiles; dy <= kAnchorRadiusTiles; ++dy) {
    for (int dx = -kAnchorRadiusTiles; dx <= kAnchorRadiusTiles; ++dx) {
      if (dx == 0 && dy == 0) continue;
      int nx = x + dx;
      int ny = y + dy;
      if (!world.InBounds(nx, ny)) continue;
      const Tile& n = world.At(nx, ny);
      if (n.type != TileType::Land) continue;
      if (n.trees == 0) continue;
      const uint32_t p = Priority(nx, ny);
      if (p > selfP) return false;
      if (p == selfP && CoordKey(nx, ny) > selfK) return false;
    }
  }
  return true;
}

inline bool FootprintContains(int anchorX, int anchorY, int tileX, int tileY) {
  if (tileX < anchorX - kFootprintHalfWidthTiles) return false;
  if (tileX > anchorX + kFootprintHalfWidthTiles) return false;
  if (tileY < anchorY - kFootprintUpTiles) return false;
  if (tileY > anchorY) return false;
  return true;
}

inline bool BlocksBuildingPlacement(const World& world, int tileX, int tileY) {
  // Allow building directly on an anchor tile (it clears the tree there), but block any other tile
  // that sits inside a nearby anchor's trunk/ground footprint.
  if (IsLargeTreeAnchor(world, tileX, tileY)) return false;

  // Only anchors in a small neighborhood can cover this tile due to the compact footprint.
  for (int ay = tileY; ay <= tileY + kFootprintUpTiles; ++ay) {
    for (int ax = tileX - kFootprintHalfWidthTiles; ax <= tileX + kFootprintHalfWidthTiles; ++ax) {
      if (!world.InBounds(ax, ay)) continue;
      if (!IsLargeTreeAnchor(world, ax, ay)) continue;
      if (FootprintContains(ax, ay, tileX, tileY)) return true;
    }
  }
  return false;
}
}  // namespace TreeRules

