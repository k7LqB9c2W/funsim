#include "humans.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "settlements.h"

namespace {
constexpr int kAdultAgeDays = 18 * 365;
constexpr int kGestationDays = 90;
constexpr int kCrowdPenalty = 25;
constexpr int kMateRadius = 4;
constexpr int kMateCooldownDays = 30;
constexpr float kMateBaseChance = 0.01f;
constexpr float kMatePerMaleChance = 0.02f;
constexpr float kMateMaxChance = 0.25f;
constexpr float kStepsPerDay = 8.0f;
constexpr int kBlockedReplanTicks = 8;
constexpr int kFoodGraceDays = 21;
constexpr int kFoodMaxDays = 35;
constexpr int kWaterGraceDays = 3;
constexpr int kWaterMaxDays = 7;

constexpr int kMacroBins = 6;
constexpr int kMacroBinDays[kMacroBins] = {365, 4 * 365, 13 * 365, 22 * 365, 20 * 365, 200 * 365};
constexpr float kMacroDeathRate[kMacroBins] = {0.0020f, 0.0003f, 0.00008f, 0.00012f, 0.0006f, 0.0025f};
constexpr float kMacroBirthRatePerDay = 0.0014f;

bool IsWalkable(const Tile& tile) {
  return tile.type != TileType::Ocean;
}

int Manhattan(int ax, int ay, int bx, int by) {
  return std::abs(ax - bx) + std::abs(ay - by);
}

uint32_t HashNoise(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  uint32_t h = a * 0x9E3779B9u;
  h ^= b * 0x85EBCA6Bu;
  h ^= c * 0xC2B2AE35u;
  h ^= d * 0x27D4EB2Fu;
  h ^= (h >> 13);
  h *= 0x165667B1u;
  h ^= (h >> 16);
  return h;
}

int ClampInt(int value, int min_value, int max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}

int AgeBinIndex(int ageDays) {
  int remaining = ageDays;
  for (int i = 0; i < kMacroBins - 1; ++i) {
    if (remaining < kMacroBinDays[i]) return i;
    remaining -= kMacroBinDays[i];
  }
  return kMacroBins - 1;
}

int ApplyRate(int count, float rate, Random& rng) {
  if (count <= 0 || rate <= 0.0f) return 0;
  float expected = static_cast<float>(count) * rate;
  int whole = static_cast<int>(expected);
  float frac = expected - static_cast<float>(whole);
  int extra = rng.Chance(frac) ? 1 : 0;
  int total = whole + extra;
  if (total > count) total = count;
  return total;
}
}  // namespace

HumanManager::HumanManager() {
  newborns_.reserve(32);
}

Human HumanManager::CreateHuman(int x, int y, bool female, Random& rng, int ageDays) {
  Human human;
  human.id = nextId_++;
  human.female = female;
  human.ageDays = ageDays;
  human.x = x;
  human.y = y;
  human.alive = true;
  human.pregnant = false;
  human.gestationDays = 0;
  human.daysWithoutFood = 0;
  human.daysWithoutWater = 0;
  human.animTimer = 0.0f;
  human.animFrame = 0;
  human.moving = false;
  human.goal = Goal::Wander;
  human.role = Role::Idle;
  human.targetX = x;
  human.targetY = y;
  human.homeX = x;
  human.homeY = y;
  human.lastFoodX = x;
  human.lastFoodY = y;
  human.lastWaterX = x;
  human.lastWaterY = y;
  human.rethinkCooldownTicks = 0;
  human.mateCooldownDays = 0;
  human.settlementId = -1;
  human.bravery = static_cast<uint8_t>(rng.RangeInt(0, 255));
  human.greed = static_cast<uint8_t>(rng.RangeInt(0, 255));
  human.wanderlust = static_cast<uint8_t>(rng.RangeInt(0, 255));
  human.parentIdMother = -1;
  human.parentIdFather = -1;
  human.moveAccum = 0.0f;
  human.blockedTicks = 0;
  human.forceReplan = false;
  human.mateTargetId = -1;
  human.hasTask = false;
  human.taskX = x;
  human.taskY = y;
  human.taskAmount = 0;
  human.taskSettlementId = -1;
  human.carrying = false;
  human.carryFood = 0;
  return human;
}

void HumanManager::Spawn(int x, int y, bool female, Random& rng) {
  humans_.push_back(CreateHuman(x, y, female, rng, kAdultAgeDays));
}

void HumanManager::RebuildIdMap() {
  humanIdToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(humans_.size()); ++i) {
    humanIdToIndex_[humans_[i].id] = i;
  }
}

bool HumanManager::GetHumanById(int id, int& outX, int& outY) const {
  if (id <= 0 || id >= static_cast<int>(humanIdToIndex_.size())) return false;
  int idx = humanIdToIndex_[id];
  if (idx < 0 || idx >= static_cast<int>(humans_.size())) return false;
  const Human& human = humans_[idx];
  if (!human.alive) return false;
  outX = human.x;
  outY = human.y;
  return true;
}

