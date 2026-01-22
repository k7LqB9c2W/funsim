#pragma once

#include <cstdint>

#include "overlays.h"
#include "tools.h"

class FactionManager;
class SettlementManager;

struct SimStats {
  int dayCount = 0;
  int totalPop = 0;
  int birthsToday = 0;
  int deathsToday = 0;
  int totalBirths = 0;
  int totalDeaths = 0;
  int totalFood = 0;
  int totalTrees = 0;
  int totalSettlements = 0;
  int totalStockFood = 0;
  int totalStockWood = 0;
  int totalHouses = 0;
  int totalFarms = 0;
  int totalGranaries = 0;
  int totalWells = 0;
  int totalTownHalls = 0;
  int totalHousingCap = 0;
  int totalSoldiers = 0;
  int totalScouts = 0;
  int totalVillages = 0;
  int totalTowns = 0;
  int totalCities = 0;
  int totalLegendary = 0;
  int totalWars = 0;

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
  bool starvationDeathEnabled = true;
  bool dehydrationDeathEnabled = true;
  OverlayMode overlayMode = OverlayMode::FactionTerritory;
  bool wholeMapView = false;
  bool saveMap = false;
  bool loadMap = false;
  char mapPath[256] = "maps/map.fmap";
};

struct HoverInfo {
  bool valid = false;
  int tileX = 0;
  int tileY = 0;
  int settlementId = -1;
  int factionId = -1;
};

void DrawUI(UIState& state, const SimStats& stats, const FactionManager& factions,
            const SettlementManager& settlements, const HoverInfo& hover);
