#pragma once

#include "tools.h"

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
  int totalTownHalls = 0;
  int totalHousingCap = 0;
};

struct UIState {
  ToolType tool = ToolType::PlaceLand;
  int brushSize = 1;
  bool paused = false;
  int speedIndex = 0;
  bool stepDay = false;
};

void DrawUI(UIState& state, const SimStats& stats);
