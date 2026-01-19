#pragma once

#include <SDL.h>
#include <SDL_image.h>

#include "humans.h"
#include "render.h"
#include "settlements.h"
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
  void StepTick(float tickSeconds);
  void StepDayCoarse();
  void AdvanceMacro(int days);
  void EnterMacroMode();
  void ExitMacroMode();
  void ApplyToolAt(int tileX, int tileY, bool erase);
  bool ScreenToTile(int screenX, int screenY, int& tileX, int& tileY) const;
  void ClampCamera();
  void RefreshTotals();
  void WriteDeathLog() const;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;

  bool running_ = true;

  World world_;
  HumanManager humans_;
  SettlementManager settlements_;
  Renderer rendererAssets_;
  Camera camera_;
  UIState ui_;
  SimStats stats_;
  Random rng_;
  std::vector<VillageMarker> villageMarkers_;

  double accumulator_ = 0.0;
  double daySeconds_ = 5.0;
  int ticksPerDay_ = 50;
  double tickSeconds_ = 0.1;
  int tickCount_ = 0;
  int maxTickStepsPerFrame_ = 200;
  int maxMacroDaysPerFrame_ = 2000;
  bool macroActive_ = false;

  int hoverTileX_ = 0;
  int hoverTileY_ = 0;
  bool hoverValid_ = false;

  bool imguiInitialized_ = false;
  bool worldDirty_ = false;
};
