#pragma once

#include <SDL.h>
#include <string>

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
  bool Load(SDL_Renderer* renderer, const std::string& humanSpritesPath);
  void Shutdown();
 void Render(SDL_Renderer* renderer, const World& world, const HumanManager& humans,
              const Camera& camera, int windowWidth, int windowHeight);

 private:
  SDL_Texture* humansTexture_ = nullptr;
  int spriteWidth_ = 32;
  int spriteHeight_ = 32;
};
