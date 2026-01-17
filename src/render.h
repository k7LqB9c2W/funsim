#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "humans.h"
#include "world.h"

struct Camera {
  float x = 0.0f;
  float y = 0.0f;
  float zoom = 1.0f;
};

class Renderer {
 public:
  Renderer() = default;
  bool Load(SDL_Renderer* renderer, const std::string& humanSpritesPath,
            const std::string& tilesPath, const std::string& terrainOverlayPath,
            const std::string& objectsPath);
  void Shutdown();
  void Render(SDL_Renderer* renderer, const World& world, const HumanManager& humans,
              const Camera& camera, int windowWidth, int windowHeight);

 private:
  void DestroyTerrainCache();
  void EnsureTerrainCache(SDL_Renderer* renderer, const World& world);
  void RebuildTerrainCache(SDL_Renderer* renderer, const World& world);
  void BuildChunks(SDL_Renderer* renderer, int worldWidth, int worldHeight);

  struct TerrainChunk {
    SDL_Texture* texture = nullptr;
    int originX = 0;
    int originY = 0;
    int tilesWide = 0;
    int tilesHigh = 0;
    bool dirty = true;
  };

  SDL_Texture* humansTexture_ = nullptr;
  SDL_Texture* tilesTexture_ = nullptr;
  SDL_Texture* terrainOverlayTexture_ = nullptr;
  SDL_Texture* objectsTexture_ = nullptr;
  SDL_Texture* shadowTexture_ = nullptr;
  SDL_Texture* fireTexture_ = nullptr;
  int spriteWidth_ = 32;
  int spriteHeight_ = 32;

  int worldWidth_ = 0;
  int worldHeight_ = 0;
  int chunkTiles_ = 32;
  int chunksX_ = 0;
  int chunksY_ = 0;
  bool terrainDirty_ = true;
  std::vector<TerrainChunk> chunks_;
  std::vector<uint8_t> landMask_;
  std::vector<int> waterDistance_;
};
