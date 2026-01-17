#pragma once

#include <cstdint>
#include <vector>

#include "util.h"
#include "world.h"

class SettlementManager;
enum class TaskType : uint8_t;

enum class Goal : uint8_t { Wander, SeekFood, SeekWater, SeekMate, StayHome, FleeFire };
enum class Role : uint8_t { Idle, Gatherer, Builder, Guard };

struct Human {
  int id = 0;
  bool female = false;
  int ageDays = 0;
  int x = 0;
  int y = 0;
  bool alive = true;
  bool pregnant = false;
  int gestationDays = 0;
  int daysWithoutFood = 0;
  int daysWithoutWater = 0;
  float animTimer = 0.0f;
  int animFrame = 0;
  bool moving = false;
  Goal goal = Goal::Wander;
  Role role = Role::Idle;
  int targetX = 0;
  int targetY = 0;
  int homeX = 0;
  int homeY = 0;
  int lastFoodX = 0;
  int lastFoodY = 0;
  int lastWaterX = 0;
  int lastWaterY = 0;
  int rethinkCooldownTicks = 0;
  int mateCooldownDays = 0;
  int settlementId = -1;
  uint8_t bravery = 0;
  uint8_t greed = 0;
  uint8_t wanderlust = 0;
  int parentIdMother = -1;
  int parentIdFather = -1;
  float moveAccum = 0.0f;
  uint8_t blockedTicks = 0;
  bool forceReplan = false;
  int mateTargetId = -1;
  bool hasTask = false;
  TaskType taskType{};
  int taskX = 0;
  int taskY = 0;
  int taskAmount = 0;
  int taskSettlementId = -1;
  bool carrying = false;
  int carryFood = 0;
};

class HumanManager {
 public:
  HumanManager();

  void Spawn(int x, int y, bool female, Random& rng);
  void UpdateTick(World& world, SettlementManager& settlements, Random& rng, int tickCount,
                  float tickSeconds, int ticksPerDay);
  void UpdateDailyCoarse(World& world, SettlementManager& settlements, Random& rng, int dayCount,
                         int& birthsToday, int& deathsToday);
  void EnterMacro(SettlementManager& settlements);
  void ExitMacro(SettlementManager& settlements, Random& rng);
  void AdvanceMacro(World& world, SettlementManager& settlements, Random& rng, int days,
                    int& birthsToday, int& deathsToday);
  int MacroPopulation(const SettlementManager& settlements) const;
  void UpdateAnimation(float dt);

  int CountAlive() const;
  const std::vector<Human>& Humans() const { return humans_; }
  std::vector<Human>& HumansMutable() { return humans_; }

 private:
  Human CreateHuman(int x, int y, bool female, Random& rng, int ageDays);
  void ReplanGoal(Human& human, const World& world, const SettlementManager& settlements,
                  Random& rng, int tickCount, int ticksPerDay);
  bool GetHumanById(int id, int& outX, int& outY) const;
  int FindMateTargetId(const Human& human, const World& world, Random& rng) const;
  void UpdateMoveStep(Human& human, World& world, SettlementManager& settlements, Random& rng,
                      int tickCount, int ticksPerDay);
  void RebuildIdMap();

  int nextId_ = 1;
  std::vector<Human> humans_;
  std::vector<int> adultMaleCounts_;
  std::vector<int> popCounts_;
  std::vector<int> adultMaleSampleId_;
  std::vector<int> humanIdToIndex_;
  std::vector<Human> newborns_;
  int thinkCursor_ = 0;
  bool macroActive_ = false;
  int macroFallbackM_[6] = {};
  int macroFallbackF_[6] = {};
  float macroFallbackBirthAccum_ = 0.0f;
  int macroFallbackX_ = 0;
  int macroFallbackY_ = 0;
  bool macroHasFallback_ = false;
};
