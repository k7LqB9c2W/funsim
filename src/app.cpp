#include "app.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <system_error>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

namespace {
constexpr int kTileSize = 32;
constexpr int kDefaultWidth = 256;
constexpr int kDefaultHeight = 144;
constexpr int kCalendarDaysPerCoarseDay = 30;

float Clamp(float value, float min_value, float max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}
}  // namespace

App::App() : world_(kDefaultWidth, kDefaultHeight) {
  tickSeconds_ = daySeconds_ / static_cast<double>(ticksPerDay_);
}

App::~App() {
  WriteDeathLog();
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
  CrashContextSetStage("App::Init");
  CrashContextSetWorld(world_.width(), world_.height());
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

  if (!rendererAssets_.Load(renderer_, "assets/sprites/humans.png", "assets/sprites/tiles.png",
                            "assets/sprites/terrain_tiles.png",
                            "assets/sprites/object_tiles.png",
                            "assets/sprites/buildings_tiles.png",
                            "assets/fonts/Inter-Regular.ttc", 14)) {
    return false;
  }

  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);
  camera_.zoom = 1.0f;
  camera_.x = (world_.width() * kTileSize - winW) * 0.5f;
  camera_.y = (world_.height() * kTileSize - winH) * 0.5f;
  ClampCamera();

  RefreshTotals();
  CrashContextSetStage("App::Init done");
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

    DrawUI(ui_, stats_, factions_, settlements_, humans_, hoverInfo_);

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
#if SDL_VERSION_ATLEAST(2, 0, 2)
    } else if (event.type == SDL_RENDER_TARGETS_RESET) {
      rendererAssets_.OnRenderTargetsReset();
    } else if (event.type == SDL_RENDER_DEVICE_RESET) {
      rendererAssets_.OnRenderTargetsReset();
#endif
    } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
      if (ui_.wholeMapView) {
        FitCameraToWorld();
      } else {
        ClampCamera();
      }
    } else if (event.type == SDL_MOUSEWHEEL) {
      if (ImGui::GetIO().WantCaptureMouse || ui_.wholeMapView) {
        continue;
      }
      int mouseX = 0;
      int mouseY = 0;
      SDL_GetMouseState(&mouseX, &mouseY);
      int zoomStep = static_cast<int>(std::floor(camera_.zoom + 0.5f));
      if (event.wheel.y > 0) {
        zoomStep = std::min(4, zoomStep + 1);
      } else if (event.wheel.y < 0) {
        zoomStep = std::max(1, zoomStep - 1);
      }
      float newZoom = static_cast<float>(zoomStep);
      float worldX = mouseX / camera_.zoom + camera_.x;
      float worldY = mouseY / camera_.zoom + camera_.y;
      camera_.zoom = newZoom;
      camera_.x = worldX - mouseX / camera_.zoom;
      camera_.y = worldY - mouseY / camera_.zoom;
      ClampCamera();
    }
  }
}

