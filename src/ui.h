#pragma once

#include <cstdint>

#include "overlays.h"
#include "tools.h"

class FactionManager;
class SettlementManager;
class HumanManager;

struct SimStats {
  int dayCount = 0;
  int64_t totalPop = 0;
  int birthsToday = 0;
  int deathsToday = 0;
  int64_t totalBirths = 0;
  int64_t totalDeaths = 0;
  int64_t totalFood = 0;
  int64_t totalTrees = 0;
  int64_t totalSettlements = 0;
  int64_t totalStockFood = 0;
  int64_t totalStockWood = 0;
  int64_t totalHouses = 0;
  int64_t totalFarms = 0;
  int64_t totalGranaries = 0;
  int64_t totalWells = 0;
  int64_t totalTownHalls = 0;
  int64_t totalHousingCap = 0;
  int64_t totalSoldiers = 0;
  int64_t totalScouts = 0;
  int64_t totalVillages = 0;
  int64_t totalTowns = 0;
  int64_t totalCities = 0;
  int64_t totalLegendary = 0;
  int64_t totalWars = 0;

  struct LegendaryInfo {
    int id = 0;
    int ageDays = 0;
    int settlementId = -1;
    int factionId = -1;
    uint16_t traits = 0;
    bool legendary = false;
    char traitsText[96] = "";
  };
  static constexpr int kLegendaryDisplayCount = 8;
  LegendaryInfo legendary[kLegendaryDisplayCount];
  int legendaryShown = 0;
};

struct UIState {
  ToolType tool = ToolType::PlaceLand;
  int brushSize = 1;
  bool paused = false;
  int speedIndex = 0;
  bool stepDay = false;
  bool warEnabled = true;
  bool rebellionsEnabled = true;
  bool starvationDeathEnabled = true;
  bool dehydrationDeathEnabled = true;
  OverlayMode overlayMode = OverlayMode::FactionTerritory;
  bool wholeMapView = false;
  int territoryOverlayAlpha = 90;
  float territoryOverlayDarken = 0.65f;
  bool showWarZones = true;
  bool showWarArrows = true;
  bool showTroopCounts = true;
  bool showTroopCountsAllZones = false;
  int worldSizeIndex = 0;
  bool newWorld = false;
  bool saveMap = false;
  bool loadMap = false;
  char mapPath[256] = "maps/map.fmap";

  int selectedFactionId = -1;
  bool factionEditorOpen = false;
  int lastFactionEditorId = -1;
  char factionNameBuf[96] = "";
  char factionIdeologyBuf[96] = "";
  char factionLeaderNameBuf[96] = "";
  char factionLeaderTitleBuf[96] = "";

  bool warDebugOpen = false;
  int warDebugSettlementId = -1;
  int warDebugFactionId = -1;
  bool warDebugFollowHover = true;
  bool warLoggingEnabled = false;
  bool warLogOnlySelected = true;

  int diplomacyOtherFactionId = -1;
  bool requestArmyOrdersRefresh = false;
};

struct HoverInfo {
  bool valid = false;
  int tileX = 0;
  int tileY = 0;
  int settlementId = -1;
  int factionId = -1;
};

void DrawUI(UIState& state, const SimStats& stats, FactionManager& factions,
            const SettlementManager& settlements, const HumanManager& humans, const HoverInfo& hover);
// Backward-compatible overload for stale build artifacts.
void DrawUI(UIState& state, const SimStats& stats, FactionManager& factions,
            const SettlementManager& settlements, const HoverInfo& hover);
