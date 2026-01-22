#pragma once

#include <cstdint>

enum class OverlayMode : uint8_t {
  None = 0,
  FactionTerritory,
  SettlementInfluence,
  PopulationHeat,
  Conflict,
};