void App::Update(float dt) {
  ImGuiIO& io = ImGui::GetIO();

  if (ui_.saveMap) {
    if (!SaveMap(ui_.mapPath)) {
      SDL_Log("Failed to save map: %s", ui_.mapPath);
    }
  }
  if (ui_.loadMap) {
    if (!LoadMap(ui_.mapPath)) {
      SDL_Log("Failed to load map: %s", ui_.mapPath);
    }
  }
  if (ui_.newWorld) {
    int scale = (ui_.worldSizeIndex == 1) ? 4 : 1;
    CreateNewWorld(scale);
  }

  UpdateWholeMapView();
  factions_.SetWarEnabled(ui_.warEnabled);
  settlements_.SetRebellionsEnabled(ui_.rebellionsEnabled);
  humans_.SetAllowStarvationDeath(ui_.starvationDeathEnabled);
  if (ui_.requestArmyOrdersRefresh && !macroActive_) {
    settlements_.UpdateArmyOrders(world_, humans_, rng_, stats_.dayCount, 1, factions_);
  }

  if (!io.WantCaptureKeyboard && !ui_.wholeMapView) {
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
    hoverValid_ = ScreenToTile(mouseX, mouseY, hoverTileX_, hoverTileY_);
    bool leftDown = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool rightDown = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    bool leftPressed = leftDown && !prevLeftDown_;
    bool rightPressed = rightDown && !prevRightDown_;
    if (leftDown || rightDown) {
      int tileX = 0;
      int tileY = 0;
      if (ScreenToTile(mouseX, mouseY, tileX, tileY)) {
        if (ui_.tool == ToolType::SelectKingdom) {
          if (leftPressed) {
            int ownerId = settlements_.ZoneOwnerForTile(tileX, tileY);
            const Settlement* settlement = (ownerId > 0) ? settlements_.Get(ownerId) : nullptr;
            int factionId = (settlement && settlement->factionId > 0) ? settlement->factionId : -1;
            ui_.selectedFactionId = factionId;
            ui_.factionEditorOpen = (factionId > 0);
          }
        } else {
          ApplyToolAt(tileX, tileY, rightDown);
        }
      }
    }
    prevLeftDown_ = leftDown;
    prevRightDown_ = rightDown;
  } else {
    hoverValid_ = false;
    prevLeftDown_ = false;
    prevRightDown_ = false;
  }

  hoverInfo_.valid = hoverValid_;
  hoverInfo_.tileX = hoverTileX_;
  hoverInfo_.tileY = hoverTileY_;
  hoverInfo_.settlementId = -1;
  hoverInfo_.factionId = -1;
  if (hoverValid_) {
    int ownerId = settlements_.ZoneOwnerForTile(hoverTileX_, hoverTileY_);
    hoverInfo_.settlementId = ownerId;
    if (ownerId > 0) {
      const Settlement* settlement = settlements_.Get(ownerId);
      if (settlement) {
        hoverInfo_.factionId = settlement->factionId;
      }
    }
  }

  humans_.UpdateAnimation(dt);

  const bool wantsMacro = (ui_.speedIndex == 4);
  if (wantsMacro && !macroActive_) {
    EnterMacroMode();
  } else if (!wantsMacro && macroActive_) {
    ExitMacroMode();
  }

  if (!ui_.paused) {
    double speed = 1.0;
    if (ui_.speedIndex == 1) speed = 5.0;
    if (ui_.speedIndex == 2) speed = 20.0;
    if (ui_.speedIndex == 3) speed = 200.0;
    if (ui_.speedIndex == 4) speed = 2000.0;
    accumulator_ += static_cast<double>(dt) * speed;
    if (wantsMacro) {
      int daysToAdvance = static_cast<int>(accumulator_ / daySeconds_);
      if (daysToAdvance > 0) {
        if (daysToAdvance > maxMacroDaysPerFrame_) daysToAdvance = maxMacroDaysPerFrame_;
        accumulator_ -= static_cast<double>(daysToAdvance) * daySeconds_;
        AdvanceMacro(daysToAdvance);
      }
    } else {
      int steps = 0;
      while (accumulator_ >= tickSeconds_ && steps < maxTickStepsPerFrame_) {
        StepTick(static_cast<float>(tickSeconds_));
        accumulator_ -= tickSeconds_;
        tickCount_++;
        if ((tickCount_ % ticksPerDay_) == 0) {
          StepDayCoarse(kCalendarDaysPerCoarseDay);
        }
        steps++;
      }
    }
  }

  if (ui_.stepDay) {
    if (wantsMacro) {
      AdvanceMacro(1);
    } else {
      tickCount_ += ticksPerDay_;
      StepDayCoarse(1);
    }
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

  RenderOverlayConfig overlayConfig;
  overlayConfig.territoryAlpha = ui_.territoryOverlayAlpha;
  overlayConfig.territoryDarken = ui_.territoryOverlayDarken;
  overlayConfig.showWarZones = ui_.showWarZones;
  overlayConfig.showWarArrows = ui_.showWarArrows;
  overlayConfig.showTroopCounts = ui_.showTroopCounts;
  overlayConfig.showTroopCountsAllZones = ui_.showTroopCountsAllZones;
  overlayConfig.showSoldierTileMarkers = ui_.showSoldierTileMarkers;
  rendererAssets_.Render(renderer_, world_, humans_, settlements_, factions_, camera_, winW, winH,
                         villageMarkers_, hoverTileX_, hoverTileY_, hoverValid_, ui_.brushSize,
                         ui_.overlayMode, overlayConfig);

  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);

  SDL_RenderPresent(renderer_);
}

