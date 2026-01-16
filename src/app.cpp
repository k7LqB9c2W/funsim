#include "app.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

namespace {
constexpr int kTileSize = 32;
constexpr int kDefaultWidth = 256;
constexpr int kDefaultHeight = 144;

float Clamp(float value, float min_value, float max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}
}  // namespace

App::App() : world_(kDefaultWidth, kDefaultHeight) {}

App::~App() {
  rendererAssets_.Shutdown();

  if (imguiInitialized_) {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }

  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }

  IMG_Quit();
  SDL_Quit();
}

bool App::Init() {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
    SDL_Log("IMG_Init failed: %s", IMG_GetError());
    return false;
  }

  window_ = SDL_CreateWindow("funsim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280,
                             720, SDL_WINDOW_RESIZABLE);
  if (!window_) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, -1,
                                 SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return false;
  }

  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer2_Init(renderer_);
  imguiInitialized_ = true;

  ImGuiIO& io = ImGui::GetIO();
  std::string fontPath = "assets/fonts/Inter-Regular.ttf";
  if (std::filesystem::exists(fontPath)) {
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f);
  }

  if (!rendererAssets_.Load(renderer_, "assets/sprites/humans.png")) {
    return false;
  }

  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);
  camera_.zoom = 1.0f;
  camera_.x = (world_.width() * kTileSize - winW) * 0.5f;
  camera_.y = (world_.height() * kTileSize - winH) * 0.5f;
  if (camera_.x < 0.0f) camera_.x = 0.0f;
  if (camera_.y < 0.0f) camera_.y = 0.0f;

  RefreshTotals();
  return true;
}

void App::Run() {
  Uint64 lastCounter = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

  while (running_) {
    HandleEvents();

    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>((now - lastCounter) / freq);
    lastCounter = now;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    DrawUI(ui_, stats_);

    Update(dt);

    RenderFrame();
  }
}

void App::HandleEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) {
      running_ = false;
    } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
      ClampCamera();
    } else if (event.type == SDL_MOUSEWHEEL) {
      if (ImGui::GetIO().WantCaptureMouse) {
        continue;
      }
      int mouseX = 0;
      int mouseY = 0;
      SDL_GetMouseState(&mouseX, &mouseY);
      float zoomFactor = (event.wheel.y > 0) ? 1.1f : 1.0f / 1.1f;
      float worldX = mouseX / camera_.zoom + camera_.x;
      float worldY = mouseY / camera_.zoom + camera_.y;
      camera_.zoom = Clamp(camera_.zoom * zoomFactor, 0.5f, 4.0f);
      camera_.x = worldX - mouseX / camera_.zoom;
      camera_.y = worldY - mouseY / camera_.zoom;
      ClampCamera();
    }
  }
}

void App::Update(float dt) {
  ImGuiIO& io = ImGui::GetIO();

  if (!io.WantCaptureKeyboard) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float move = 500.0f * dt / camera_.zoom;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) camera_.y -= move;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) camera_.y += move;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) camera_.x -= move;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) camera_.x += move;
    ClampCamera();
  }

  if (!io.WantCaptureMouse) {
    int mouseX = 0;
    int mouseY = 0;
    Uint32 buttons = SDL_GetMouseState(&mouseX, &mouseY);
    bool leftDown = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool rightDown = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    if (leftDown || rightDown) {
      int tileX = 0;
      int tileY = 0;
      if (ScreenToTile(mouseX, mouseY, tileX, tileY)) {
        ApplyToolAt(tileX, tileY, rightDown);
      }
    }
  }

  humans_.UpdateAnimation(dt);

  if (!ui_.paused) {
    double speed = 1.0;
    if (ui_.speedIndex == 1) speed = 5.0;
    if (ui_.speedIndex == 2) speed = 20.0;
    accumulator_ += static_cast<double>(dt) * speed;
    int steps = 0;
    while (accumulator_ >= daySeconds_ && steps < 10) {
      StepDay();
      accumulator_ -= daySeconds_;
      steps++;
    }
  }

  if (ui_.stepDay) {
    StepDay();
  }

  if (worldDirty_) {
    RefreshTotals();
    worldDirty_ = false;
  }
}

