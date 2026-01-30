#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "util.h"
#include "world.h"

class SettlementManager;
enum class TaskType : uint8_t;

enum class Goal : uint8_t { Wander, SeekFood, SeekWater, SeekMate, StayHome, FleeFire };
enum class Role : uint8_t { Idle, Gatherer, Farmer, Builder, Guard, Soldier, Scout };
enum class DeathReason : uint8_t { Starvation, Dehydration, OldAge, War };
enum class ArmyState : uint8_t { Idle, Rally, March, Siege, Defend, Retreat };

enum class HumanTrait : uint16_t {
  Brave = 1u << 0,
  Lazy = 1u << 1,
  Wise = 1u << 2,
  Greedy = 1u << 3,
  Ambitious = 1u << 4,
  Kind = 1u << 5,
  Curious = 1u << 6,
};

struct DeathRecord {
  int day = 0;
  int humanId = 0;
  DeathReason reason = DeathReason::Starvation;
};

struct DeathSummary {
  int starvation = 0;
  int dehydration = 0;
  int oldAge = 0;
  int war = 0;
  int macroNatural = 0;
  int macroStarvation = 0;
  int macroFire = 0;
};

struct ArrowProjectile {
  float x = 0.0f;     // tile-space (e.g. human.x + 0.5)
  float y = 0.0f;
  float prevX = 0.0f;
  float prevY = 0.0f;
  float vx = 0.0f;    // tile-space units / second
  float vy = 0.0f;
  float ttlSeconds = 0.0f;
  int targetId = -1;
  int shooterFactionId = -1;
};

struct Human {
  static constexpr int kDaysPerYear = 360;
  static constexpr int kAdultAgeDays = 18 * kDaysPerYear;

  int id = 0;
  bool female = false;
  int ageDays = 0;
  int x = 0;
  int y = 0;
  bool alive = true;
  bool pregnant = false;
  int gestationDays = 0;
  int nutrition = 100;  // 0..100
  float nutritionMonthAccumulator = 0.0f;
  int maxHealth = 100;
  int health = 100;
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
  uint16_t traits = 0;
  bool legendary = false;
  uint8_t legendPower = 0;
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
  BuildingType taskBuildType = BuildingType::None;
  bool carrying = false;
  int carryFood = 0;
  int carryWood = 0;

  ArmyState armyState = ArmyState::Idle;
  int warId = -1;
  int warTargetSettlementId = -1;
  int formationSlot = 0;
  bool isGeneral = false;

  float meleeCooldownSeconds = 0.0f;
  float bowCooldownSeconds = 0.0f;
  int bowTargetId = -1;
};

class HumanManager {
 public:
  HumanManager();

  void Spawn(int x, int y, bool female, Random& rng);
  void UpdateTick(World& world, SettlementManager& settlements, Random& rng, int tickCount,
                  float tickSeconds, int ticksPerDay);
  void UpdateDailyCoarse(World& world, SettlementManager& settlements, Random& rng, int dayCount,
                         int dayDelta, int& birthsToday, int& deathsToday);
  void EnterMacro(SettlementManager& settlements);
  void ExitMacro(SettlementManager& settlements, Random& rng);
  void AdvanceMacro(World& world, SettlementManager& settlements, Random& rng, int days,
                    int& birthsToday, int& deathsToday);
  int MacroPopulation(const SettlementManager& settlements) const;
  void UpdateAnimation(float dt);
  void MarkDeadByIndex(int index, int day, DeathReason reason);
  void RecordWarDeaths(int count);
  void SetAllowStarvationDeath(bool enabled) { allowStarvationDeath_ = enabled; }

  int CountAlive() const;
  const std::vector<Human>& Humans() const { return humans_; }
  std::vector<Human>& HumansMutable() { return humans_; }
  const std::vector<ArrowProjectile>& Arrows() const { return arrows_; }
  const std::vector<DeathRecord>& DeathLog() const { return deathLog_; }
  const DeathSummary& GetDeathSummary() const { return deathSummary_; }

 private:
  Human CreateHuman(int x, int y, bool female, Random& rng, int ageDays);
  void ReplanGoal(Human& human, const World& world, const SettlementManager& settlements,
                  Random& rng, int tickCount, int ticksPerDay);
  bool GetHumanById(int id, int& outX, int& outY) const;
  int FindMateTargetId(const Human& human, const World& world, Random& rng) const;
  void UpdateMoveStep(Human& human, World& world, SettlementManager& settlements, Random& rng,
                      int tickCount, int ticksPerDay);
  void RebuildIdMap();
  void RecordDeath(int humanId, int day, DeathReason reason);

  static uint64_t PackCoord(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(y));
  }

  int PopCountAt(int x, int y) const;
  int AdultMaleCountAt(int x, int y) const;
  int AdultMaleSampleIdAt(int x, int y) const;
  void EnsureCrowdGrids(int w, int h);

  int nextId_ = 1;
  std::vector<Human> humans_;
  std::vector<ArrowProjectile> arrows_;
  int crowdGridW_ = 0;
  int crowdGridH_ = 0;
  uint32_t crowdGeneration_ = 1;
  std::vector<uint32_t> popStampByTile_;
  std::vector<int> popCountByTile_;
  std::vector<uint32_t> adultMaleStampByTile_;
  std::vector<int> adultMaleCountByTile_;
  std::vector<int> adultMaleSampleIdByTile_;
  uint32_t soldierGridGeneration_ = 1;
  std::vector<uint32_t> soldierStampByTile_;
  std::vector<uint16_t> soldierCountByTile_;
  std::vector<int> soldierSampleIdByTile_;
  uint32_t unitGridGeneration_ = 1;
  std::vector<uint32_t> unitStampByTile_;
  std::vector<uint16_t> unitCountByTile_;
  std::vector<int> unitSampleIdByTile_;
  std::vector<int> humanIdToIndex_;
  std::vector<Human> newborns_;
  std::vector<DeathRecord> deathLog_;
  DeathSummary deathSummary_;
  int thinkCursor_ = 0;
  int currentDay_ = 0;
  bool macroActive_ = false;
  int macroFallbackM_[6] = {};
  int macroFallbackF_[6] = {};
  float macroFallbackBirthAccum_ = 0.0f;
  int macroFallbackX_ = 0;
  int macroFallbackY_ = 0;
  bool macroHasFallback_ = false;
  bool allowStarvationDeath_ = true;
};

const char* DeathReasonName(DeathReason reason);
const char* ArmyStateName(ArmyState state);
const char* HumanTraitName(HumanTrait trait);
bool HumanHasTrait(uint16_t traits, HumanTrait trait);
void HumanTraitsToString(char* buffer, size_t size, uint16_t traits, bool legendary);