int HumanManager::FindMateTargetId(const Human& human, const World& world, Random& rng) const {
  if (adultMaleCounts_.empty() || adultMaleSampleId_.empty()) return -1;
  const int w = world.width();
  const int h = world.height();
  int total = 0;
  int selected = -1;

  for (int dy = -kMateRadius; dy <= kMateRadius; ++dy) {
    int ny = human.y + dy;
    if (ny < 0 || ny >= h) continue;
    for (int dx = -kMateRadius; dx <= kMateRadius; ++dx) {
      int nx = human.x + dx;
      if (nx < 0 || nx >= w) continue;
      int idx = ny * w + nx;
      int count = adultMaleCounts_[idx];
      if (count <= 0) continue;
      total += count;
      int sampleId = adultMaleSampleId_[idx];
      if (sampleId != -1 && rng.RangeInt(0, total - 1) < count) {
        selected = sampleId;
      }
    }
  }

  return selected;
}

void HumanManager::ReplanGoal(Human& human, const World& world, const SettlementManager& settlements,
                              Random& rng, int tickCount, int ticksPerDay) {
  if (!human.alive) return;

  if (human.hasTask) {
    if (human.taskType == TaskType::CollectFood) {
      human.goal = Goal::SeekFood;
      human.targetX = human.taskX;
      human.targetY = human.taskY;
    } else if (human.taskType == TaskType::HaulToStockpile) {
      human.goal = Goal::StayHome;
      human.targetX = human.taskX;
      human.targetY = human.taskY;
    } else if (human.taskType == TaskType::PatrolEdge) {
      human.goal = Goal::Wander;
      human.targetX = human.taskX;
      human.targetY = human.taskY;
    }
  } else {
    bool nearFire = world.At(human.x, human.y).burning;
    if (!nearFire) {
      const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& d : dirs) {
        int nx = human.x + d[0];
        int ny = human.y + d[1];
        if (!world.InBounds(nx, ny)) continue;
        if (world.At(nx, ny).burning) {
          nearFire = true;
          break;
        }
      }
    }

    bool thirsty = human.daysWithoutWater >= 2;
    bool hungry = human.daysWithoutFood >= 2;
    bool adult = human.ageDays >= kAdultAgeDays;

    if (nearFire) {
      human.goal = Goal::FleeFire;
      human.mateTargetId = -1;
    } else if (thirsty) {
      human.goal = Goal::SeekWater;
      human.mateTargetId = -1;
    } else if (hungry) {
      human.goal = Goal::SeekFood;
      human.mateTargetId = -1;
    } else {
      bool canMate = adult && human.female && !human.pregnant && human.mateCooldownDays == 0 &&
                     human.daysWithoutFood <= 4 && human.daysWithoutWater <= 4;
      if (canMate && human.settlementId != -1) {
        const Settlement* settlement = settlements.Get(human.settlementId);
        if (!settlement || settlement->stockFood < settlement->population * 3) {
          canMate = false;
        }
      }
      if (canMate) {
        int mateId = FindMateTargetId(human, world, rng);
        if (mateId != -1) {
          human.goal = Goal::SeekMate;
          human.mateTargetId = mateId;
        } else {
          canMate = false;
        }
      }
      if (!canMate) {
        if (human.settlementId != -1 &&
            (human.wanderlust < 100 || human.role == Role::Guard || human.role == Role::Builder)) {
          human.goal = Goal::StayHome;
        } else {
          human.goal = Goal::Wander;
        }
        human.mateTargetId = -1;
      }
    }
  }

  if (human.goal == Goal::StayHome) {
    human.targetX = human.homeX;
    human.targetY = human.homeY;
  } else if (human.goal == Goal::Wander) {
    uint32_t hash = HashNoise(static_cast<uint32_t>(human.id),
                              static_cast<uint32_t>(tickCount), 0xA5u, 0x5Au);
    int radius = 10;
    int dx = static_cast<int>(hash % (radius * 2 + 1)) - radius;
    int dy = static_cast<int>((hash >> 8) % (radius * 2 + 1)) - radius;
    int tx = ClampInt(human.x + dx, 0, world.width() - 1);
    int ty = ClampInt(human.y + dy, 0, world.height() - 1);
    if (world.At(tx, ty).type == TileType::Ocean) {
      tx = human.x;
      ty = human.y;
    }
    human.targetX = tx;
    human.targetY = ty;
  }

  int minTicks = ticksPerDay / 2;
  int maxTicks = ticksPerDay * 2;
  if (human.daysWithoutFood >= 2 || human.daysWithoutWater >= 2) {
    minTicks = ticksPerDay / 3;
    maxTicks = ticksPerDay;
  }
  minTicks = std::max(1, minTicks);
  maxTicks = std::max(minTicks, maxTicks);
  human.rethinkCooldownTicks = rng.RangeInt(minTicks, maxTicks);
}