void App::StepTick(float tickSeconds) {
  CrashContextSetStage("StepTick:Humans");
  humans_.UpdateTick(world_, settlements_, rng_, tickCount_, tickSeconds, ticksPerDay_);
}

void App::StepDayCoarse(int dayDelta) {
  if (dayDelta < 1) dayDelta = 1;
  stats_.birthsToday = 0;
  stats_.deathsToday = 0;
  stats_.dayCount += dayDelta;
  CrashContextSetDay(stats_.dayCount);
  std::vector<int> warsBefore;
  for (const auto& war : factions_.Wars()) {
    if (war.active) warsBefore.push_back(war.id);
  }
  CrashContextSetStage("StepDay:World");
  world_.UpdateDaily(rng_, dayDelta);
  CrashContextSetStage("StepDay:Settlements");
  settlements_.UpdateDaily(world_, humans_, rng_, stats_.dayCount, dayDelta, villageMarkers_, factions_);
  int warDeathsToday = settlements_.ConsumeWarDeaths();
  CrashContextSetStage("StepDay:Humans");
  humans_.UpdateDailyCoarse(world_, settlements_, rng_, stats_.dayCount, dayDelta, stats_.birthsToday,
                            stats_.deathsToday);
  stats_.deathsToday += warDeathsToday;
  stats_.totalBirths += stats_.birthsToday;
  stats_.totalDeaths += stats_.deathsToday;
  for (auto& marker : villageMarkers_) {
    if (marker.ttlDays > 0) {
      marker.ttlDays = std::max(0, marker.ttlDays - dayDelta);
    }
  }
  villageMarkers_.erase(
      std::remove_if(villageMarkers_.begin(), villageMarkers_.end(),
                     [](const VillageMarker& marker) { return marker.ttlDays <= 0; }),
      villageMarkers_.end());
  factions_.UpdateStats(settlements_);
  if (!macroActive_) {
    factions_.UpdateLeaders(settlements_, humans_);
  }
  factions_.UpdateDiplomacy(settlements_, rng_, stats_.dayCount);
  std::vector<int> warsStarted;
  for (const auto& war : factions_.Wars()) {
    if (!war.active) continue;
    if (std::find(warsBefore.begin(), warsBefore.end(), war.id) != warsBefore.end()) continue;
    warsStarted.push_back(war.id);
  }
  if (!macroActive_ && !warsStarted.empty()) {
    settlements_.MobilizeForWarStart(humans_, rng_, factions_, warsStarted);
  }
  settlements_.UpdateArmyOrders(world_, humans_, rng_, stats_.dayCount, dayDelta, factions_);
  if (ui_.warLoggingEnabled) {
    AppendWarLog(dayDelta);
    AppendWarEvents(dayDelta);
  }
  RefreshTotals();
  CrashContextSetStage("StepDay:Done");
}

namespace {
bool FileNeedsHeader(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return true;
  auto size = std::filesystem::file_size(path, ec);
  return ec ? true : (size == 0);
}

void CsvEscape(std::ostream& out, const std::string& value) {
  bool needQuotes = false;
  for (char c : value) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needQuotes = true;
      break;
    }
  }
  if (!needQuotes) {
    out << value;
    return;
  }
  out << '"';
  for (char c : value) {
    if (c == '"') out << "\"\"";
    else out << c;
  }
  out << '"';
}

struct WarLogCounts {
  int roles[7] = {};
  int armyStates[6] = {};
  int soldiers = 0;
  int soldiersInEnemyTerritory = 0;
  int soldiersInTargetTerritory = 0;
};
}  // namespace

