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
     "Unlocks walls so settlements can become harder targets during wars.", 130.0f, 0,
     Index(T::OralTradition), kNoTech, BuildingType::Walls},
    {T::MilitaryDrill, "Military Drill", "Ancient",
     "Unlocks barracks that organize soldiers and strengthen border towns.", 145.0f, 0,
     Index(T::OralTradition), kNoTech, BuildingType::Barracks},
    {T::Writing, "Writing", "Ancient",
     "Unlocks archives and turns stored knowledge into reliable research.", 150.0f, 0,
     Index(T::OralTradition), kNoTech, BuildingType::Archive},
    {T::Currency, "Currency", "Classical",
     "Unlocks mints that make gold and trade matter at the kingdom level.", 220.0f, 1,
     Index(T::Writing), kNoTech, BuildingType::Mint},
    {T::Education, "Education", "Classical",
     "Unlocks schools and creates the first real research snowball.", 260.0f, 1,
     Index(T::Writing), kNoTech, BuildingType::School},
    {T::Engineering, "Engineering", "Classical",
     "Improves large works and prepares cities for stronger infrastructure.", 240.0f, 1,
     Index(T::Masonry), kNoTech, BuildingType::None},
    {T::HorsebackRiding, "Horseback Riding", "Classical",
     "Improves mounted movement doctrine and future cavalry paths.", 235.0f, 1,
     Index(T::MilitaryDrill), kNoTech, BuildingType::None},
    {T::Apprenticeship, "Apprenticeship", "Medieval",
     "Formalizes skilled labor and raises the ceiling for productive towns.", 390.0f, 2,
     Index(T::Currency), Index(T::Engineering), BuildingType::Workshop},
    {T::Machinery, "Machinery", "Medieval",
     "Adds mechanical thinking to workshops, logistics, and battlefield tools.", 420.0f, 2,
     Index(T::Education), Index(T::HorsebackRiding), BuildingType::None},
    {T::Banking, "Banking", "Renaissance",
     "Organizes credit, treasuries, and larger economic networks.", 600.0f, 3,
     Index(T::Education), Index(T::Apprenticeship), BuildingType::Bank, BuildingType::University},
    {T::Gunpowder, "Gunpowder", "Renaissance",
     "Starts a new military age and points toward stronger siege warfare.", 620.0f, 3,
     Index(T::Apprenticeship), Index(T::Machinery), BuildingType::None},
    {T::Cartography, "Cartography", "Renaissance",
     "Makes coastlines strategically valuable through ports and longer trade reach.", 640.0f, 3,
     Index(T::Banking), Index(T::Engineering), BuildingType::Harbor},
    {T::Industrialization, "Industrialization", "Industrial",
     "Turns production into a kingdom-scale engine for growth.", 860.0f, 4,
     Index(T::Banking), Index(T::Gunpowder), BuildingType::Factory},
    {T::ScientificTheory, "Scientific Theory", "Industrial",
     "Makes knowledge systematic enough to support modern breakthroughs.", 900.0f, 4,
     Index(T::Education), Index(T::Banking), BuildingType::None},
    {T::SteamPower, "Steam Power", "Industrial",
     "Connects cities with reliable heavy transport and faster trade logistics.", 980.0f, 4,
     Index(T::Industrialization), Index(T::Cartography), BuildingType::RailDepot},
    {T::Sanitation, "Sanitation", "Industrial",
     "Keeps dense cities healthier and able to support larger populations.", 1020.0f, 4,
     Index(T::ScientificTheory), Index(T::Industrialization), BuildingType::Hospital},
    {T::Flight, "Flight", "Modern",
     "Opens the path to air power and faster long-distance reach.", 1250.0f, 5,
     Index(T::Industrialization), kNoTech, BuildingType::Airfield},
    {T::Electricity, "Electricity", "Modern",
     "Modernizes infrastructure and prepares settlements for advanced industry.", 1280.0f, 5,
     Index(T::Industrialization), Index(T::ScientificTheory), BuildingType::PowerPlant},
    {T::ReplaceableParts, "Replaceable Parts", "Modern",
     "Standardizes modern military logistics and centralized command.", 1450.0f, 5,
     Index(T::Flight), Index(T::Electricity), BuildingType::MilitaryHQ},
    {T::Rocketry, "Rocketry", "Atomic",
     "Pushes engineering beyond the ground and into strategic-scale projects.", 1800.0f, 6,
     Index(T::Flight), Index(T::Electricity), BuildingType::None},
    {T::NuclearFission, "Nuclear Fission", "Atomic",
     "Represents the first dangerous mastery of atomic energy.", 1900.0f, 6,
     Index(T::Electricity), Index(T::ScientificTheory), BuildingType::Reactor},
    {T::Satellites, "Satellites", "Information",
     "Adds global observation and communication as a future strategic layer.", 2500.0f, 7,
     Index(T::Rocketry), kNoTech, BuildingType::SatelliteArray},
    {T::Robotics, "Robotics", "Information",
     "Sets up late-game automation and advanced production systems.", 2700.0f, 7,
     Index(T::Rocketry), Index(T::NuclearFission), BuildingType::RoboticsLab},
    {T::Telecommunications, "Telecommunications", "Information",
     "Turns information itself into infrastructure for research and coordination.", 2850.0f, 7,
     Index(T::Satellites), Index(T::Robotics), BuildingType::DataCenter},
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
  if (eraLevel == 2) return "Medieval";
  if (eraLevel == 3) return "Renaissance";
  if (eraLevel == 4) return "Industrial";
  if (eraLevel == 5) return "Modern";
  if (eraLevel == 6) return "Atomic";
  return "Information";
}