void HumanManager::UpdateMoveStep(Human& human, World& world, SettlementManager& settlements,
                                  Random& rng, int tickCount, int ticksPerDay) {
  if (!human.alive) return;

  if (static_cast<unsigned>(human.x) >= static_cast<unsigned>(world.width()) ||
      static_cast<unsigned>(human.y) >= static_cast<unsigned>(world.height())) {
    human.x = ClampInt(human.x, 0, world.width() - 1);
    human.y = ClampInt(human.y, 0, world.height() - 1);
  }

  bool nearFire = world.At(human.x, human.y).burning;
  if (!nearFire) {
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : dirs) {
      int nx = human.x + d[0];
      int ny = human.y + d[1];
      if (!world.InBounds(nx, ny)) continue;
      if (world.At(nx, ny).burning) {
        nearFire = true;
        break;
      }
    }
  }

  bool thirsty = human.daysWithoutWater >= 2;
  bool hungry = human.daysWithoutFood >= 2;
  bool emergency = nearFire || thirsty || hungry;

  if (nearFire && human.goal != Goal::FleeFire) {
    human.goal = Goal::FleeFire;
    human.hasTask = false;
    human.mateTargetId = -1;
  } else if (thirsty && human.goal != Goal::SeekWater) {
    human.goal = Goal::SeekWater;
    human.hasTask = false;
    human.mateTargetId = -1;
  } else if (hungry && human.goal != Goal::SeekFood) {
    human.goal = Goal::SeekFood;
  }

  if (human.goal == Goal::SeekMate && human.mateTargetId != -1) {
    int mx = 0;
    int my = 0;
    if (!GetHumanById(human.mateTargetId, mx, my)) {
      human.mateTargetId = -1;
      human.forceReplan = true;
    }
  }

  if (!emergency && !human.hasTask && human.settlementId != -1) {
    Settlement* settlement = settlements.GetMutable(human.settlementId);
    if (settlement) {
      Task task;
      if (settlement->PopTask(task)) {
        human.hasTask = true;
        human.taskType = task.type;
        human.taskX = task.x;
        human.taskY = task.y;
        human.taskAmount = task.amount;
        human.taskSettlementId = task.settlementId;
        if (task.type == TaskType::CollectFood) {
          human.goal = Goal::SeekFood;
          human.targetX = task.x;
          human.targetY = task.y;
        } else if (task.type == TaskType::PatrolEdge) {
          human.goal = Goal::Wander;
          human.targetX = task.x;
          human.targetY = task.y;
        }
      }
    }
  }

  const int w = world.width();
  const int h = world.height();
  const int dirs[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  int bestX = human.x;
  int bestY = human.y;
  int bestScore = std::numeric_limits<int>::min();

  int mateX = human.x;
  int mateY = human.y;
  bool haveMateTarget = false;
  if (human.goal == Goal::SeekMate && human.mateTargetId != -1) {
    haveMateTarget = GetHumanById(human.mateTargetId, mateX, mateY);
  }

  for (const auto& d : dirs) {
    int nx = human.x + d[0];
    int ny = human.y + d[1];
    if (!world.InBounds(nx, ny)) continue;
    if ((d[0] != 0 || d[1] != 0) && !IsWalkable(world.At(nx, ny))) continue;

    int idx = ny * w + nx;
    int score = 0;

    if (!popCounts_.empty()) {
      score -= popCounts_[idx] * kCrowdPenalty;
    }
    if (world.At(nx, ny).burning) {
      score -= 200000;
    }

    switch (human.goal) {
      case Goal::SeekFood:
        if (human.hasTask && human.taskType == TaskType::CollectFood) {
          int dist = Manhattan(nx, ny, human.targetX, human.targetY);
          score -= dist * 160;
        } else {
          score += static_cast<int>(world.FoodScentAt(nx, ny)) * 4;
        }
        break;
      case Goal::SeekWater:
        score += static_cast<int>(world.WaterScentAt(nx, ny)) * 4;
        break;
      case Goal::FleeFire:
        score -= static_cast<int>(world.FireRiskAt(nx, ny)) * 6;
        break;
      case Goal::StayHome: {
        int dist = Manhattan(nx, ny, human.homeX, human.homeY);
        score -= dist * 200;
        break;
      }
      case Goal::SeekMate: {
        if (haveMateTarget) {
          int dist = Manhattan(nx, ny, mateX, mateY);
          score -= dist * 180;
        } else {
          int dist = Manhattan(nx, ny, human.targetX, human.targetY);
          score -= dist * 40;
        }
        break;
      }
      case Goal::Wander: {
        int dist = Manhattan(nx, ny, human.targetX, human.targetY);
        score -= dist * 40;
        break;
      }
    }

    if (human.role == Role::Gatherer) {
      score += static_cast<int>(world.FoodScentAt(nx, ny)) / 4;
    } else if (human.role == Role::Guard || human.role == Role::Builder) {
      int dist = Manhattan(nx, ny, human.homeX, human.homeY);
      score -= dist * 40;
    }

    if (human.settlementId != -1) {
      bool wanderHeavy = (human.goal == Goal::Wander || human.role == Role::Gatherer);
      if (!wanderHeavy) {
        float homeBias = 1.0f - (static_cast<float>(human.wanderlust) / 255.0f);
        int homeScore = static_cast<int>(static_cast<float>(world.HomeScentAt(nx, ny)) *
                                         0.015f * homeBias);
        score += homeScore;
      }
    }

    uint32_t noise = HashNoise(static_cast<uint32_t>(human.id),
                               static_cast<uint32_t>(tickCount),
                               static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
    score += static_cast<int>(noise % 200) - 100;

    if (score > bestScore) {
      bestScore = score;
      bestX = nx;
      bestY = ny;
    }
  }

  human.moving = (bestX != human.x || bestY != human.y);
  if (human.moving) {
    human.blockedTicks = 0;
  } else if (human.blockedTicks < 255) {
    human.blockedTicks++;
  }
  human.x = bestX;
  human.y = bestY;

  if (human.blockedTicks >= kBlockedReplanTicks) {
    human.blockedTicks = 0;
    human.forceReplan = true;
  }

  if (human.hasTask) {
    if (human.taskType == TaskType::CollectFood) {
      if (human.x == human.taskX && human.y == human.taskY) {
        Tile& tile = world.At(human.x, human.y);
        if (tile.food > 0) {
          tile.food--;
          human.carryFood++;
          human.carrying = true;
          human.taskType = TaskType::HaulToStockpile;
          human.taskX = human.homeX;
          human.taskY = human.homeY;
          human.taskSettlementId = human.settlementId;
          human.goal = Goal::StayHome;
        } else {
          human.hasTask = false;
          human.forceReplan = true;
        }
      }
    } else if (human.taskType == TaskType::HaulToStockpile) {
      if (human.x == human.taskX && human.y == human.taskY) {
        Settlement* settlement = settlements.GetMutable(human.taskSettlementId);
        if (settlement && human.carryFood > 0) {
          settlement->stockFood += human.carryFood;
        }
        human.carryFood = 0;
        human.carrying = false;
        human.hasTask = false;
        human.forceReplan = true;
      }
    } else if (human.taskType == TaskType::PatrolEdge) {
      if (human.x == human.taskX && human.y == human.taskY) {
        human.hasTask = false;
        human.forceReplan = true;
      }
    }
  }

  if (human.forceReplan) {
    ReplanGoal(human, world, settlements, rng, tickCount, ticksPerDay);
    human.forceReplan = false;
  }
}

void HumanManager::UpdateTick(World& world, SettlementManager& settlements, Random& rng,
                              int tickCount, float tickSeconds, int ticksPerDay) {
  (void)tickSeconds;
  if (macroActive_) return;
  if (humans_.empty()) return;

  CrashContextSetStage("Humans::UpdateTick");
  const float stepsPerTick = kStepsPerDay / static_cast<float>(ticksPerDay);

  int thinkBudget = ClampInt(static_cast<int>(humans_.size() / 500), 200, 5000);
  for (int i = 0; i < thinkBudget && !humans_.empty(); ++i) {
    if (thinkCursor_ >= static_cast<int>(humans_.size())) thinkCursor_ = 0;
    Human& human = humans_[thinkCursor_++];
    if (!human.alive) continue;
    if (human.forceReplan || human.rethinkCooldownTicks <= 0) {
      ReplanGoal(human, world, settlements, rng, tickCount, ticksPerDay);
      human.forceReplan = false;
    }
  }

  for (auto& human : humans_) {
    if (!human.alive) continue;
    CrashContextSetHuman(human.id, human.x, human.y);

    if (human.rethinkCooldownTicks > 0) {
      human.rethinkCooldownTicks--;
    }

    human.moveAccum += stepsPerTick;
    int steps = 0;
    while (human.moveAccum >= 1.0f && steps < 4) {
      human.moveAccum -= 1.0f;
      UpdateMoveStep(human, world, settlements, rng, tickCount, ticksPerDay);
      steps++;
    }
  }
}

void HumanManager::UpdateDailyCoarse(World& world, SettlementManager& settlements, Random& rng,
                                     int dayCount, int& birthsToday, int& deathsToday) {
  if (macroActive_) return;
  (void)dayCount;
  CrashContextSetStage("Humans::UpdateDailyCoarse begin");
  CrashContextSetPopulation(static_cast<int>(humans_.size()));
  birthsToday = 0;
  deathsToday = 0;

  const int w = world.width();
  const int h = world.height();
  const int size = w * h;

  if (static_cast<int>(adultMaleCounts_.size()) != size) {
    adultMaleCounts_.assign(size, 0);
    adultMaleSampleId_.assign(size, -1);
  } else {
    std::fill(adultMaleCounts_.begin(), adultMaleCounts_.end(), 0);
    std::fill(adultMaleSampleId_.begin(), adultMaleSampleId_.end(), -1);
  }

  if (static_cast<int>(popCounts_.size()) != size) {
    popCounts_.assign(size, 0);
  } else {
    std::fill(popCounts_.begin(), popCounts_.end(), 0);
  }

  CrashContextSetStage("Humans::UpdateDailyCoarse count");
  for (const auto& human : humans_) {
    if (!human.alive) continue;
    if (static_cast<unsigned>(human.x) >= static_cast<unsigned>(w) ||
        static_cast<unsigned>(human.y) >= static_cast<unsigned>(h)) {
      continue;
    }
    int idx = human.y * w + human.x;
    popCounts_[idx]++;
    if (!human.female && human.ageDays >= kAdultAgeDays) {
      int count = ++adultMaleCounts_[idx];
      if (adultMaleSampleId_[idx] == -1 || rng.RangeInt(0, count - 1) == 0) {
        adultMaleSampleId_[idx] = human.id;
      }
    }
  }

  newborns_.clear();

  CrashContextSetStage("Humans::UpdateDailyCoarse loop");
  for (auto& human : humans_) {
    if (!human.alive) continue;
    CrashContextSetHuman(human.id, human.x, human.y);

    if (static_cast<unsigned>(human.x) >= static_cast<unsigned>(w) ||
        static_cast<unsigned>(human.y) >= static_cast<unsigned>(h)) {
      human.x = ClampInt(human.x, 0, w - 1);
      human.y = ClampInt(human.y, 0, h - 1);
    }

    human.ageDays++;
    if (human.mateCooldownDays > 0) {
      human.mateCooldownDays--;
    }

    if (human.pregnant) {
      human.gestationDays++;
      if (human.gestationDays >= kGestationDays) {
        human.pregnant = false;
        human.gestationDays = 0;
        bool babyFemale = rng.Chance(0.5f);
        Human baby = CreateHuman(human.x, human.y, babyFemale, rng, 0);
        baby.parentIdMother = human.id;
        newborns_.push_back(baby);
        birthsToday++;
        if (human.settlementId != -1) {
          Settlement* settlement = settlements.GetMutable(human.settlementId);
          if (settlement && settlement->stockFood > 0) {
            settlement->stockFood = std::max(0, settlement->stockFood - 2);
          }
        }
      }
    }

    Settlement* settlement = nullptr;
    if (human.settlementId != -1) {
      settlement = settlements.GetMutable(human.settlementId);
    }

    Tile& tile = world.At(human.x, human.y);
    if (settlement && settlement->stockFood > 0) {
      settlement->stockFood--;
      human.daysWithoutFood = 0;
      human.lastFoodX = human.x;
      human.lastFoodY = human.y;
    } else if (tile.food > 0) {
      tile.food--;
      human.daysWithoutFood = 0;
      human.lastFoodX = human.x;
      human.lastFoodY = human.y;
    } else {
      human.daysWithoutFood++;
    }

    if (tile.type == TileType::FreshWater) {
      human.daysWithoutWater = 0;
      human.lastWaterX = human.x;
      human.lastWaterY = human.y;
    } else if (settlement) {
      int dist = Manhattan(human.x, human.y, human.homeX, human.homeY);
      if (world.WaterScentAt(human.homeX, human.homeY) > 20000 && dist <= 12) {
        human.daysWithoutWater = 0;
        human.lastWaterX = human.homeX;
        human.lastWaterY = human.homeY;
      } else {
        human.daysWithoutWater++;
      }
    } else {
      human.daysWithoutWater++;
    }

    bool adult = human.ageDays >= kAdultAgeDays;
    if (human.female && adult && !human.pregnant && human.mateCooldownDays == 0 &&
        human.daysWithoutFood <= 4 && human.daysWithoutWater <= 4) {
      bool canMate = true;
      if (settlement && settlement->stockFood < settlement->population * 3) {
        canMate = false;
      }
      if (canMate) {
        int maleCount = 0;
        for (int dy = -kMateRadius; dy <= kMateRadius; ++dy) {
          for (int dx = -kMateRadius; dx <= kMateRadius; ++dx) {
            int mx = human.x + dx;
            int my = human.y + dy;
            if (mx < 0 || my < 0 || mx >= w || my >= h) continue;
            maleCount += adultMaleCounts_[my * w + mx];
          }
        }
        if (maleCount > 0) {
          float chance = kMateBaseChance + static_cast<float>(maleCount) * kMatePerMaleChance;
          if (chance > kMateMaxChance) chance = kMateMaxChance;
          if (rng.Chance(chance)) {
            human.pregnant = true;
            human.gestationDays = 0;
            human.mateCooldownDays = kMateCooldownDays;
          }
        }
      }
    }

    if (human.daysWithoutFood > kFoodGraceDays) {
      if (human.daysWithoutFood >= kFoodMaxDays) {
        human.alive = false;
        deathsToday++;
        continue;
      }
      float chance = static_cast<float>(human.daysWithoutFood - kFoodGraceDays) /
                     static_cast<float>(kFoodMaxDays - kFoodGraceDays);
      if (rng.Chance(chance)) {
        human.alive = false;
        deathsToday++;
        continue;
      }
    }

    if (human.daysWithoutWater > kWaterGraceDays) {
      if (human.daysWithoutWater >= kWaterMaxDays) {
        human.alive = false;
        deathsToday++;
        continue;
      }
      float chance = static_cast<float>(human.daysWithoutWater - kWaterGraceDays) /
                     static_cast<float>(kWaterMaxDays - kWaterGraceDays);
      if (rng.Chance(chance)) {
        human.alive = false;
        deathsToday++;
        continue;
      }
    }
  }

  if (!newborns_.empty()) {
    humans_.insert(humans_.end(), newborns_.begin(), newborns_.end());
  }

  size_t write = 0;
  for (size_t read = 0; read < humans_.size(); ++read) {
    if (!humans_[read].alive) continue;
    if (write != read) {
      humans_[write] = humans_[read];
    }
    write++;
  }
  humans_.resize(write);

  RebuildIdMap();
}

void HumanManager::EnterMacro(SettlementManager& settlements) {
  if (macroActive_) return;
  macroActive_ = true;

  auto& list = settlements.SettlementsMutable();
  for (auto& settlement : list) {
    settlement.ClearMacroPools();
  }
  std::fill(std::begin(macroFallbackM_), std::end(macroFallbackM_), 0);
  std::fill(std::begin(macroFallbackF_), std::end(macroFallbackF_), 0);
  macroFallbackBirthAccum_ = 0.0f;
  macroHasFallback_ = false;
  long long fallbackSumX = 0;
  long long fallbackSumY = 0;
  int fallbackCount = 0;

  for (const auto& human : humans_) {
    int bin = AgeBinIndex(human.ageDays);
    if (human.settlementId != -1) {
      Settlement* settlement = settlements.GetMutable(human.settlementId);
      if (!settlement) continue;
      if (human.female) {
        settlement->macroPopF[bin]++;
      } else {
        settlement->macroPopM[bin]++;
      }
    } else if (!list.empty()) {
      int bestIdx = -1;
      int bestDist = std::numeric_limits<int>::max();
      for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const Settlement& settlement = list[i];
        int dist = Manhattan(human.x, human.y, settlement.centerX, settlement.centerY);
        if (dist < bestDist) {
          bestDist = dist;
          bestIdx = i;
        }
      }
      if (bestIdx >= 0) {
        if (human.female) {
          list[bestIdx].macroPopF[bin]++;
        } else {
          list[bestIdx].macroPopM[bin]++;
        }
      }
    } else {
      if (human.female) {
        macroFallbackF_[bin]++;
      } else {
        macroFallbackM_[bin]++;
      }
      fallbackSumX += human.x;
      fallbackSumY += human.y;
      fallbackCount++;
    }
  }
  if (fallbackCount > 0) {
    macroHasFallback_ = true;
    macroFallbackX_ = static_cast<int>(fallbackSumX / fallbackCount);
    macroFallbackY_ = static_cast<int>(fallbackSumY / fallbackCount);
  }

  humans_.clear();
  newborns_.clear();
  humanIdToIndex_.clear();
}

