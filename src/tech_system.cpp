#include "tech_system.h"

#include <algorithm>

#include "settlements.h"

namespace TechSystem {
namespace {

using T = TechId;

constexpr int Index(T tech) {
  return static_cast<int>(tech);
}

const std::array<TechDefinition, kTechCount> kDefinitions = {{
    {T::OralTradition, "Oral Tradition", "Ancient",
     "Unlocks monuments that preserve identity and spark early ideas.", 85.0f, 0,
     kNoTech, kNoTech, BuildingType::Monument},
    {T::Masonry, "Masonry", "Ancient",
     "Unlocks walls so settlements can become harder targets during wars.", 130.0f, 1,
     Index(T::OralTradition), kNoTech, BuildingType::Walls},
    {T::MilitaryDrill, "Military Drill", "Ancient",
     "Unlocks barracks that organize soldiers and strengthen border towns.", 145.0f, 1,
     Index(T::OralTradition), kNoTech, BuildingType::Barracks},
    {T::Writing, "Writing", "Classical",
     "Unlocks archives and turns stored knowledge into reliable research.", 150.0f, 1,
     Index(T::OralTradition), kNoTech, BuildingType::Archive},
    {T::Currency, "Currency", "Classical",
     "Unlocks mints that make gold and trade matter at the kingdom level.", 220.0f, 2,
     Index(T::Writing), kNoTech, BuildingType::Mint},
    {T::Education, "Education", "Classical",
     "Unlocks schools and creates the first real research snowball.", 260.0f, 2,
     Index(T::Writing), kNoTech, BuildingType::School},
}};

bool PrereqMet(uint64_t mask, int prereq) {
  if (prereq == kNoTech) return true;
  if (prereq < 0 || prereq >= kTechCount) return false;
  return HasTech(mask, static_cast<TechId>(prereq));
}

}  // namespace

const std::array<TechDefinition, kTechCount>& Definitions() {
  return kDefinitions;
}

const TechDefinition& Definition(TechId tech) {
  return kDefinitions[static_cast<size_t>(tech)];
}

const TechDefinition* DefinitionByIndex(int techIndex) {
  if (techIndex < 0 || techIndex >= kTechCount) return nullptr;
  return &kDefinitions[static_cast<size_t>(techIndex)];
}

uint64_t Bit(TechId tech) {
  return 1ull << static_cast<uint8_t>(tech);
}

bool HasTech(uint64_t mask, TechId tech) {
  return (mask & Bit(tech)) != 0;
}

bool PrerequisitesMet(uint64_t mask, const TechDefinition& tech) {
  return PrereqMet(mask, tech.prereqA) && PrereqMet(mask, tech.prereqB);
}

int FirstAvailableTech(uint64_t mask) {
  for (const auto& tech : kDefinitions) {
    if (HasTech(mask, tech.id)) continue;
    if (!PrerequisitesMet(mask, tech)) continue;
    return static_cast<int>(tech.id);
  }
  return kNoTech;
}

int EraLevelForMask(uint64_t mask) {
  int level = 0;
  for (const auto& tech : kDefinitions) {
    if (HasTech(mask, tech.id)) {
      level = std::max(level, tech.eraLevel);
    }
  }
  return level;
}

const char* EraNameForLevel(int eraLevel) {
  if (eraLevel <= 0) return "Ancient";
  if (eraLevel == 1) return "Classical";
  if (eraLevel == 2) return "Early State";
  if (eraLevel == 3) return "Medieval";
  if (eraLevel == 4) return "Renaissance";
  if (eraLevel == 5) return "Industrial";
  return "Modern";
}

const char* BuildingName(BuildingType building) {
  switch (building) {
    case BuildingType::House:
      return "House";
    case BuildingType::TownHall:
      return "Town Hall";
    case BuildingType::Farm:
      return "Farm";
    case BuildingType::Granary:
      return "Granary";
    case BuildingType::Well:
      return "Well";
    case BuildingType::Market:
      return "Market";
    case BuildingType::Forge:
      return "Forge";
    case BuildingType::Monument:
      return "Monument";
    case BuildingType::Archive:
      return "Archive";
    case BuildingType::Walls:
      return "Walls";
    case BuildingType::Barracks:
      return "Barracks";
    case BuildingType::Mint:
      return "Mint";
    case BuildingType::School:
      return "School";
    default:
      return "None";
  }
}

bool IsTechBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::Monument:
    case BuildingType::Archive:
    case BuildingType::Walls:
    case BuildingType::Barracks:
    case BuildingType::Mint:
    case BuildingType::School:
      return true;
    default:
      return false;
  }
}

bool IsLargeBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::TownHall:
    case BuildingType::Market:
    case BuildingType::Forge:
    case BuildingType::Monument:
    case BuildingType::Archive:
    case BuildingType::Walls:
    case BuildingType::Barracks:
    case BuildingType::Mint:
    case BuildingType::School:
      return true;
    default:
      return false;
  }
}

bool IsUnlocked(uint64_t mask, BuildingType building) {
  if (!IsTechBuilding(building)) return true;
  for (const auto& tech : kDefinitions) {
    if (tech.unlockBuilding == building) {
      return HasTech(mask, tech.id);
    }
  }
  return false;
}

BuildingCost CostForBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::House:
      return {Settlement::kHouseWoodCost, 0, 0};
    case BuildingType::TownHall:
      return {Settlement::kTownHallWoodCost, 0, 0};
    case BuildingType::Farm:
      return {Settlement::kFarmWoodCost, 0, 0};
    case BuildingType::Granary:
      return {Settlement::kGranaryWoodCost, 0, 0};
    case BuildingType::Well:
      return {Settlement::kWellWoodCost, 0, 0};
    case BuildingType::Market:
      return {Settlement::kMarketWoodCost, Settlement::kMarketStoneCost, 0};
    case BuildingType::Forge:
      return {Settlement::kForgeWoodCost, Settlement::kForgeStoneCost, Settlement::kForgeMetalCost};
    case BuildingType::Monument:
      return {12, 8, 0};
    case BuildingType::Archive:
      return {18, 12, 0};
    case BuildingType::Walls:
      return {28, 36, 0};
    case BuildingType::Barracks:
      return {24, 16, 4};
    case BuildingType::Mint:
      return {18, 30, 6};
    case BuildingType::School:
      return {26, 20, 0};
    default:
      return {};
  }
}

int ClaimRadiusForBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::Monument:
      return 14;
    case BuildingType::Archive:
      return 12;
    case BuildingType::Walls:
      return 18;
    case BuildingType::Barracks:
      return 14;
    case BuildingType::Mint:
      return 12;
    case BuildingType::School:
      return 14;
    default:
      return 0;
  }
}

int ResearchYieldForBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::Monument:
      return 1;
    case BuildingType::Archive:
      return 4;
    case BuildingType::School:
      return 7;
    case BuildingType::Mint:
      return 1;
    default:
      return 0;
  }
}

int StabilityYieldForBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::Monument:
      return 4;
    case BuildingType::Walls:
      return 2;
    case BuildingType::School:
      return 2;
    default:
      return 0;
  }
}

int ProsperityYieldForBuilding(BuildingType building) {
  switch (building) {
    case BuildingType::Mint:
      return 8;
    case BuildingType::Archive:
      return 3;
    case BuildingType::School:
      return 4;
    default:
      return 0;
  }
}

}  // namespace TechSystem