void App::AppendWarLog(int dayDelta) {
  const std::string path = "war_debug.csv";
  std::ofstream out(path, std::ios::app);
  if (!out.is_open()) return;
  if (FileNeedsHeader(path)) {
    out << "day,dayDelta,speedIndex,wantsMacro,settlementId,factionId,warId,isAttacker,targetSettlementId,"
           "population,stockFood,stockWood,stability,unrest,borderPressure,warPressure,captureProgress,"
           "captureLeaderFactionId,foodEmergency,soldiersPreEmergency,warSoldierFloor,targetFarmers,targetGatherers,"
           "targetBuilders,targetGuards,targetSoldiers,targetScouts,targetIdle,roleIdle,roleGatherer,roleFarmer,"
           "roleBuilder,roleGuard,roleSoldier,roleScout,armyIdle,armyRally,armyMarch,armySiege,armyDefend,armyRetreat,"
           "soldiersTracked,soldiersInEnemyTerritory,soldiersInTargetTerritory\n";
  }

  std::unordered_map<int, WarLogCounts> countsBySettlement;
  for (const auto& human : humans_.Humans()) {
    if (!human.alive) continue;
    if (human.settlementId <= 0) continue;
    WarLogCounts& counts = countsBySettlement[human.settlementId];
    int roleIndex = static_cast<int>(human.role);
    if (roleIndex >= 0 && roleIndex < 7) counts.roles[roleIndex]++;
    if (human.role != Role::Soldier) continue;
    counts.soldiers++;
    int stateIndex = static_cast<int>(human.armyState);
    if (stateIndex >= 0 && stateIndex < 6) counts.armyStates[stateIndex]++;

    const Settlement* home = settlements_.Get(human.settlementId);
    if (!home || home->factionId <= 0) continue;
    int ownerSettlementId = settlements_.ZoneOwnerForTile(human.x, human.y);
    if (ownerSettlementId <= 0 || ownerSettlementId == home->id) continue;
    const Settlement* owner = settlements_.Get(ownerSettlementId);
    if (!owner || owner->factionId <= 0) continue;
    if (!factions_.IsAtWar(home->factionId, owner->factionId)) continue;
    counts.soldiersInEnemyTerritory++;
    if (ownerSettlementId == home->warTargetSettlementId) {
      counts.soldiersInTargetTerritory++;
    }
  }

  const bool wantsMacro = (ui_.speedIndex == 4);
  const int filterSettlementId = ui_.warLogOnlySelected ? ui_.warDebugSettlementId : -1;
  const int filterFactionId = ui_.warLogOnlySelected ? ui_.warDebugFactionId : -1;

  for (const auto& settlement : settlements_.Settlements()) {
    if (settlement.id <= 0) continue;
    if (filterSettlementId > 0 && settlement.id != filterSettlementId) continue;
    if (filterFactionId > 0 && settlement.factionId != filterFactionId) continue;

    int warId = (settlement.factionId > 0) ? factions_.ActiveWarIdForFaction(settlement.factionId) : -1;
    bool isAttacker = (warId > 0 && factions_.WarIsAttacker(warId, settlement.factionId));

    WarLogCounts empty{};
    const WarLogCounts& counts =
        (countsBySettlement.find(settlement.id) != countsBySettlement.end())
            ? countsBySettlement[settlement.id]
            : empty;

    out << stats_.dayCount << ',' << dayDelta << ',' << ui_.speedIndex << ',' << (wantsMacro ? 1 : 0) << ','
        << settlement.id << ',' << settlement.factionId << ',' << warId << ',' << (isAttacker ? 1 : 0) << ','
        << settlement.warTargetSettlementId << ',' << settlement.population << ',' << settlement.stockFood << ','
        << settlement.stockWood << ',' << settlement.stability << ',' << settlement.unrest << ','
        << settlement.borderPressure << ',' << settlement.warPressure << ',' << std::fixed << std::setprecision(2)
        << settlement.captureProgress << ',' << settlement.captureLeaderFactionId << ','
        << (settlement.debugFoodEmergency ? 1 : 0) << ',' << settlement.debugSoldiersPreEmergency << ','
        << settlement.debugWarSoldierFloor << ',' << settlement.debugTargetFarmers << ','
        << settlement.debugTargetGatherers << ',' << settlement.debugTargetBuilders << ','
        << settlement.debugTargetGuards << ',' << settlement.debugTargetSoldiers << ','
        << settlement.debugTargetScouts << ',' << settlement.debugTargetIdle << ','
        << counts.roles[0] << ',' << counts.roles[1] << ',' << counts.roles[2] << ',' << counts.roles[3] << ','
        << counts.roles[4] << ',' << counts.roles[5] << ',' << counts.roles[6] << ',' << counts.armyStates[0] << ','
        << counts.armyStates[1] << ',' << counts.armyStates[2] << ',' << counts.armyStates[3] << ','
        << counts.armyStates[4] << ',' << counts.armyStates[5] << ',' << counts.soldiers << ','
        << counts.soldiersInEnemyTerritory << ',' << counts.soldiersInTargetTerritory << '\n';
  }
}

