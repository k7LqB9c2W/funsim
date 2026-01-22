#include "render.h"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "factions.h"
#include "settlements.h"

namespace {
constexpr int kTilePx = 32;
constexpr int kTilesAtlasCols = 8;
constexpr int kTilesAtlasRows = 8;
constexpr int kFoamCols = 4;
constexpr int kFoamRows = 4;
constexpr int kObjectCols = 2;
constexpr int kObjectRows = 2;
constexpr int kShadowTexPx = 32;
constexpr int kFireTexPx = 16;

constexpr uint32_t kDeepSeed = 0x3C6EF372u;
constexpr uint32_t kMidSeed = 0x9E3779B9u;
constexpr uint32_t kShallowSeed = 0xBB67AE85u;
constexpr uint32_t kGrassSeed = 0xA54FF53Au;
constexpr uint32_t kSandSeed = 0x510E527Fu;
constexpr uint32_t kTreeSeed = 0x1F83D9ABu;
constexpr uint32_t kFoodSeed = 0x5BE0CD19u;
constexpr uint32_t kFireSeed = 0xC1059ED8u;

struct AtlasCoord {
  int col = 0;
  int row = 0;
};

SDL_Rect TilesRect(AtlasCoord coord) {
  assert(coord.col >= 0 && coord.col < kTilesAtlasCols);
  assert(coord.row >= 0 && coord.row < kTilesAtlasRows);
  return SDL_Rect{coord.col * kTilePx, coord.row * kTilePx, kTilePx, kTilePx};
}

SDL_Rect ObjectRect(AtlasCoord coord) {
  assert(coord.col >= 0 && coord.col < kObjectCols);
  assert(coord.row >= 0 && coord.row < kObjectRows);
  return SDL_Rect{coord.col * kTilePx, coord.row * kTilePx, kTilePx, kTilePx};
}

SDL_Rect FoamRect(uint8_t mask) {
  assert(mask < 16);
  int col = mask % kFoamCols;
  int row = mask / kFoamCols;
  assert(col >= 0 && col < kFoamCols);
  assert(row >= 0 && row < kFoamRows);
  return SDL_Rect{col * kTilePx, row * kTilePx, kTilePx, kTilePx};
}

const std::array<AtlasCoord, 4> kDeepWaterCoords = {
    AtlasCoord{0, 0}, AtlasCoord{1, 0}, AtlasCoord{2, 0}, AtlasCoord{3, 0}};
const std::array<AtlasCoord, 4> kMidWaterCoords = {
    AtlasCoord{4, 0}, AtlasCoord{5, 0}, AtlasCoord{6, 0}, AtlasCoord{7, 0}};
const std::array<AtlasCoord, 4> kShallowWaterCoords = {
    AtlasCoord{0, 1}, AtlasCoord{1, 1}, AtlasCoord{2, 1}, AtlasCoord{3, 1}};
const std::array<AtlasCoord, 8> kGrassCoords = {
    AtlasCoord{0, 2}, AtlasCoord{1, 2}, AtlasCoord{2, 2}, AtlasCoord{3, 2},
    AtlasCoord{0, 7}, AtlasCoord{1, 7}, AtlasCoord{2, 7}, AtlasCoord{3, 7}};
const std::array<AtlasCoord, 8> kSandCoords = {
    AtlasCoord{4, 2}, AtlasCoord{5, 2}, AtlasCoord{6, 2}, AtlasCoord{7, 2},
    AtlasCoord{4, 7}, AtlasCoord{5, 7}, AtlasCoord{6, 7}, AtlasCoord{7, 7}};

const std::array<AtlasCoord, 2> kTreeCoords = {AtlasCoord{0, 0}, AtlasCoord{1, 0}};
const std::array<AtlasCoord, 2> kFoodCoords = {AtlasCoord{0, 1}, AtlasCoord{1, 1}};

SDL_Rect ShadowSrc() { return SDL_Rect{0, 0, kShadowTexPx, kShadowTexPx}; }
SDL_Rect FireSrc() { return SDL_Rect{0, 0, kFireTexPx, kFireTexPx}; }

template <size_t N>
SDL_Rect PickTilesVariant(const std::array<AtlasCoord, N>& coords, uint32_t h) {
  return TilesRect(coords[h % N]);
}

template <size_t N>
SDL_Rect PickObjectVariant(const std::array<AtlasCoord, N>& coords, uint32_t h) {
  return ObjectRect(coords[h % N]);
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t h = x * 0x8DA6B343u;
  h ^= y * 0xD8163841u;
  h ^= seed;
  h ^= (h >> 13);
  h *= 0x85EBCA6Bu;
  h ^= (h >> 16);
  return h;
}

SDL_FRect SnapRect(SDL_FRect rect) {
  rect.x = std::floor(rect.x + 0.5f);
  rect.y = std::floor(rect.y + 0.5f);
  rect.w = std::floor(rect.w + 0.5f);
  rect.h = std::floor(rect.h + 0.5f);
  return rect;
}

SDL_FRect MakeDstRect(float worldX, float worldY, float width, float height, const Camera& camera) {
  SDL_FRect rect{
      (worldX - camera.x) * camera.zoom,
      (worldY - camera.y) * camera.zoom,
      width * camera.zoom,
      height * camera.zoom,
  };
  return SnapRect(rect);
}

SDL_Texture* CreateShadowTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, kShadowTexPx, kShadowTexPx, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create shadow surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  const float cx = (kShadowTexPx - 1) * 0.5f;
  const float cy = (kShadowTexPx - 1) * 0.6f;
  const float rx = kShadowTexPx * 0.4f;
  const float ry = kShadowTexPx * 0.22f;
  const float maxAlpha = 110.0f;