void App::RenderFrame() {
  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);

  SDL_SetRenderDrawColor(renderer_, 8, 12, 22, 255);
  SDL_RenderClear(renderer_);

  rendererAssets_.Render(renderer_, world_, humans_, camera_, winW, winH);

  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);

  SDL_RenderPresent(renderer_);
}

void App::StepDay() {
  stats_.dayCount++;
  world_.UpdateDaily(rng_);
  humans_.UpdateDaily(world_, rng_, stats_.birthsToday, stats_.deathsToday);
  RefreshTotals();
}

void App::ApplyToolAt(int tileX, int tileY, bool erase) {
  int radius = ui_.brushSize / 2;
  bool modified = false;
  bool spawned = false;

  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int x = tileX + dx;
      int y = tileY + dy;
      if (!world_.InBounds(x, y)) continue;

      if (erase) {
        world_.EraseAt(x, y);
        modified = true;
        continue;
      }

      Tile& tile = world_.At(x, y);
      switch (ui_.tool) {
        case ToolType::PlaceLand:
          tile.type = TileType::Land;
          modified = true;
          break;
        case ToolType::PlaceFreshWater:
          tile.type = TileType::FreshWater;
          tile.trees = 0;
          tile.food = 0;
          tile.burning = false;
          tile.burnDaysRemaining = 0;
          modified = true;
          break;
        case ToolType::AddTrees:
          if (tile.type == TileType::Land) {
            tile.trees = std::min(20, tile.trees + 5);
            modified = true;
          }
          break;
        case ToolType::AddFood:
          if (tile.type == TileType::Land) {
            tile.food = std::min(50, tile.food + 10);
            modified = true;
          }
          break;
        case ToolType::SpawnMale:
          if (tile.type != TileType::Ocean) {
            humans_.Spawn(x, y, false);
            spawned = true;
          }
          break;
        case ToolType::SpawnFemale:
          if (tile.type != TileType::Ocean) {
            humans_.Spawn(x, y, true);
            spawned = true;
          }
          break;
        case ToolType::Fire:
          if (tile.type == TileType::Land && tile.trees > 0) {
            tile.burning = true;
            tile.burnDaysRemaining = 4;
            modified = true;
          }
          break;
        case ToolType::Meteor:
          tile.type = TileType::Ocean;
          tile.trees = 0;
          tile.food = 0;
          tile.burning = false;
          tile.burnDaysRemaining = 0;
          modified = true;
          break;
        case ToolType::GiftFood:
          if (tile.type == TileType::Land) {
            tile.food = 50;
            modified = true;
          }
          break;
      }
    }
  }

  if (spawned) {
    stats_.totalPop = humans_.CountAlive();
  }
  if (modified) {
    worldDirty_ = true;
  }
}

bool App::ScreenToTile(int screenX, int screenY, int& tileX, int& tileY) const {
  float worldX = screenX / camera_.zoom + camera_.x;
  float worldY = screenY / camera_.zoom + camera_.y;
  tileX = static_cast<int>(worldX) / kTileSize;
  tileY = static_cast<int>(worldY) / kTileSize;
  return world_.InBounds(tileX, tileY);
}

void App::ClampCamera() {
  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);

  float worldW = world_.width() * static_cast<float>(kTileSize);
  float worldH = world_.height() * static_cast<float>(kTileSize);
  float viewW = winW / camera_.zoom;
  float viewH = winH / camera_.zoom;

  float maxX = std::max(0.0f, worldW - viewW);
  float maxY = std::max(0.0f, worldH - viewH);

  camera_.x = Clamp(camera_.x, 0.0f, maxX);
  camera_.y = Clamp(camera_.y, 0.0f, maxY);
}

void App::RefreshTotals() {
  stats_.totalFood = world_.TotalFood();
  stats_.totalTrees = world_.TotalTrees();
  stats_.totalPop = humans_.CountAlive();
}