void App::AppendWarEvents(int dayDelta) {
  (void)dayDelta;
  const std::string path = "war_events.csv";
  std::ofstream out(path, std::ios::app);
  if (!out.is_open()) return;
  if (FileNeedsHeader(path)) {
    out << "day,event,warId,settlementId,factionId,detail\n";
  }

  std::unordered_set<int> activeWarsNow;
  for (const auto& war : factions_.Wars()) {
    if (!war.active) continue;
    activeWarsNow.insert(war.id);
    if (prevActiveWarIds_.find(war.id) == prevActiveWarIds_.end()) {
      out << stats_.dayCount << ",war_started," << war.id << ",,," << "decl=" << war.declaringFactionId
          << " def=" << war.defendingFactionId << '\n';
    }
  }
  for (int warId : prevActiveWarIds_) {
    if (activeWarsNow.find(warId) == activeWarsNow.end()) {
      out << stats_.dayCount << ",war_ended," << warId << ",,," << "" << '\n';
    }
  }
  prevActiveWarIds_ = std::move(activeWarsNow);

  for (const auto& settlement : settlements_.Settlements()) {
    if (settlement.id <= 0) continue;
    int prevTarget = prevSettlementWarTarget_.count(settlement.id) ? prevSettlementWarTarget_[settlement.id] : 0;
    int prevWarId = prevSettlementWarId_.count(settlement.id) ? prevSettlementWarId_[settlement.id] : 0;
    int prevGeneral = prevSettlementGeneralId_.count(settlement.id) ? prevSettlementGeneralId_[settlement.id] : 0;
    int prevTracked = prevSettlementSoldiersTracked_.count(settlement.id)
                          ? prevSettlementSoldiersTracked_[settlement.id]
                          : 0;

    if (prevWarId != settlement.warId) {
      out << stats_.dayCount << ",settlement_war_changed," << settlement.warId << ',' << settlement.id << ','
          << settlement.factionId << ",from=" << prevWarId << '\n';
    }
    if (prevTarget != settlement.warTargetSettlementId) {
      out << stats_.dayCount << ",settlement_target_changed," << settlement.warId << ',' << settlement.id << ','
          << settlement.factionId << ",from=" << prevTarget << " to=" << settlement.warTargetSettlementId << '\n';
    }
    if (prevGeneral != settlement.generalHumanId) {
      out << stats_.dayCount << ",settlement_general_changed," << settlement.warId << ',' << settlement.id << ','
          << settlement.factionId << ",from=" << prevGeneral << " to=" << settlement.generalHumanId << '\n';
    }

    int trackedSoldiers = settlement.debugTargetSoldiers;
    if (trackedSoldiers == 0 && prevTracked != 0 && settlement.warId > 0) {
      out << stats_.dayCount << ",soldiers_dropped_to_zero," << settlement.warId << ',' << settlement.id << ','
          << settlement.factionId << ",foodEmergency=" << (settlement.debugFoodEmergency ? 1 : 0) << '\n';
    }
    prevSettlementWarTarget_[settlement.id] = settlement.warTargetSettlementId;
    prevSettlementWarId_[settlement.id] = settlement.warId;
    prevSettlementGeneralId_[settlement.id] = settlement.generalHumanId;
    prevSettlementSoldiersTracked_[settlement.id] = trackedSoldiers;
  }
}