  for (int y = 0; y < kShadowTexPx; ++y) {
    for (int x = 0; x < kShadowTexPx; ++x) {
      float dx = (x - cx) / rx;
      float dy = (y - cy) / ry;
      float dist = dx * dx + dy * dy;
      Uint8 alpha = 0;
      if (dist < 1.0f) {
        float t = 1.0f - dist;
        alpha = static_cast<Uint8>(maxAlpha * t);
      }
      pixels[y * kShadowTexPx + x] = SDL_MapRGBA(fmt, 0, 0, 0, alpha);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create shadow texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  return texture;
}

SDL_Texture* CreateFireTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, kFireTexPx, kFireTexPx, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create fire surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  const float cx = (kFireTexPx - 1) * 0.5f;
  const float cy = (kFireTexPx - 1) * 0.55f;
  const float radius = kFireTexPx * 0.45f;

  for (int y = 0; y < kFireTexPx; ++y) {
    for (int x = 0; x < kFireTexPx; ++x) {
      float dx = x - cx;
      float dy = y - cy;
      float dist = std::sqrt(dx * dx + dy * dy) / radius;
      Uint8 alpha = 0;
      Uint8 r = 0;
      Uint8 g = 0;
      Uint8 b = 0;
      if (dist < 1.0f) {
        float t = 1.0f - dist;
        alpha = static_cast<Uint8>(180.0f * t);
        r = 255;
        g = static_cast<Uint8>(120 + 80 * t);
        b = static_cast<Uint8>(30 + 40 * t);
      }
      pixels[y * kFireTexPx + x] = SDL_MapRGBA(fmt, r, g, b, alpha);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create fire texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  return texture;
}

bool SameColor(const SDL_Color& a, const SDL_Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

float Clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

SDL_Color HeatColor(float t) {
  t = Clamp01(t);
  Uint8 r = static_cast<Uint8>(50 + 205 * t);
  float mid = 1.0f - std::abs(t - 0.5f) * 2.0f;
  Uint8 g = static_cast<Uint8>(60 + 140 * Clamp01(mid));
  Uint8 b = static_cast<Uint8>(200 - 170 * t);
  return SDL_Color{r, g, b, 180};
}

}  // namespace

bool Renderer::Load(SDL_Renderer* renderer, const std::string& humanSpritesPath,
                    const std::string& tilesPath, const std::string& terrainOverlayPath,
                    const std::string& objectsPath, const std::string& buildingsPath,
                    const std::string& labelFontPath, int labelFontSize) {
  Shutdown();

  if (TTF_WasInit() == 0) {
    if (TTF_Init() != 0) {
      SDL_Log("TTF_Init failed: %s", TTF_GetError());
    } else {
      ttfReady_ = true;
      ttfOwned_ = true;
    }
  } else {
    ttfReady_ = true;
  }

  if (ttfReady_) {
    labelFont_ = TTF_OpenFont(labelFontPath.c_str(), labelFontSize);
    if (!labelFont_) {
      SDL_Log("Failed to load label font (%s): %s", labelFontPath.c_str(), TTF_GetError());
    }
  }

  auto loadTexture = [&](const std::string& path, SDL_Texture*& texture, const char* label) {
    texture = IMG_LoadTexture(renderer, path.c_str());
    if (!texture) {
      SDL_Log("Failed to load %s texture (%s): %s", label, path.c_str(), IMG_GetError());
      return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    return true;
  };

  if (!loadTexture(humanSpritesPath, humansTexture_, "humans")) {
    return false;
  }
  if (!loadTexture(tilesPath, tilesTexture_, "tiles")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(terrainOverlayPath, terrainOverlayTexture_, "terrain overlays")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(objectsPath, objectsTexture_, "objects")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(buildingsPath, buildingsTexture_, "buildings")) {
    Shutdown();
    return false;
  }

  SDL_SetTextureBlendMode(humansTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(tilesTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(terrainOverlayTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(objectsTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(buildingsTexture_, SDL_BLENDMODE_BLEND);

  shadowTexture_ = CreateShadowTexture(renderer);
  if (!shadowTexture_) {
    Shutdown();
    return false;
  }
  fireTexture_ = CreateFireTexture(renderer);
  if (!fireTexture_) {
    Shutdown();
    return false;
  }

  auto validateAtlas = [&](SDL_Texture* texture, int expectedW, int expectedH, const char* label) {
    int texW = 0;
    int texH = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &texW, &texH) != 0) {
      SDL_Log("Failed to query %s texture: %s", label, SDL_GetError());
      return;
    }
    if (texW != expectedW || texH != expectedH) {
      SDL_Log("%s atlas size %dx%d does not match expected %dx%d", label, texW, texH, expectedW,
              expectedH);
    }
  };

  validateAtlas(tilesTexture_, kTilesAtlasCols * kTilePx, kTilesAtlasRows * kTilePx, "tiles");
  validateAtlas(terrainOverlayTexture_, kFoamCols * kTilePx, kFoamRows * kTilePx, "terrain overlays");
  validateAtlas(objectsTexture_, kObjectCols * kTilePx, kObjectRows * kTilePx, "objects");
  {
    int texW = 0;
    int texH = 0;
    if (SDL_QueryTexture(buildingsTexture_, nullptr, nullptr, &texW, &texH) == 0) {
      if (texW % kTilePx != 0 || texH % kTilePx != 0) {
        SDL_Log("buildings atlas size %dx%d is not divisible by tile size %d", texW, texH, kTilePx);
      }
    }
  }

  int texW = 0;
  int texH = 0;
  if (SDL_QueryTexture(humansTexture_, nullptr, nullptr, &texW, &texH) == 0) {
    if (texW >= 4 && texH >= 2) {
      spriteWidth_ = texW / 4;
      spriteHeight_ = texH / 2;
      if (texW % 4 != 0 || texH % 2 != 0) {
        SDL_Log("Humans spritesheet size (%dx%d) is not divisible by 4x2; using %dx%d sprites",
                texW, texH, spriteWidth_, spriteHeight_);
      }
    } else {
      SDL_Log("Humans spritesheet size (%dx%d) too small; defaulting to 32x32 sprites", texW,
              texH);
      spriteWidth_ = 32;
      spriteHeight_ = 32;
    }
  }

  terrainDirty_ = true;
  return true;
}

void Renderer::Shutdown() {
  DestroyTerrainCache();
  ClearLabelCache();

  if (labelFont_) {
    TTF_CloseFont(labelFont_);
    labelFont_ = nullptr;
  }
  if (ttfOwned_) {
    TTF_Quit();
    ttfOwned_ = false;
    ttfReady_ = false;
  }

  if (humansTexture_) {
    SDL_DestroyTexture(humansTexture_);
    humansTexture_ = nullptr;
  }
  if (tilesTexture_) {
    SDL_DestroyTexture(tilesTexture_);
    tilesTexture_ = nullptr;
  }
  if (terrainOverlayTexture_) {
    SDL_DestroyTexture(terrainOverlayTexture_);
    terrainOverlayTexture_ = nullptr;
  }
  if (objectsTexture_) {
    SDL_DestroyTexture(objectsTexture_);
    objectsTexture_ = nullptr;
  }
  if (buildingsTexture_) {
    SDL_DestroyTexture(buildingsTexture_);
    buildingsTexture_ = nullptr;
  }
  if (shadowTexture_) {
    SDL_DestroyTexture(shadowTexture_);
    shadowTexture_ = nullptr;
  }
  if (fireTexture_) {
    SDL_DestroyTexture(fireTexture_);
    fireTexture_ = nullptr;
  }
}

void Renderer::DestroyTerrainCache() {
  for (auto& chunk : chunks_) {
    if (chunk.texture) {
      SDL_DestroyTexture(chunk.texture);
      chunk.texture = nullptr;
    }
  }
  chunks_.clear();
  landMask_.clear();
  waterDistance_.clear();
  worldWidth_ = 0;
  worldHeight_ = 0;
  chunksX_ = 0;
  chunksY_ = 0;
  terrainDirty_ = true;
}

void Renderer::ClearLabelCache() {
  for (auto& entry : labelCache_) {
    if (entry.texture) {
      SDL_DestroyTexture(entry.texture);
      entry.texture = nullptr;
    }
  }
  labelCache_.clear();
}

void Renderer::UpdateLabelCache(SDL_Renderer* renderer, const SettlementManager& settlements,
                                const FactionManager& factions) {
  if (!labelFont_) {
    ClearLabelCache();
    return;
  }

  const auto& list = settlements.Settlements();
  std::vector<int> factionCounts(factions.Count() + 1, 0);
  for (const auto& settlement : list) {
    int factionId = settlement.factionId;
    if (factionId > 0 && factionId < static_cast<int>(factionCounts.size())) {
      factionCounts[factionId]++;
    }
  }

  for (auto& entry : labelCache_) {
    entry.used = false;
  }

  for (const auto& settlement : list) {
    std::string label;
    SDL_Color color{255, 255, 255, 255};
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      label = faction->name;
      color.r = faction->color.r;
      color.g = faction->color.g;
      color.b = faction->color.b;
      int factionId = settlement.factionId;
      if (factionId > 0 && factionId < static_cast<int>(factionCounts.size()) &&
          factionCounts[factionId] > 1) {
        label += " #";
        label += std::to_string(settlement.id);
      }
    } else {
      label = "Settlement ";
      label += std::to_string(settlement.id);
    }

    LabelCacheEntry* entry = nullptr;
    for (auto& existing : labelCache_) {
      if (existing.settlementId == settlement.id) {
        entry = &existing;
        break;
      }
    }
    if (!entry) {
      labelCache_.push_back(LabelCacheEntry{});
      entry = &labelCache_.back();
      entry->settlementId = settlement.id;
    }

    entry->used = true;
    if (entry->text == label && SameColor(entry->color, color)) {
      continue;
    }

    if (entry->texture) {
      SDL_DestroyTexture(entry->texture);
      entry->texture = nullptr;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(labelFont_, label.c_str(), color);
    if (!surface) {
      SDL_Log("Failed to render label text: %s", TTF_GetError());
      entry->text = label;
      entry->color = color;
      entry->width = 0;
      entry->height = 0;
      continue;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_Log("Failed to create label texture: %s", SDL_GetError());
      SDL_FreeSurface(surface);
      entry->text = label;
      entry->color = color;
      entry->width = 0;
      entry->height = 0;
      continue;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    entry->texture = texture;
    entry->width = surface->w;
    entry->height = surface->h;
    entry->text = label;
    entry->color = color;
    SDL_FreeSurface(surface);
  }

  for (auto it = labelCache_.begin(); it != labelCache_.end();) {
    if (!it->used) {
      if (it->texture) {
        SDL_DestroyTexture(it->texture);
        it->texture = nullptr;
      }
      it = labelCache_.erase(it);
    } else {
      ++it;
    }
  }
}

void Renderer::BuildChunks(SDL_Renderer* renderer, int worldWidth, int worldHeight) {
  chunksX_ = (worldWidth + chunkTiles_ - 1) / chunkTiles_;
  chunksY_ = (worldHeight + chunkTiles_ - 1) / chunkTiles_;
  chunks_.assign(chunksX_ * chunksY_, TerrainChunk{});

  for (int cy = 0; cy < chunksY_; ++cy) {
    for (int cx = 0; cx < chunksX_; ++cx) {
      TerrainChunk& chunk = chunks_[cy * chunksX_ + cx];
      chunk.originX = cx * chunkTiles_;
      chunk.originY = cy * chunkTiles_;
      chunk.tilesWide = std::min(chunkTiles_, worldWidth - chunk.originX);
      chunk.tilesHigh = std::min(chunkTiles_, worldHeight - chunk.originY);
      chunk.dirty = true;

      int texW = chunk.tilesWide * kTilePx;
      int texH = chunk.tilesHigh * kTilePx;
      chunk.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                        texW, texH);
      if (!chunk.texture) {
        SDL_Log("Failed to create chunk texture: %s", SDL_GetError());
        continue;
      }
      SDL_SetTextureBlendMode(chunk.texture, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(chunk.texture, SDL_ScaleModeNearest);
    }
  }
}

void Renderer::EnsureTerrainCache(SDL_Renderer* renderer, World& world) {
  bool fullRebuild = false;
  if (world.width() != worldWidth_ || world.height() != worldHeight_) {
    DestroyTerrainCache();
    worldWidth_ = world.width();
    worldHeight_ = world.height();
    landMask_.assign(worldWidth_ * worldHeight_, 0);
    BuildChunks(renderer, worldWidth_, worldHeight_);
    fullRebuild = true;
    terrainDirty_ = true;
  }

  int dirtyMinX = 0;
  int dirtyMinY = 0;
  int dirtyMaxX = worldWidth_ > 0 ? worldWidth_ - 1 : 0;
  int dirtyMaxY = worldHeight_ > 0 ? worldHeight_ - 1 : 0;
  bool hasDirty = false;
  if (terrainDirty_ || fullRebuild) {
    terrainDirty_ = false;
    int clearMinX = 0;
    int clearMinY = 0;
    int clearMaxX = 0;
    int clearMaxY = 0;
    world.ConsumeTerrainDirty(clearMinX, clearMinY, clearMaxX, clearMaxY);
    hasDirty = true;
    dirtyMinX = 0;
    dirtyMinY = 0;
    dirtyMaxX = worldWidth_ > 0 ? worldWidth_ - 1 : 0;
    dirtyMaxY = worldHeight_ > 0 ? worldHeight_ - 1 : 0;
  } else {
    hasDirty = world.ConsumeTerrainDirty(dirtyMinX, dirtyMinY, dirtyMaxX, dirtyMaxY);
  }
  if (!hasDirty) return;

  dirtyMinX = std::max(0, dirtyMinX);
  dirtyMinY = std::max(0, dirtyMinY);
  dirtyMaxX = std::min(worldWidth_ - 1, dirtyMaxX);
  dirtyMaxY = std::min(worldHeight_ - 1, dirtyMaxY);

  if (dirtyMinX > dirtyMaxX || dirtyMinY > dirtyMaxY) return;

  if (fullRebuild) {
    for (int y = 0; y < worldHeight_; ++y) {
      for (int x = 0; x < worldWidth_; ++x) {
        int idx = y * worldWidth_ + x;
        landMask_[idx] = (world.At(x, y).type == TileType::Land) ? 1u : 0u;
      }
    }
  } else {
    for (int y = dirtyMinY; y <= dirtyMaxY; ++y) {
      for (int x = dirtyMinX; x <= dirtyMaxX; ++x) {
        int idx = y * worldWidth_ + x;
        landMask_[idx] = (world.At(x, y).type == TileType::Land) ? 1u : 0u;
      }
    }
  }

  constexpr int kTerrainPadding = 6;
  int paddedMinX = std::max(0, dirtyMinX - kTerrainPadding);
  int paddedMinY = std::max(0, dirtyMinY - kTerrainPadding);
  int paddedMaxX = std::min(worldWidth_ - 1, dirtyMaxX + kTerrainPadding);
  int paddedMaxY = std::min(worldHeight_ - 1, dirtyMaxY + kTerrainPadding);
  int minChunkX = paddedMinX / chunkTiles_;
  int maxChunkX = paddedMaxX / chunkTiles_;
  int minChunkY = paddedMinY / chunkTiles_;
  int maxChunkY = paddedMaxY / chunkTiles_;
  for (int cy = minChunkY; cy <= maxChunkY; ++cy) {
    for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
      int idx = cy * chunksX_ + cx;
      if (idx >= 0 && idx < static_cast<int>(chunks_.size())) {
        chunks_[idx].dirty = true;
      }
    }
  }

  RebuildTerrainCache(renderer, world);
}

void Renderer::RebuildTerrainCache(SDL_Renderer* renderer, const World& world) {
  (void)world;
  if (chunks_.empty()) {
    BuildChunks(renderer, worldWidth_, worldHeight_);
  }
  assert(world.width() == worldWidth_);
  assert(world.height() == worldHeight_);

  auto isLand = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= worldWidth_ || y >= worldHeight_) return false;
    return landMask_[y * worldWidth_ + x] != 0u;
  };

  auto coastDistance = [&](int x, int y) -> int {
    constexpr int kMaxLandDist = 5;
    for (int dist = 1; dist <= kMaxLandDist; ++dist) {
      for (int dy = -dist; dy <= dist; ++dy) {
        int yPos = y + dy;
        if (yPos < 0 || yPos >= worldHeight_) continue;
        int dx = dist - std::abs(dy);
        int xLeft = x - dx;
        int xRight = x + dx;
        if (xLeft >= 0 && xLeft < worldWidth_ && isLand(xLeft, yPos)) {
          return dist;
        }
        if (dx != 0 && xRight >= 0 && xRight < worldWidth_ && isLand(xRight, yPos)) {
          return dist;
        }
      }
    }
    return kMaxLandDist + 1;
  };

  SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);

  for (auto& chunk : chunks_) {
    if (!chunk.dirty || !chunk.texture) continue;
    chunk.dirty = false;

    SDL_SetRenderTarget(renderer, chunk.texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    for (int y = chunk.originY; y < chunk.originY + chunk.tilesHigh; ++y) {
      for (int x = chunk.originX; x < chunk.originX + chunk.tilesWide; ++x) {
        int idx = y * worldWidth_ + x;
        if (landMask_[idx] != 0u) continue;

        int distToLand = coastDistance(x, y);
        int coastDist = std::max(0, distToLand - 1);
        uint32_t h = Hash2D(static_cast<uint32_t>(x >> 2), static_cast<uint32_t>(y >> 2),
                            static_cast<uint32_t>((coastDist <= 1)   ? kShallowSeed
                                                 : (coastDist <= 4) ? kMidSeed
                                                                   : kDeepSeed));
        SDL_Rect src;
        if (coastDist <= 1) {
          src = PickTilesVariant(kShallowWaterCoords, h);
        } else if (coastDist <= 4) {
          src = PickTilesVariant(kMidWaterCoords, h);
        } else {
          src = PickTilesVariant(kDeepWaterCoords, h);
        }

        SDL_Rect dst{(x - chunk.originX) * kTilePx, (y - chunk.originY) * kTilePx, kTilePx, kTilePx};
        SDL_RenderCopy(renderer, tilesTexture_, &src, &dst);

        uint8_t mask = 0;
        if (isLand(x, y - 1)) mask |= 1u;
        if (isLand(x + 1, y)) mask |= 2u;
        if (isLand(x, y + 1)) mask |= 4u;
        if (isLand(x - 1, y)) mask |= 8u;

        if (mask != 0u) {
          SDL_Rect foam = FoamRect(mask);
          SDL_RenderCopy(renderer, terrainOverlayTexture_, &foam, &dst);
        }
      }
    }

    for (int y = chunk.originY; y < chunk.originY + chunk.tilesHigh; ++y) {
      for (int x = chunk.originX; x < chunk.originX + chunk.tilesWide; ++x) {
        int idx = y * worldWidth_ + x;
        if (landMask_[idx] == 0u) continue;

        bool beach = !isLand(x, y - 1) || !isLand(x + 1, y) || !isLand(x, y + 1) || !isLand(x - 1, y);
        uint32_t h = Hash2D(static_cast<uint32_t>(x >> 2), static_cast<uint32_t>(y >> 2),
                            beach ? kSandSeed : kGrassSeed);
        SDL_Rect src = beach ? PickTilesVariant(kSandCoords, h) : PickTilesVariant(kGrassCoords, h);
        SDL_Rect dst{(x - chunk.originX) * kTilePx, (y - chunk.originY) * kTilePx, kTilePx, kTilePx};
        SDL_RenderCopy(renderer, tilesTexture_, &src, &dst);
      }
    }
  }

  SDL_SetRenderTarget(renderer, previousTarget);
}

void Renderer::Render(SDL_Renderer* renderer, World& world, const HumanManager& humans,
                      const SettlementManager& settlements, const FactionManager& factions,
                      const Camera& camera, int windowWidth, int windowHeight,
                      const std::vector<VillageMarker>& villageMarkers, int hoverTileX,
                      int hoverTileY, bool hoverValid, int brushSize, OverlayMode overlayMode) {
  const float tileSize = static_cast<float>(kTilePx);
  const float invZoom = 1.0f / camera.zoom;

  const float worldLeft = camera.x;
  const float worldTop = camera.y;
  const float worldRight = camera.x + static_cast<float>(windowWidth) * invZoom;
  const float worldBottom = camera.y + static_cast<float>(windowHeight) * invZoom;

  int minX = std::max(0, static_cast<int>(worldLeft / tileSize) - 1);
  int minY = std::max(0, static_cast<int>(worldTop / tileSize) - 1);
  int maxX = std::min(world.width() - 1, static_cast<int>(worldRight / tileSize) + 1);
  int maxY = std::min(world.height() - 1, static_cast<int>(worldBottom / tileSize) + 1);

  EnsureTerrainCache(renderer, world);

  for (const auto& chunk : chunks_) {
    if (!chunk.texture) continue;
    if (chunk.originX > maxX || chunk.originX + chunk.tilesWide - 1 < minX) continue;
    if (chunk.originY > maxY || chunk.originY + chunk.tilesHigh - 1 < minY) continue;
    float worldX = static_cast<float>(chunk.originX) * tileSize;
    float worldY = static_cast<float>(chunk.originY) * tileSize;
    float width = static_cast<float>(chunk.tilesWide) * tileSize;
    float height = static_cast<float>(chunk.tilesHigh) * tileSize;
    SDL_FRect dst = MakeDstRect(worldX, worldY, width, height, camera);
    SDL_RenderCopyF(renderer, chunk.texture, nullptr, &dst);
  }

  int zoneSize = settlements.ZoneSize();
  int zonesX = settlements.ZonesX();
  int zonesY = settlements.ZonesY();
  if (overlayMode != OverlayMode::None && zoneSize > 0 && zonesX > 0 && zonesY > 0) {
    int minZoneX = std::max(0, minX / zoneSize);
    int minZoneY = std::max(0, minY / zoneSize);
    int maxZoneX = std::min(zonesX - 1, maxX / zoneSize);
    int maxZoneY = std::min(zonesY - 1, maxY / zoneSize);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int maxPop = 1;
    if (overlayMode == OverlayMode::PopulationHeat) {
      for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
        for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
          maxPop = std::max(maxPop, settlements.ZonePopAt(zx, zy));
        }
      }
    }

    for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
      for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
        int ownerId = settlements.ZoneOwnerAt(zx, zy);
        const Settlement* settlement = (ownerId > 0) ? settlements.Get(ownerId) : nullptr;
        const Faction* faction = (settlement && settlement->factionId > 0)
                                     ? factions.Get(settlement->factionId)
                                     : nullptr;

        int tilesWide = std::min(zoneSize, world.width() - zx * zoneSize);
        int tilesHigh = std::min(zoneSize, world.height() - zy * zoneSize);
        float worldX = static_cast<float>(zx * zoneSize) * tileSize;
        float worldY = static_cast<float>(zy * zoneSize) * tileSize;
        float width = static_cast<float>(tilesWide) * tileSize;
        float height = static_cast<float>(tilesHigh) * tileSize;
        SDL_FRect dst = MakeDstRect(worldX, worldY, width, height, camera);

        if (overlayMode == OverlayMode::FactionTerritory) {
          if (!faction) continue;
          SDL_SetRenderDrawColor(renderer, faction->color.r, faction->color.g, faction->color.b, 70);
          SDL_RenderFillRectF(renderer, &dst);
        } else if (overlayMode == OverlayMode::SettlementInfluence) {
          if (!faction || !settlement) continue;
          SDL_SetRenderDrawColor(renderer, faction->color.r, faction->color.g, faction->color.b, 70);
          SDL_RenderFillRectF(renderer, &dst);
        } else if (overlayMode == OverlayMode::PopulationHeat) {
          int pop = settlements.ZonePopAt(zx, zy);
          float t = (maxPop > 0) ? static_cast<float>(pop) / static_cast<float>(maxPop) : 0.0f;
          SDL_Color color = HeatColor(t);
          SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
          SDL_RenderFillRectF(renderer, &dst);
        } else if (overlayMode == OverlayMode::Conflict) {
          int intensity = settlements.ZoneConflictAt(zx, zy);
          if (intensity <= 0) continue;
          Uint8 alpha = static_cast<Uint8>(std::min(200, intensity));
          SDL_SetRenderDrawColor(renderer, 220, 70, 60, alpha);
          SDL_RenderFillRectF(renderer, &dst);
        }

        if (overlayMode == OverlayMode::FactionTerritory ||
            overlayMode == OverlayMode::SettlementInfluence) {
          int rightOwner = (zx + 1 <= maxZoneX) ? settlements.ZoneOwnerAt(zx + 1, zy) : ownerId;
          int downOwner = (zy + 1 <= maxZoneY) ? settlements.ZoneOwnerAt(zx, zy + 1) : ownerId;
          int ownerKey = ownerId;
          int rightKey = rightOwner;
          int downKey = downOwner;
          if (overlayMode == OverlayMode::FactionTerritory) {
            const Settlement* rightSettlement = (rightOwner > 0) ? settlements.Get(rightOwner) : nullptr;
            const Settlement* downSettlement = (downOwner > 0) ? settlements.Get(downOwner) : nullptr;
            ownerKey = (settlement && settlement->factionId > 0) ? settlement->factionId : -1;
            rightKey =
                (rightSettlement && rightSettlement->factionId > 0) ? rightSettlement->factionId : -1;
            downKey =
                (downSettlement && downSettlement->factionId > 0) ? downSettlement->factionId : -1;
          }
          SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
          if (rightKey != ownerKey && zx + 1 <= maxZoneX) {
            float x = worldX + width;
            SDL_FRect line = MakeDstRect(x - 1.0f, worldY, 2.0f, height, camera);
            SDL_RenderFillRectF(renderer, &line);
          }
          if (downKey != ownerKey && zy + 1 <= maxZoneY) {
            float y = worldY + height;
            SDL_FRect line = MakeDstRect(worldX, y - 1.0f, width, 2.0f, camera);
            SDL_RenderFillRectF(renderer, &line);
          }
        }
      }
    }
  }

