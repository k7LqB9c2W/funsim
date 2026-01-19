#pragma once

#include <cstdint>
#include <vector>

#include "render.h"

class HumanManager;
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
  static constexpr int kHouseCapacity = 4;
  static constexpr int kTownHallCapacity = 8;
  static constexpr int kHouseWoodCost = 10;
  static constexpr int kFarmWoodCost = 6;
  static constexpr int kTownHallWoodCost = 18;
  static constexpr int kFarmYield = 8;
  static constexpr int kFarmReadyStage = 3;

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
  int idle = 0;
  int ageDays = 0;
  int houses = 0;
  int farms = 0;
  int townHalls = 0;
  int housingCap = 0;
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
                   std::vector<VillageMarker>& markers);
  void UpdateMacro(World& world, Random& rng, int dayCount, std::vector<VillageMarker>& markers);
  void RefreshBuildingStats(const World& world) { RecomputeSettlementBuildings(world); }

  int Count() const { return static_cast<int>(settlements_.size()); }
  const std::vector<Settlement>& Settlements() const { return settlements_; }
  std::vector<Settlement>& SettlementsMutable() { return settlements_; }

  bool HasSettlement(int settlementId) const;
  const Settlement* Get(int settlementId) const;
  Settlement* GetMutable(int settlementId);
  int ZoneOwnerForTile(int x, int y) const;

 private:
  void EnsureZoneBuffers(const World& world);
  void RecomputeZonePop(const World& world, const HumanManager& humans);
  void RecomputeZonePopMacro();
  void TryFoundNewSettlements(World& world, Random& rng, int dayCount,
                              std::vector<VillageMarker>& markers);
  void RecomputeZoneOwners(const World& world);
  void AssignHumansToSettlements(HumanManager& humans);
  void RecomputeSettlementBuildings(const World& world);
  void ComputeSettlementWaterTargets(const World& world);
  void RecomputeSettlementPopAndRoles(World& world, Random& rng, int dayCount,
                                      HumanManager& humans);
  void GenerateTasks(World& world, Random& rng);
  void RunSettlementEconomy(World& world, Random& rng);

  int nextId_ = 1;
  std::vector<Settlement> settlements_;

  int zoneSize_ = 8;
  int zonesX_ = 0;
  int zonesY_ = 0;

  std::vector<int> zonePop_;
  std::vector<int> zoneDenseDays_;
  std::vector<int> zoneOwner_;
  std::vector<int> memberCounts_;
  std::vector<int> memberOffsets_;
  std::vector<int> memberIndices_;
  std::vector<int> idToIndex_;
};