void App::AdvanceMacro(int days) {
  if (days <= 0) return;
  stats_.birthsToday = 0;
  stats_.deathsToday = 0;
  int warDeathsTotal = 0;
  for (int i = 0; i < days; ++i) {
    stats_.dayCount++;
    CrashContextSetDay(stats_.dayCount);
    if ((stats_.dayCount % 7) == 0) {
      world_.UpdateDaily(rng_, 1);
    }
    humans_.AdvanceMacro(world_, settlements_, rng_, 1, stats_.birthsToday, stats_.deathsToday);
    settlements_.UpdateMacro(world_, rng_, stats_.dayCount, villageMarkers_, factions_);
    warDeathsTotal += settlements_.ConsumeWarDeaths();
    factions_.UpdateStats(settlements_);
    factions_.UpdateDiplomacy(settlements_, rng_, stats_.dayCount);
  }
  stats_.deathsToday += warDeathsTotal;
  humans_.RecordWarDeaths(warDeathsTotal);
  stats_.totalBirths += stats_.birthsToday;
  stats_.totalDeaths += stats_.deathsToday;
  for (auto& marker : villageMarkers_) {
    if (marker.ttlDays > 0) {
      marker.ttlDays = std::max(0, marker.ttlDays - days);
    }
  }
  villageMarkers_.erase(
      std::remove_if(villageMarkers_.begin(), villageMarkers_.end(),
                     [](const VillageMarker& marker) { return marker.ttlDays <= 0; }),
      villageMarkers_.end());
  RefreshTotals();
}

void App::EnterMacroMode() {
  if (macroActive_) return;
  macroActive_ = true;
  humans_.EnterMacro(settlements_);
  RefreshTotals();
}

void App::ExitMacroMode() {
  if (!macroActive_) return;
  macroActive_ = false;
  humans_.ExitMacro(settlements_, rng_);
  RefreshTotals();
}

void App::ApplyToolAt(int tileX, int tileY, bool erase) {
  CrashContextSetNote("ApplyToolAt");
  int radius = ui_.brushSize / 2;
  bool spawned = false;

  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int x = tileX + dx;
      int y = tileY + dy;
      if (!world_.InBounds(x, y)) continue;

      if (erase) {
        world_.EraseAt(x, y);
      } else {
        switch (ui_.tool) {
          case ToolType::PlaceLand:
            world_.SetTileType(x, y, TileType::Land);
            break;
          case ToolType::PlaceFreshWater:
            world_.EditTile(x, y, [&](Tile& tile) {
              tile.type = TileType::FreshWater;
              tile.trees = 0;
              tile.food = 0;
              tile.burning = false;
              tile.burnDaysRemaining = 0;
              tile.building = BuildingType::None;
              tile.farmStage = 0;
              tile.buildingOwnerId = -1;
            });
            world_.MarkBuildingDirty();
            break;
          case ToolType::AddTrees:
            world_.EditTile(x, y, [&](Tile& tile) {
              if (tile.type != TileType::Land) return;
              int trees = static_cast<int>(tile.trees);
              trees = std::min(20, trees + 5);
              tile.trees = static_cast<uint8_t>(trees);
            });
            break;
          case ToolType::AddFood:
            world_.EditTile(x, y, [&](Tile& tile) {
              if (tile.type != TileType::Land) return;
              int food = static_cast<int>(tile.food);
              food = std::min(50, food + 10);
              tile.food = static_cast<uint8_t>(food);
            });
            break;
          case ToolType::SpawnMale:
            if (world_.At(x, y).type != TileType::Ocean) {
              humans_.Spawn(x, y, false, rng_);
              CrashContextSetNote("ApplyToolAt: SpawnMale");
              spawned = true;
            }
            break;
          case ToolType::SpawnFemale:
            if (world_.At(x, y).type != TileType::Ocean) {
              humans_.Spawn(x, y, true, rng_);
              CrashContextSetNote("ApplyToolAt: SpawnFemale");
              spawned = true;
            }
            break;
          case ToolType::Fire:
            if (world_.At(x, y).type == TileType::Land && world_.At(x, y).trees > 0) {
              world_.SetBurning(x, y, true, 4);
            }
            break;
          case ToolType::Meteor:
            world_.EditTile(x, y, [&](Tile& tile) {
              tile.type = TileType::Ocean;
              tile.trees = 0;
              tile.food = 0;
              tile.burning = false;
              tile.burnDaysRemaining = 0;
              tile.building = BuildingType::None;
              tile.farmStage = 0;
              tile.buildingOwnerId = -1;
            });
            world_.MarkBuildingDirty();
            break;
          case ToolType::GiftFood:
            world_.EditTile(x, y, [&](Tile& tile) {
              if (tile.type != TileType::Land) return;
              tile.food = 50;
            });
            break;
        }
      }
    }
  }

  stats_.totalFood = world_.TotalFood();
  stats_.totalTrees = world_.TotalTrees();
  if (spawned) {
    stats_.totalPop = macroActive_ ? humans_.MacroPopulation(settlements_) : humans_.CountAlive();
  }
  CrashContextSetNote("");
}