const char* EraBoostSummaryForLevel(int eraLevel) {
  if (eraLevel <= 0) return "base housing, food, gathering, and mining";
  if (eraLevel == 1) return "slightly better housing, farms, and mining access";
  if (eraLevel == 2) return "stronger farms, gathering, housing, and gold potential";
  if (eraLevel == 3) return "denser farms, better housing, and stronger mining output";
  if (eraLevel == 4) return "industrial-scale food, gathering, housing, and extraction";
  if (eraLevel == 5) return "modern settlement productivity and larger housing capacity";
  if (eraLevel == 6) return "atomic-era productivity across core settlement systems";
  return "information-era productivity across core settlement systems";
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
    case BuildingType::Workshop:
      return "Workshop";
    case BuildingType::Bank:
      return "Bank";
    case BuildingType::University:
      return "University";
    case BuildingType::Factory:
      return "Factory";
    case BuildingType::PowerPlant:
      return "Power Plant";
    case BuildingType::Airfield:
      return "Airfield";
    case BuildingType::Reactor:
      return "Reactor";
    case BuildingType::SatelliteArray:
      return "Satellite Array";
    case BuildingType::RoboticsLab:
      return "Robotics Lab";
    case BuildingType::Harbor:
      return "Harbor";
    case BuildingType::RailDepot:
      return "Rail Depot";
    case BuildingType::Hospital:
      return "Hospital";
    case BuildingType::DataCenter:
      return "Data Center";
    case BuildingType::MilitaryHQ:
      return "Military HQ";
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
    case BuildingType::Workshop:
    case BuildingType::Bank:
    case BuildingType::University:
    case BuildingType::Factory:
    case BuildingType::PowerPlant:
    case BuildingType::Airfield:
    case BuildingType::Reactor:
    case BuildingType::SatelliteArray:
    case BuildingType::RoboticsLab:
    case BuildingType::Harbor:
    case BuildingType::RailDepot:
    case BuildingType::Hospital:
    case BuildingType::DataCenter:
    case BuildingType::MilitaryHQ:
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
    case BuildingType::Workshop:
    case BuildingType::Bank:
    case BuildingType::University:
    case BuildingType::Factory:
    case BuildingType::PowerPlant:
    case BuildingType::Airfield:
    case BuildingType::Reactor:
    case BuildingType::SatelliteArray:
    case BuildingType::RoboticsLab:
    case BuildingType::Harbor:
    case BuildingType::RailDepot:
    case BuildingType::Hospital:
    case BuildingType::DataCenter:
    case BuildingType::MilitaryHQ:
      return true;
    default:
      return false;
  }
}

