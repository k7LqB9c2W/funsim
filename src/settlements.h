#pragma once

#include <cstdint>
#include <vector>

#include "render.h"

class HumanManager;
class FactionManager;
class Random;
class World;

enum class TaskType : uint8_t {
  CollectFood,
  CollectWood,
  HarvestFarm,
  PlantFarm,
  BuildStructure,
  HaulToStockpile,
  HaulWoodToStockpile,
  PatrolEdge
};

enum class SettlementTier : uint8_t { Village, Town, City };

struct Task {
  TaskType type{};
  int x = 0;
  int y = 0;
  int amount = 0;
  int settlementId = -1;
  BuildingType buildType = BuildingType::None;
};

struct Settlement {
  static constexpr int kTaskCap = 2048;
  static constexpr int kHouseCapacity = 10;
  static constexpr int kTownHallCapacity = 8;
  static constexpr int kHouseWoodCost = 6;
  static constexpr int kFarmWoodCost = 6;
  static constexpr int kGranaryWoodCost = 6;
  static constexpr int kWellWoodCost = 8;
  static constexpr int kTownHallWoodCost = 18;
  static constexpr int kFarmYield = 50;
  static constexpr int kFarmReadyStage = 2;

  int id = 0;
  int centerX = 0;
  int centerY = 0;
  int factionId = 0;
  int stockFood = 0;
  int stockWood = 0;
  int population = 0;
  int gatherers = 0;
  int farmers = 0;
  int builders = 0;
  int guards = 0;
  int soldiers = 0;
  int scouts = 0;
  int idle = 0;
  int ageDays = 0;
  int houses = 0;
  int farms = 0;
  int granaries = 0;
  int wells = 0;
  int farmsPlanted = 0;
  int farmsReady = 0;
  int townHalls = 0;
  int housingCap = 0;
  SettlementTier tier = SettlementTier::Village;
  int techTier = 0;
  float techProgress = 0.0f;
  int stability = 80;
  int unrest = 0;
  int borderPressure = 0;
  int warPressure = 0;
  int influenceRadius = 0;
  bool isCapital = false;
  int waterTargetX = 0;
  int waterTargetY = 0;
  bool hasWaterTarget = false;

  int macroPopM[6] = {};
  int macroPopF[6] = {};
  float macroBirthAccum = 0.0f;
  float macroFarmFoodAccum = 0.0f;
  float macroFoodNeedAccum = 0.0f;

  Task tasks[kTaskCap];
  int taskHead = 0;
  int taskTail = 0;

  bool PushTask(const Task& task) {
    int next = (taskTail + 1) % kTaskCap;
    if (next == taskHead) return false;
    tasks[taskTail] = task;
    taskTail = next;
    return true;
  }

  bool PopTask(Task& out) {
    if (taskHead == taskTail) return false;
    out = tasks[taskHead];
    taskHead = (taskHead + 1) % kTaskCap;
    return true;
  }

  int TaskCount() const {
    int count = taskTail - taskHead;
    if (count < 0) count += kTaskCap;
    return count;
  }

  void ClearMacroPools() {
    for (int i = 0; i < 6; ++i) {
      macroPopM[i] = 0;
      macroPopF[i] = 0;
    }
    macroBirthAccum = 0.0f;
    macroFarmFoodAccum = 0.0f;
    macroFoodNeedAccum = 0.0f;
    taskHead = 0;
    taskTail = 0;
  }

  int MacroTotal() const {
    int total = 0;
    for (int i = 0; i < 6; ++i) {
      total += macroPopM[i] + macroPopF[i];
    }
    return total;
  }
};

class SettlementManager {
 public:
  void UpdateDaily(World& world, HumanManager& humans, Random& rng, int dayCount,
                   std::vector<VillageMarker>& markers, FactionManager& factions);
  void UpdateMacro(World& world, Random& rng, int dayCount, std::vector<VillageMarker>& markers,
                   FactionManager& factions);
  void RefreshBuildingStats(const World& world) { RecomputeSettlementBuildings(world); }
  int ConsumeWarDeaths();

  int Count() const { return static_cast<int>(settlements_.size()); }
  const std::vector<Settlement>& Settlements() const { return settlements_; }
  std::vector<Settlement>& SettlementsMutable() { return settlements_; }

  bool HasSettlement(int settlementId) const;
  const Settlement* Get(int settlementId) const;
  Settlement* GetMutable(int settlementId);
  int ZoneOwnerForTile(int x, int y) const;
  int ZoneOwnerAt(int zx, int zy) const;
  int ZonePopAt(int zx, int zy) const;
  int ZoneConflictAt(int zx, int zy) const;
  int ZoneSize() const { return zoneSize_; }
  int ZonesX() const { return zonesX_; }
  int ZonesY() const { return zonesY_; }

 private:
  void EnsureZoneBuffers(const World& world);
  void RecomputeZonePop(const World& world, const HumanManager& humans);
  void RecomputeZonePopMacro();
  void TryFoundNewSettlements(World& world, Random& rng, int dayCount,
                              std::vector<VillageMarker>& markers, FactionManager& factions);
  void RecomputeZoneOwners(const World& world);
  void UpdateZoneConflict(const FactionManager& factions);
  void AssignHumansToSettlements(HumanManager& humans);
  void RecomputeSettlementBuildings(const World& world);
  void UpdateSettlementCaps();
  void ComputeSettlementWaterTargets(const World& world);
  void RecomputeSettlementPopAndRoles(World& world, Random& rng, int dayCount,
                                      HumanManager& humans);
  void UpdateSettlementRoleStatsMacro(World& world);
  void UpdateSettlementEvolution(const FactionManager& factions, Random& rng);
  void UpdateSettlementStability(const FactionManager& factions, Random& rng);
  void UpdateSettlementInfluence(const FactionManager& factions);
  void UpdateCapitalStatus(const FactionManager& factions);
  void ApplyConflictImpact(World& world, HumanManager& humans, Random& rng, int dayCount,
                           FactionManager& factions);
  void ApplyConflictImpactMacro(World& world, Random& rng, int dayCount,
                                FactionManager& factions);
  void UpdateBorderPressure(const FactionManager& factions);
 void GenerateTasks(World& world, Random& rng);
  void RunSettlementEconomy(World& world, Random& rng);
  void EnsureSettlementFactions(FactionManager& factions, Random& rng);

  struct ClaimSource {
    int x = 0;
    int y = 0;
    int radius = 0;
  };

  int nextId_ = 1;
  std::vector<Settlement> settlements_;

  int zoneSize_ = 8;
  int zonesX_ = 0;
  int zonesY_ = 0;

  std::vector<int> zonePop_;
  std::vector<int> zoneDenseDays_;
  std::vector<int> zoneOwner_;
  std::vector<int> zoneConflict_;
  std::vector<int> memberCounts_;
  std::vector<int> memberOffsets_;
  std::vector<int> memberIndices_;
  std::vector<int> idToIndex_;
  std::vector<std::vector<ClaimSource>> claimSources_;
  int warDeathsPending_ = 0;
  bool homeFieldDirty_ = true;
};

const char* SettlementTierName(SettlementTier tier);
int HouseCapacityForTier(int tier);
int TownHallCapacityForTier(int tier);
int FarmYieldForTier(int tier);
int GatherYieldForTier(int tier);
int FarmsPerPopForTier(int tier);