void App::ResetSimulationState() {
  humans_ = HumanManager();
  settlements_ = SettlementManager();
  factions_ = FactionManager();
  villageMarkers_.clear();
  stats_ = SimStats{};
  tickCount_ = 0;
  accumulator_ = 0.0;
  macroActive_ = false;
  hoverValid_ = false;
  hoverInfo_ = HoverInfo{};
  worldDirty_ = true;
  CrashContextSetDay(0);
  CrashContextSetPopulation(0);
}

void App::ResetCameraToWorld() {
  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);

  camera_.zoom = 1.0f;
  camera_.x = (world_.width() * kTileSize - winW) * 0.5f;
  camera_.y = (world_.height() * kTileSize - winH) * 0.5f;
  ClampCamera();
}

void App::FitCameraToWorld() {
  int winW = 0;
  int winH = 0;
  SDL_GetWindowSize(window_, &winW, &winH);

  float worldW = world_.width() * static_cast<float>(kTileSize);
  float worldH = world_.height() * static_cast<float>(kTileSize);
  if (worldW <= 0.0f || worldH <= 0.0f || winW <= 0 || winH <= 0) {
    return;
  }

  float zoomX = static_cast<float>(winW) / worldW;
  float zoomY = static_cast<float>(winH) / worldH;
  float zoom = std::min(zoomX, zoomY);
  if (zoom <= 0.0f) {
    return;
  }

  camera_.zoom = zoom;
  float viewW = winW / camera_.zoom;
  float viewH = winH / camera_.zoom;
  camera_.x = (worldW - viewW) * 0.5f;
  camera_.y = (worldH - viewH) * 0.5f;
  camera_.x = std::floor(camera_.x + 0.5f);
  camera_.y = std::floor(camera_.y + 0.5f);
}

void App::UpdateWholeMapView() {
  if (ui_.wholeMapView) {
    if (!wholeMapViewActive_) {
      savedCamera_ = camera_;
      wholeMapViewActive_ = true;
    }
    FitCameraToWorld();
  } else if (wholeMapViewActive_) {
    camera_ = savedCamera_;
    wholeMapViewActive_ = false;
    ClampCamera();
  }
}

void App::CreateNewWorld(int scale) {
  if (scale < 1) scale = 1;
  world_ = World(kDefaultWidth * scale, kDefaultHeight * scale);
  ResetSimulationState();
  world_.RecomputeScentFields();
  CrashContextSetWorld(world_.width(), world_.height());

  ResetCameraToWorld();
  savedCamera_ = camera_;
  wholeMapViewActive_ = ui_.wholeMapView;
  if (ui_.wholeMapView) {
    FitCameraToWorld();
  }

  RefreshTotals();
  worldDirty_ = false;
}

bool App::SaveMap(const char* path) const {
  if (!path || !path[0]) {
    return false;
  }

  std::filesystem::path outPath(path);
  if (outPath.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
  }

  return world_.SaveMap(outPath.string());
}

bool App::LoadMap(const char* path) {
  if (!path || !path[0]) {
    return false;
  }

  if (!world_.LoadMap(path)) {
    return false;
  }

  ResetSimulationState();
  CrashContextSetWorld(world_.width(), world_.height());

  ResetCameraToWorld();
  savedCamera_ = camera_;
  wholeMapViewActive_ = ui_.wholeMapView;
  if (ui_.wholeMapView) {
    FitCameraToWorld();
  }

  RefreshTotals();
  return true;
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

  camera_.x = std::floor(camera_.x + 0.5f);
  camera_.y = std::floor(camera_.y + 0.5f);
  camera_.x = Clamp(camera_.x, 0.0f, maxX);
  camera_.y = Clamp(camera_.y, 0.0f, maxY);
}

