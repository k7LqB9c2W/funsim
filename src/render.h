#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "humans.h"
#include "overlays.h"
#include "world.h"

class FactionManager;
class SettlementManager;
struct _TTF_Font;
typedef struct _TTF_Font TTF_Font;

struct Camera {
  float x = 0.0f;
  float y = 0.0f;
  float zoom = 1.0f;
};

struct VillageMarker {
  int x = 0;
  int y = 0;
  int ttlDays = 0;
};

struct RenderOverlayConfig {
  int territoryAlpha = 90;          // Used for FactionTerritory/SettlementInfluence fills.
  float territoryDarken = 0.65f;    // Multiplies faction RGB to reduce saturation.
  bool showWarZones = true;         // Draw conflict glow regardless of overlay mode.
  bool showWarArrows = true;        // Draw arrows between warring settlements.
  bool showTroopCounts = true;      // Draw soldier counts over zones.
  bool showTroopCountsAllZones = false;  // Otherwise only conflict zones.
  bool showSoldierTileMarkers = true;    // Draw green tile highlights under soldiers.
};

class Renderer {
 public:
  Renderer() = default;
  bool Load(SDL_Renderer* renderer, const std::string& humanSpritesPath,
            const std::string& tilesPath, const std::string& terrainOverlayPath,
            const std::string& objectsPath, const std::string& buildingsPath,
            const std::string& labelFontPath, int labelFontSize);
  void Shutdown();
  // Render targets (SDL_TEXTUREACCESS_TARGET) can lose contents on resize/minimize/device reset.
  // Call this on SDL_RENDER_TARGETS_RESET (and optionally SDL_RENDER_DEVICE_RESET).
  void OnRenderTargetsReset();
  void Render(SDL_Renderer* renderer, World& world, const HumanManager& humans,
              const SettlementManager& settlements, const FactionManager& factions,
              const Camera& camera, int windowWidth, int windowHeight,
              const std::vector<VillageMarker>& villageMarkers, int hoverTileX, int hoverTileY,
              bool hoverValid, int brushSize, OverlayMode overlayMode);
  void Render(SDL_Renderer* renderer, World& world, const HumanManager& humans,
              const SettlementManager& settlements, const FactionManager& factions,
              const Camera& camera, int windowWidth, int windowHeight,
              const std::vector<VillageMarker>& villageMarkers, int hoverTileX, int hoverTileY,
              bool hoverValid, int brushSize, OverlayMode overlayMode, const RenderOverlayConfig& config);

 private:
  void DestroyTerrainCache();
  void EnsureTerrainCache(SDL_Renderer* renderer, World& world);
  void RebuildTerrainCache(SDL_Renderer* renderer, const World& world, int minX, int minY, int maxX,
                           int maxY);
  void BuildChunks(SDL_Renderer* renderer, int worldWidth, int worldHeight);
  void ClearLabelCache();
  void UpdateLabelCache(SDL_Renderer* renderer, const SettlementManager& settlements,
                        const FactionManager& factions);

  struct TerrainChunk {
    SDL_Texture* texture = nullptr;
    int originX = 0;
    int originY = 0;
    int tilesWide = 0;
    int tilesHigh = 0;
    bool dirty = true;
    uint64_t lastUsedFrame = 0;
  };

  SDL_Texture* humansTexture_ = nullptr;
  SDL_Texture* tilesTexture_ = nullptr;
  SDL_Texture* terrainOverlayTexture_ = nullptr;
  SDL_Texture* objectsTexture_ = nullptr;
  SDL_Texture* buildingsTexture_ = nullptr;
  SDL_Texture* shadowTexture_ = nullptr;
  SDL_Texture* fireTexture_ = nullptr;
  TTF_Font* labelFont_ = nullptr;
  bool ttfReady_ = false;
  bool ttfOwned_ = false;
  int spriteWidth_ = 32;
  int spriteHeight_ = 32;

  int worldWidth_ = 0;
  int worldHeight_ = 0;
  int chunkTiles_ = 32;
  int chunksX_ = 0;
  int chunksY_ = 0;
  bool terrainDirty_ = true;
  std::vector<TerrainChunk> chunks_;
  std::vector<int> terrainTextureIndices_;
  uint64_t frameCounter_ = 0;
  int maxTerrainTextures_ = 256;

  struct LabelCacheEntry {
    int settlementId = -1;
    std::string text;
    SDL_Color color{255, 255, 255, 255};
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    bool used = false;
  };
  std::vector<LabelCacheEntry> labelCache_;

  struct TextCacheEntry {
    std::string text;
    SDL_Color color{255, 255, 255, 255};
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    uint64_t lastUsedFrame = 0;
  };
  std::vector<TextCacheEntry> textCache_;
};
