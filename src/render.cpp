#include "render.h"

#include <SDL_image.h>

#include <algorithm>

namespace {
constexpr int kTileSize = 32;

SDL_Color ColorForTile(const Tile& tile) {
  if (tile.type == TileType::Ocean) return SDL_Color{18, 40, 92, 255};
  if (tile.type == TileType::FreshWater) return SDL_Color{35, 110, 170, 255};
  return SDL_Color{40, 150, 60, 255};
}
}  // namespace

bool Renderer::Load(SDL_Renderer* renderer, const std::string& humanSpritesPath) {
  humansTexture_ = IMG_LoadTexture(renderer, humanSpritesPath.c_str());
  if (!humansTexture_) {
    SDL_Log("Failed to load humans texture: %s", IMG_GetError());
    return false;
  }
  SDL_SetTextureScaleMode(humansTexture_, SDL_ScaleModeNearest);

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
      SDL_Log("Humans spritesheet size (%dx%d) too small; defaulting to 32x32 sprites", texW, texH);
      spriteWidth_ = 32;
      spriteHeight_ = 32;
    }
  }
  return true;
}

void Renderer::Shutdown() {
  if (humansTexture_) {
    SDL_DestroyTexture(humansTexture_);
    humansTexture_ = nullptr;
  }
}

void Renderer::Render(SDL_Renderer* renderer, const World& world, const HumanManager& humans,
                      const Camera& camera, int windowWidth, int windowHeight) {
  const float tileSize = static_cast<float>(kTileSize);
  const float invZoom = 1.0f / camera.zoom;

  const float worldLeft = camera.x;
  const float worldTop = camera.y;
  const float worldRight = camera.x + static_cast<float>(windowWidth) * invZoom;
  const float worldBottom = camera.y + static_cast<float>(windowHeight) * invZoom;

  int minX = std::max(0, static_cast<int>(worldLeft / tileSize) - 1);
  int minY = std::max(0, static_cast<int>(worldTop / tileSize) - 1);
  int maxX = std::min(world.width() - 1, static_cast<int>(worldRight / tileSize) + 1);
  int maxY = std::min(world.height() - 1, static_cast<int>(worldBottom / tileSize) + 1);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const Tile& tile = world.At(x, y);
      SDL_Color base = ColorForTile(tile);
      SDL_SetRenderDrawColor(renderer, base.r, base.g, base.b, base.a);

      const float worldX = static_cast<float>(x) * tileSize;
      const float worldY = static_cast<float>(y) * tileSize;
      SDL_FRect rect{
          (worldX - camera.x) * camera.zoom,
          (worldY - camera.y) * camera.zoom,
          tileSize * camera.zoom,
          tileSize * camera.zoom,
      };
      SDL_RenderFillRectF(renderer, &rect);

      if (tile.trees > 0) {
        int treeShade = std::min(200, 60 + tile.trees * 6);
        SDL_SetRenderDrawColor(renderer, 20, static_cast<Uint8>(treeShade), 20, 255);
        SDL_FRect treeRect{
            rect.x + rect.w * 0.2f,
            rect.y + rect.h * 0.2f,
            rect.w * 0.6f,
            rect.h * 0.6f,
        };
        SDL_RenderFillRectF(renderer, &treeRect);
      }

      if (tile.food > 0) {
        SDL_SetRenderDrawColor(renderer, 220, 200, 70, 255);
        SDL_FRect foodRect{
            rect.x + rect.w * 0.65f,
            rect.y + rect.h * 0.1f,
            rect.w * 0.2f,
            rect.h * 0.2f,
        };
        SDL_RenderFillRectF(renderer, &foodRect);
      }

      if (tile.burning) {
        SDL_SetRenderDrawColor(renderer, 220, 80, 30, 140);
        SDL_RenderFillRectF(renderer, &rect);
      }
    }
  }

  const auto& list = humans.Humans();
  for (const auto& human : list) {
    if (!human.alive) continue;
    if (human.x < minX || human.x > maxX || human.y < minY || human.y > maxY) continue;

    int row = human.female ? 1 : 0;
    int col = human.animFrame + (human.moving ? 2 : 0);
    SDL_Rect src{col * spriteWidth_, row * spriteHeight_, spriteWidth_, spriteHeight_};

    const float worldX = static_cast<float>(human.x) * tileSize;
    const float worldY = static_cast<float>(human.y) * tileSize;
    SDL_FRect dst{
        (worldX - camera.x) * camera.zoom,
        (worldY - camera.y) * camera.zoom,
        tileSize * camera.zoom,
        tileSize * camera.zoom,
    };

    SDL_RenderCopyF(renderer, humansTexture_, &src, &dst);
  }
}
