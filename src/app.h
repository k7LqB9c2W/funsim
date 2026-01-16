#pragma once

#include <SDL.h>
#include <SDL_image.h>

#include "humans.h"
#include "render.h"
#include "tools.h"
#include "ui.h"
#include "util.h"
#include "world.h"

class App {
 public:
  App();
  ~App();

  bool Init();
  void Run();

 private:
  void HandleEvents();
  void Update(float dt);
  void RenderFrame();
  void StepDay();
  void ApplyToolAt(int tileX, int tileY, bool erase);
  bool ScreenToTile(int screenX, int screenY, int& tileX, int& tileY) const;
  void ClampCamera();
  void RefreshTotals();

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;

  bool running_ = true;

  World world_;
  HumanManager humans_;
  Renderer rendererAssets_;
  Camera camera_;
  UIState ui_;
  SimStats stats_;
  Random rng_;

  double accumulator_ = 0.0;
  double daySeconds_ = 0.35;

  bool imguiInitialized_ = false;
  bool worldDirty_ = false;
};