bool IsUnlocked(uint64_t mask, BuildingType building) {
  if (!IsTechBuilding(building)) return true;
  for (const auto& tech : kDefinitions) {
    if (tech.unlockBuilding == building || tech.unlockBuildingB == building) {
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
    case BuildingType::Workshop:
      return {34, 28, 8};
    case BuildingType::Bank:
      return {30, 48, 12};
    case BuildingType::University:
      return {42, 38, 8};
    case BuildingType::Factory:
      return {54, 70, 28};
    case BuildingType::PowerPlant:
      return {46, 82, 34};
    case BuildingType::Airfield:
      return {58, 74, 32};
    case BuildingType::Reactor:
      return {64, 96, 42};
    case BuildingType::SatelliteArray:
      return {72, 88, 36};
    case BuildingType::RoboticsLab:
      return {84, 104, 48};
    case BuildingType::Harbor:
      return {52, 64, 18};
    case BuildingType::RailDepot:
      return {76, 92, 42};
    case BuildingType::Hospital:
      return {64, 82, 28};
    case BuildingType::DataCenter:
      return {92, 110, 56};
    case BuildingType::MilitaryHQ:
      return {70, 88, 46};
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
    case BuildingType::Workshop:
      return 13;
    case BuildingType::Bank:
      return 16;
    case BuildingType::University:
      return 17;
    case BuildingType::Factory:
      return 18;
    case BuildingType::PowerPlant:
      return 18;
    case BuildingType::Airfield:
      return 22;
    case BuildingType::Reactor:
      return 20;
    case BuildingType::SatelliteArray:
      return 24;
    case BuildingType::RoboticsLab:
      return 18;
    case BuildingType::Harbor:
      return 24;
    case BuildingType::RailDepot:
      return 18;
    case BuildingType::Hospital:
      return 16;
    case BuildingType::DataCenter:
      return 20;
    case BuildingType::MilitaryHQ:
      return 22;
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
    case BuildingType::University:
      return 11;
    case BuildingType::PowerPlant:
      return 2;
    case BuildingType::Reactor:
      return 8;
    case BuildingType::SatelliteArray:
      return 10;
    case BuildingType::RoboticsLab:
      return 14;
    case BuildingType::Hospital:
      return 3;
    case BuildingType::DataCenter:
      return 18;
    case BuildingType::MilitaryHQ:
      return 3;
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
    case BuildingType::Bank:
      return 2;
    case BuildingType::University:
      return 3;
    case BuildingType::PowerPlant:
      return 2;
    case BuildingType::Reactor:
      return -3;
    case BuildingType::SatelliteArray:
      return 2;
    case BuildingType::RoboticsLab:
      return 2;
    case BuildingType::Harbor:
      return 2;
    case BuildingType::Hospital:
      return 8;
    case BuildingType::DataCenter:
      return 2;
    case BuildingType::MilitaryHQ:
      return 4;
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
    case BuildingType::Workshop:
      return 6;
    case BuildingType::Bank:
      return 14;
    case BuildingType::University:
      return 8;
    case BuildingType::Factory:
      return 16;
    case BuildingType::PowerPlant:
      return 18;
    case BuildingType::Airfield:
      return 10;
    case BuildingType::Reactor:
      return 20;
    case BuildingType::SatelliteArray:
      return 12;
    case BuildingType::RoboticsLab:
      return 18;
    case BuildingType::Harbor:
      return 16;
    case BuildingType::RailDepot:
      return 14;
    case BuildingType::Hospital:
      return 10;
    case BuildingType::DataCenter:
      return 22;
    case BuildingType::MilitaryHQ:
      return 12;
    default:
      return 0;
  }
}

}  // namespace TechSystem