void App::RefreshTotals() {
  stats_.totalFood = world_.TotalFood();
  stats_.totalTrees = world_.TotalTrees();
  stats_.totalPop = macroActive_ ? humans_.MacroPopulation(settlements_) : humans_.CountAlive();
  stats_.totalSettlements = settlements_.Count();
  stats_.totalStockFood = 0;
  stats_.totalStockWood = 0;
  stats_.totalHouses = 0;
  stats_.totalFarms = 0;
  stats_.totalGranaries = 0;
  stats_.totalWells = 0;
  stats_.totalTownHalls = 0;
  stats_.totalHousingCap = 0;
  stats_.totalSoldiers = 0;
  stats_.totalScouts = 0;
  stats_.totalVillages = 0;
  stats_.totalTowns = 0;
  stats_.totalCities = 0;
  stats_.totalLegendary = 0;
  stats_.totalWars = 0;
  stats_.legendaryShown = 0;
  for (int i = 0; i < SimStats::kLegendaryDisplayCount; ++i) {
    stats_.legendary[i] = SimStats::LegendaryInfo{};
  }
  for (const auto& settlement : settlements_.Settlements()) {
    stats_.totalStockFood += settlement.stockFood;
    stats_.totalStockWood += settlement.stockWood;
    stats_.totalHouses += settlement.houses;
    stats_.totalFarms += settlement.farms;
    stats_.totalGranaries += settlement.granaries;
    stats_.totalWells += settlement.wells;
    stats_.totalTownHalls += settlement.townHalls;
    stats_.totalHousingCap += settlement.housingCap;
    stats_.totalSoldiers += settlement.soldiers;
    stats_.totalScouts += settlement.scouts;
    if (settlement.tier == SettlementTier::Village) {
      stats_.totalVillages++;
    } else if (settlement.tier == SettlementTier::Town) {
      stats_.totalTowns++;
    } else if (settlement.tier == SettlementTier::City) {
      stats_.totalCities++;
    }
  }
  factions_.UpdateStats(settlements_);
  factions_.UpdateLeaders(settlements_, humans_);
  stats_.totalWars = factions_.WarCount();

  for (const auto& human : humans_.Humans()) {
    if (!human.alive) continue;
    if (!human.legendary) continue;
    stats_.totalLegendary++;
    if (stats_.legendaryShown >= SimStats::kLegendaryDisplayCount) continue;
    auto& info = stats_.legendary[stats_.legendaryShown++];
    info.id = human.id;
    info.ageDays = human.ageDays;
    info.settlementId = human.settlementId;
    info.factionId = -1;
    if (human.settlementId > 0) {
      const Settlement* settlement = settlements_.Get(human.settlementId);
      if (settlement) {
        info.factionId = settlement->factionId;
      }
    }
    info.traits = human.traits;
    info.legendary = human.legendary;
    HumanTraitsToString(info.traitsText, sizeof(info.traitsText), human.traits, human.legendary);
  }
  CrashContextSetPopulation(
      static_cast<int>(std::min<int64_t>(stats_.totalPop, std::numeric_limits<int>::max())));
}

void App::WriteDeathLog() const {
  std::ofstream out("death_log.txt", std::ios::trunc);
  if (!out.is_open()) {
    return;
  }

  out << "funsim_death_log\n";
  out << "days=" << stats_.dayCount << "\n";
  out << "total_births=" << stats_.totalBirths << "\n";
  out << "total_deaths=" << stats_.totalDeaths << "\n";
  out << "final_population=" << stats_.totalPop << "\n";

  const auto& deathStats = humans_.GetDeathSummary();
  out << "starvation=" << deathStats.starvation << "\n";
  out << "dehydration=" << deathStats.dehydration << "\n";
  out << "old_age=" << deathStats.oldAge << "\n";
  out << "war=" << deathStats.war << "\n";
  out << "macro_natural=" << deathStats.macroNatural << "\n";
  out << "macro_starvation=" << deathStats.macroStarvation << "\n";
  out << "macro_fire=" << deathStats.macroFire << "\n";

  out << "\n# micro_deaths\n";
  out << "day,id,reason\n";
  for (const auto& record : humans_.DeathLog()) {
    out << record.day << "," << record.humanId << "," << DeathReasonName(record.reason) << "\n";
  }

  out << "\n# macro_deaths_summary\n";
  out << "macro counts are aggregated; no per-human ids in macro mode\n";
}