void HumanManager::ExitMacro(SettlementManager& settlements, Random& rng) {
  if (!macroActive_) return;
  macroActive_ = false;

  humans_.clear();
  newborns_.clear();
  nextId_ = 1;

  for (auto& settlement : settlements.SettlementsMutable()) {
    int total = settlement.MacroTotal();
    if (total <= 0) {
      settlement.ClearMacroPools();
      continue;
    }

    for (int bin = 0; bin < kMacroBins; ++bin) {
      for (int i = 0; i < settlement.macroPopM[bin]; ++i) {
        int ageDays = 0;
        for (int b = 0; b < bin; ++b) {
          ageDays += kMacroBinDays[b];
        }
        ageDays += rng.RangeInt(0, kMacroBinDays[bin] - 1);
        Human human = CreateHuman(settlement.centerX, settlement.centerY, false, rng, ageDays);
        human.settlementId = settlement.id;
        human.homeX = settlement.centerX;
        human.homeY = settlement.centerY;
        humans_.push_back(human);
      }
      for (int i = 0; i < settlement.macroPopF[bin]; ++i) {
        int ageDays = 0;
        for (int b = 0; b < bin; ++b) {
          ageDays += kMacroBinDays[b];
        }
        ageDays += rng.RangeInt(0, kMacroBinDays[bin] - 1);
        Human human = CreateHuman(settlement.centerX, settlement.centerY, true, rng, ageDays);
        human.settlementId = settlement.id;
        human.homeX = settlement.centerX;
        human.homeY = settlement.centerY;
        humans_.push_back(human);
      }
    }
    settlement.ClearMacroPools();
  }

  if (macroHasFallback_) {
    for (int bin = 0; bin < kMacroBins; ++bin) {
      for (int i = 0; i < macroFallbackM_[bin]; ++i) {
        int ageDays = 0;
        for (int b = 0; b < bin; ++b) {
          ageDays += kMacroBinDays[b];
        }
        ageDays += rng.RangeInt(0, kMacroBinDays[bin] - 1);
        Human human = CreateHuman(macroFallbackX_, macroFallbackY_, false, rng, ageDays);
        humans_.push_back(human);
      }
      for (int i = 0; i < macroFallbackF_[bin]; ++i) {
        int ageDays = 0;
        for (int b = 0; b < bin; ++b) {
          ageDays += kMacroBinDays[b];
        }
        ageDays += rng.RangeInt(0, kMacroBinDays[bin] - 1);
        Human human = CreateHuman(macroFallbackX_, macroFallbackY_, true, rng, ageDays);
        humans_.push_back(human);
      }
    }
  }
  std::fill(std::begin(macroFallbackM_), std::end(macroFallbackM_), 0);
  std::fill(std::begin(macroFallbackF_), std::end(macroFallbackF_), 0);
  macroFallbackBirthAccum_ = 0.0f;
  macroHasFallback_ = false;

  RebuildIdMap();
}