  if (buildingsTexture_) {
    SDL_Rect buildingSrc{0, 0, kTilePx, kTilePx};
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        const Tile& tile = world.At(x, y);
        if (tile.building == BuildingType::None) continue;

        AtlasCoord coord{0, 0};
        switch (tile.building) {
          case BuildingType::House:
            coord = AtlasCoord{0, 0};
            break;
          case BuildingType::TownHall:
            coord = AtlasCoord{0, 1};
            break;
          case BuildingType::Farm:
            coord = AtlasCoord{0, 2};
            break;
          case BuildingType::Granary:
            coord = AtlasCoord{1, 2};
            break;
          case BuildingType::Well:
            coord = AtlasCoord{1, 1};
            break;
          default:
            coord = AtlasCoord{0, 0};
            break;
        }
        buildingSrc.x = coord.col * kTilePx;
        buildingSrc.y = coord.row * kTilePx;

        const float worldX = static_cast<float>(x) * tileSize;
        const float worldY = static_cast<float>(y) * tileSize;
        SDL_FRect dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
        SDL_RenderCopyF(renderer, buildingsTexture_, &buildingSrc, &dst);
      }
    }
  }

  SDL_Rect shadowSrc = ShadowSrc();
  SDL_SetTextureAlphaMod(shadowTexture_, 90);
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const Tile& tile = world.At(x, y);
      if (tile.type != TileType::Land) continue;
      if (tile.trees <= 0 && tile.food <= 0) continue;

      const float worldX = static_cast<float>(x) * tileSize;
      const float worldY = static_cast<float>(y) * tileSize;

      if (tile.trees > 0) {
        const float shadowW = tileSize * 0.6f;
        const float shadowH = tileSize * 0.25f;
        const float shadowX = worldX + (tileSize - shadowW) * 0.5f + 1.0f;
        const float shadowY = worldY + tileSize - shadowH * 0.6f + 1.0f;
        SDL_FRect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
        SDL_RenderCopyF(renderer, shadowTexture_, &shadowSrc, &shadowDst);

        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kTreeSeed);
        SDL_Rect src = PickObjectVariant(kTreeCoords, h);
        SDL_FRect dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
        SDL_RenderCopyF(renderer, objectsTexture_, &src, &dst);
      }

      if (tile.food > 0) {
        const float shadowW = tileSize * 0.5f;
        const float shadowH = tileSize * 0.2f;
        const float shadowX = worldX + (tileSize - shadowW) * 0.5f + 0.5f;
        const float shadowY = worldY + tileSize - shadowH * 0.6f + 0.5f;
        SDL_FRect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
        SDL_RenderCopyF(renderer, shadowTexture_, &shadowSrc, &shadowDst);

        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kFoodSeed);
        SDL_Rect src = PickObjectVariant(kFoodCoords, h);
        SDL_FRect dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
        SDL_RenderCopyF(renderer, objectsTexture_, &src, &dst);
      }
    }
  }

  SDL_Rect fireSrc = FireSrc();
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const Tile& tile = world.At(x, y);
      if (!tile.burning) continue;

      const float worldX = static_cast<float>(x) * tileSize;
      const float worldY = static_cast<float>(y) * tileSize;
      uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kFireSeed);
      int offsetX = static_cast<int>(h % 3u) - 1;
      int offsetY = static_cast<int>((h >> 8) % 3u) - 1;
      const float fireSize = tileSize * 0.35f;
      const float fireX = worldX + (tileSize - fireSize) * 0.5f + static_cast<float>(offsetX);
      const float fireY = worldY + (tileSize - fireSize) * 0.5f + static_cast<float>(offsetY);
      SDL_FRect dst = MakeDstRect(fireX, fireY, fireSize, fireSize, camera);
      SDL_RenderCopyF(renderer, fireTexture_, &fireSrc, &dst);
    }
  }

  SDL_SetTextureAlphaMod(shadowTexture_, 110);
  SDL_Rect humanSrc{0, 0, spriteWidth_, spriteHeight_};
  const auto& list = humans.Humans();
  for (const auto& human : list) {
    if (!human.alive) continue;
    if (human.x < minX || human.x > maxX || human.y < minY || human.y > maxY) continue;

    int row = human.female ? 1 : 0;
    int col = human.animFrame + (human.moving ? 2 : 0);
    humanSrc.x = col * spriteWidth_;
    humanSrc.y = row * spriteHeight_;

    const float worldX = static_cast<float>(human.x) * tileSize;
    const float worldY = static_cast<float>(human.y) * tileSize;

    const float shadowW = tileSize * 0.55f;
    const float shadowH = tileSize * 0.22f;
    const float shadowX = worldX + (tileSize - shadowW) * 0.5f + 1.0f;
    const float shadowY = worldY + tileSize - shadowH * 0.6f + 1.0f;
    SDL_FRect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
    SDL_RenderCopyF(renderer, shadowTexture_, &shadowSrc, &shadowDst);

    SDL_FRect dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
    SDL_RenderCopyF(renderer, humansTexture_, &humanSrc, &dst);
  }

  if (labelFont_) {
    UpdateLabelCache(renderer, settlements, factions);
    const int padding = 3;
    for (const auto& entry : labelCache_) {
      if (!entry.texture) continue;
      const Settlement* settlement = settlements.Get(entry.settlementId);
      if (!settlement) continue;
      if (settlement->centerX < minX || settlement->centerX > maxX ||
          settlement->centerY < minY || settlement->centerY > maxY) {
        continue;
      }

      float worldX = settlement->centerX * tileSize + tileSize * 0.5f - entry.width * 0.5f;
      float worldY = settlement->centerY * tileSize - entry.height - tileSize * 0.3f;
      float bgX = worldX - padding;
      float bgY = worldY - padding;
      float bgW = entry.width + padding * 2.0f;
      float bgH = entry.height + padding * 2.0f;

      SDL_FRect bgDst = MakeDstRect(bgX, bgY, bgW, bgH, camera);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
      SDL_RenderFillRectF(renderer, &bgDst);

      SDL_FRect dst = MakeDstRect(worldX, worldY, static_cast<float>(entry.width),
                                  static_cast<float>(entry.height), camera);
      SDL_RenderCopyF(renderer, entry.texture, nullptr, &dst);
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const auto& marker : villageMarkers) {
    if (marker.ttlDays <= 0) continue;
    if (marker.x < minX || marker.x > maxX || marker.y < minY || marker.y > maxY) continue;
    float t = static_cast<float>(marker.ttlDays) / 25.0f;
    int alpha = static_cast<int>(50.0f + t * 205.0f);
    if (alpha > 255) alpha = 255;
    SDL_SetRenderDrawColor(renderer, 255, 40, 40, static_cast<Uint8>(alpha));

    const float markerSize = 6.0f;
    float worldX = marker.x * tileSize + tileSize * 0.5f - markerSize * 0.5f;
    float worldY = marker.y * tileSize + tileSize * 0.5f - markerSize * 0.5f;
    SDL_FRect dst = MakeDstRect(worldX, worldY, markerSize, markerSize, camera);
    SDL_RenderFillRectF(renderer, &dst);
  }

  if (hoverValid) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 60, 60, 220);
    const float dotSize = 4.0f;
    float dotX = hoverTileX * tileSize + tileSize * 0.5f - dotSize * 0.5f;
    float dotY = hoverTileY * tileSize + tileSize * 0.5f - dotSize * 0.5f;
    SDL_FRect dotDst = MakeDstRect(dotX, dotY, dotSize, dotSize, camera);
    SDL_RenderFillRectF(renderer, &dotDst);

    int radius = brushSize / 2;
    float brushX = static_cast<float>(hoverTileX - radius) * tileSize;
    float brushY = static_cast<float>(hoverTileY - radius) * tileSize;
    float brushW = static_cast<float>(brushSize) * tileSize;
    float brushH = static_cast<float>(brushSize) * tileSize;
    SDL_FRect brushDst = MakeDstRect(brushX, brushY, brushW, brushH, camera);
    SDL_SetRenderDrawColor(renderer, 255, 90, 90, 140);
    SDL_RenderDrawRectF(renderer, &brushDst);
  }
}
