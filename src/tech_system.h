#pragma once

#include <array>
#include <cstdint>

#include "world.h"

namespace TechSystem {

enum class TechId : uint8_t {
  OralTradition = 0,
  Masonry,
  MilitaryDrill,
  Writing,
  Currency,
  Education,
  Count,
};

static constexpr int kTechCount = static_cast<int>(TechId::Count);
static constexpr int kNoTech = -1;

struct BuildingCost {
  int wood = 0;
  int stone = 0;
  int metal = 0;
};

struct TechDefinition {
  TechId id = TechId::OralTradition;
  const char* name = "";
  const char* era = "";
  const char* description = "";
  float cost = 0.0f;
  int eraLevel = 0;
  int prereqA = kNoTech;
  int prereqB = kNoTech;
  BuildingType unlockBuilding = BuildingType::None;
};

const std::array<TechDefinition, kTechCount>& Definitions();
const TechDefinition& Definition(TechId tech);
const TechDefinition* DefinitionByIndex(int techIndex);

uint64_t Bit(TechId tech);
bool HasTech(uint64_t mask, TechId tech);
bool PrerequisitesMet(uint64_t mask, const TechDefinition& tech);
int FirstAvailableTech(uint64_t mask);
int EraLevelForMask(uint64_t mask);
const char* EraNameForLevel(int eraLevel);
const char* BuildingName(BuildingType building);

bool IsTechBuilding(BuildingType building);
bool IsLargeBuilding(BuildingType building);
bool IsUnlocked(uint64_t mask, BuildingType building);
BuildingCost CostForBuilding(BuildingType building);
int ClaimRadiusForBuilding(BuildingType building);
int ResearchYieldForBuilding(BuildingType building);
int StabilityYieldForBuilding(BuildingType building);
int ProsperityYieldForBuilding(BuildingType building);

}  // namespace TechSystem