void HumanManager::AdvanceMacro(World& world, SettlementManager& settlements, Random& rng,
                                int days, int& birthsToday, int& deathsToday) {
  if (!macroActive_) return;
  if (days <= 0) return;

  for (int day = 0; day < days; ++day) {
    for (auto& settlement : settlements.SettlementsMutable()) {
      int popTotal = settlement.MacroTotal();
      settlement.population = popTotal;

      int need = popTotal;
      if (settlement.stockFood > 0) {
        settlement.stockFood = std::max(0, settlement.stockFood - need);
      }

      float foodFactor = 0.0f;
      if (popTotal > 0) {
        foodFactor = static_cast<float>(settlement.stockFood) /
                     static_cast<float>(std::max(1, popTotal * 3));
        if (foodFactor > 1.0f) foodFactor = 1.0f;
      }

      float waterFactor = static_cast<float>(world.WaterScentAt(settlement.centerX,
                                                                settlement.centerY)) /
                          60000.0f;
      if (waterFactor > 1.0f) waterFactor = 1.0f;

      int fertileFemales = settlement.macroPopF[3];
      int adultMales = settlement.macroPopM[3] + settlement.macroPopM[4];
      int mates = std::min(fertileFemales, adultMales);
      float expectedBirths = static_cast<float>(mates) * kMacroBirthRatePerDay * foodFactor *
                             waterFactor;
      settlement.macroBirthAccum += expectedBirths;
      int births = static_cast<int>(settlement.macroBirthAccum);
      settlement.macroBirthAccum -= static_cast<float>(births);
      if (rng.Chance(settlement.macroBirthAccum)) {
        births++;
        settlement.macroBirthAccum = 0.0f;
      }

      int femaleBirths = births / 2;
      int maleBirths = births - femaleBirths;
      if (rng.Chance(0.5f)) {
        std::swap(femaleBirths, maleBirths);
      }
      settlement.macroPopF[0] += femaleBirths;
      settlement.macroPopM[0] += maleBirths;
      birthsToday += births;

      float fireFactor = static_cast<float>(world.FireRiskAt(settlement.centerX,
                                                             settlement.centerY)) /
                         60000.0f;
      float starvationRate = (settlement.stockFood == 0) ? 0.002f : 0.0f;
      for (int bin = 0; bin < kMacroBins; ++bin) {
        int baseDeathsM = ApplyRate(settlement.macroPopM[bin], kMacroDeathRate[bin], rng);
        int baseDeathsF = ApplyRate(settlement.macroPopF[bin], kMacroDeathRate[bin], rng);
        int extraDeathsM = 0;
        int extraDeathsF = 0;
        if (starvationRate > 0.0f && (bin == 0 || bin == kMacroBins - 1)) {
          extraDeathsM += ApplyRate(settlement.macroPopM[bin], starvationRate, rng);
          extraDeathsF += ApplyRate(settlement.macroPopF[bin], starvationRate, rng);
        }
        if (fireFactor > 0.2f) {
          float fireRate = fireFactor * 0.0008f;
          extraDeathsM += ApplyRate(settlement.macroPopM[bin], fireRate, rng);
          extraDeathsF += ApplyRate(settlement.macroPopF[bin], fireRate, rng);
        }

        settlement.macroPopM[bin] =
            std::max(0, settlement.macroPopM[bin] - baseDeathsM - extraDeathsM);
        settlement.macroPopF[bin] =
            std::max(0, settlement.macroPopF[bin] - baseDeathsF - extraDeathsF);
        deathsToday += baseDeathsM + baseDeathsF + extraDeathsM + extraDeathsF;
      }

      for (int bin = 0; bin < kMacroBins - 1; ++bin) {
        int moveM = ApplyRate(settlement.macroPopM[bin], 1.0f / kMacroBinDays[bin], rng);
        int moveF = ApplyRate(settlement.macroPopF[bin], 1.0f / kMacroBinDays[bin], rng);
        settlement.macroPopM[bin] -= moveM;
        settlement.macroPopF[bin] -= moveF;
        settlement.macroPopM[bin + 1] += moveM;
        settlement.macroPopF[bin + 1] += moveF;
      }

      settlement.population = settlement.MacroTotal();
      settlement.ageDays++;
    }

    if (macroHasFallback_) {
      int popTotal = 0;
      for (int bin = 0; bin < kMacroBins; ++bin) {
        popTotal += macroFallbackM_[bin] + macroFallbackF_[bin];
      }
      if (popTotal > 0) {
        float foodFactor = 0.1f;
        float waterFactor = static_cast<float>(world.WaterScentAt(macroFallbackX_,
                                                                  macroFallbackY_)) /
                            60000.0f;
        if (waterFactor > 1.0f) waterFactor = 1.0f;

        int fertileFemales = macroFallbackF_[3];
        int adultMales = macroFallbackM_[3] + macroFallbackM_[4];
        int mates = std::min(fertileFemales, adultMales);
        float expectedBirths = static_cast<float>(mates) * kMacroBirthRatePerDay * foodFactor *
                               waterFactor;
        macroFallbackBirthAccum_ += expectedBirths;
        int births = static_cast<int>(macroFallbackBirthAccum_);
        macroFallbackBirthAccum_ -= static_cast<float>(births);
        if (rng.Chance(macroFallbackBirthAccum_)) {
          births++;
          macroFallbackBirthAccum_ = 0.0f;
        }
        int femaleBirths = births / 2;
        int maleBirths = births - femaleBirths;
        if (rng.Chance(0.5f)) {
          std::swap(femaleBirths, maleBirths);
        }
        macroFallbackF_[0] += femaleBirths;
        macroFallbackM_[0] += maleBirths;
        birthsToday += births;

        for (int bin = 0; bin < kMacroBins; ++bin) {
          int deathsM = ApplyRate(macroFallbackM_[bin], kMacroDeathRate[bin] + 0.0015f, rng);
          int deathsF = ApplyRate(macroFallbackF_[bin], kMacroDeathRate[bin] + 0.0015f, rng);
          macroFallbackM_[bin] = std::max(0, macroFallbackM_[bin] - deathsM);
          macroFallbackF_[bin] = std::max(0, macroFallbackF_[bin] - deathsF);
          deathsToday += deathsM + deathsF;
        }

        for (int bin = 0; bin < kMacroBins - 1; ++bin) {
          int moveM = ApplyRate(macroFallbackM_[bin], 1.0f / kMacroBinDays[bin], rng);
          int moveF = ApplyRate(macroFallbackF_[bin], 1.0f / kMacroBinDays[bin], rng);
          macroFallbackM_[bin] -= moveM;
          macroFallbackF_[bin] -= moveF;
          macroFallbackM_[bin + 1] += moveM;
          macroFallbackF_[bin + 1] += moveF;
        }
      }
    }
  }
}

void HumanManager::UpdateAnimation(float dt) {
  for (auto& human : humans_) {
    if (!human.alive) continue;
    human.animTimer += dt;
    if (human.animTimer >= 0.35f) {
      human.animTimer -= 0.35f;
      human.animFrame = (human.animFrame + 1) % 2;
    }
  }
}

int HumanManager::CountAlive() const {
  int total = 0;
  for (const auto& human : humans_) {
    if (human.alive) total++;
  }
  return total;
}

int HumanManager::MacroPopulation(const SettlementManager& settlements) const {
  if (!macroActive_) return CountAlive();
  int total = 0;
  for (const auto& settlement : settlements.Settlements()) {
    total += settlement.MacroTotal();
  }
  if (macroHasFallback_) {
    for (int i = 0; i < 6; ++i) {
      total += macroFallbackM_[i] + macroFallbackF_[i];
    }
  }
  return total;
}
