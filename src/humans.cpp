#include "humans.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "settlements.h"

namespace {
constexpr int kGestationDays = 90;
constexpr int kCrowdPenalty = 25;
constexpr float kSeekWaterCrowdPenaltyScale = 0.1f;
constexpr int kMateRadius = 4;
constexpr int kMateCooldownDays = 30;
constexpr float kMateBaseChance = 0.01f;
constexpr float kMatePerMaleChance = 0.02f;
constexpr float kMateMaxChance = 0.25f;
constexpr float kStepsPerDay = 8.0f;
constexpr int kBlockedReplanTicks = 8;
constexpr int kFoodIntervalDays = 3;
constexpr int kFoodGraceDays = 21;
constexpr int kFoodMaxDays = 35;
constexpr int kWaterGraceDays = 3;
constexpr int kWaterMaxDays = 7;
constexpr int kCarryFoodEmergencyDays = 3;
constexpr int kMateFoodReservePerPop = 10;
constexpr int kGranaryDropRadius = 4;
constexpr int kGathererWanderRadius = 18;
constexpr int kScoutWanderRadius = 26;
constexpr int kNoStockFoodSearchRadius = 24;
constexpr int kNoStockFoodSearchSamples = 16;
constexpr int kDaysPerYear = 365;
constexpr int kOldAgeStartDays = 80 * kDaysPerYear;
constexpr int kOldAgeMaxDays = 130 * kDaysPerYear;
constexpr uint16_t kOldAgeMaxDailyChanceQ16 = 1311;  // ~0.02 * 65535

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

uint32_t HashNoise(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

void FormationOffset(int slot, int& outDx, int& outDy) {
  uint32_t h = HashNoise(static_cast<uint32_t>(slot), 0xC3u, 0x5Au, 0x19u);
  int dx = static_cast<int>(h % 7u) - 3;
  int dy = static_cast<int>((h >> 8) % 7u) - 3;
  if (dx == 0 && dy == 0) {
    dx = 1;
  }
  outDx = dx;
  outDy = dy;
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

bool SettlementHasStockFood(const SettlementManager& settlements, const Human& human) {
  if (human.settlementId == -1) return false;
  const Settlement* settlement = settlements.Get(human.settlementId);
  return settlement && settlement->stockFood > 0;
}

bool TryPickNoStockFoodTarget(Human& human, const World& world, const SettlementManager& settlements,
                              Random& rng, int tickCount) {
  if (SettlementHasStockFood(settlements, human)) return false;

  auto isEdibleFoodTile = [&](int x, int y) {
    if (!world.InBounds(x, y)) return false;
    const Tile& tile = world.At(x, y);
    if (tile.type != TileType::Land || tile.burning) return false;
    return tile.food > 0;
  };

  if (isEdibleFoodTile(human.lastFoodX, human.lastFoodY)) {
    human.targetX = human.lastFoodX;
    human.targetY = human.lastFoodY;
    return true;
  }

  int baseX = human.x;
  int baseY = human.y;
  if (human.settlementId != -1) {
    const Settlement* settlement = settlements.Get(human.settlementId);
    if (settlement) {
      baseX = settlement->centerX;
      baseY = settlement->centerY;
    }
  }

  int bestX = -1;
  int bestY = -1;
  int bestScore = std::numeric_limits<int>::min();
  for (int i = 0; i < kNoStockFoodSearchSamples; ++i) {
    int dx = rng.RangeInt(-kNoStockFoodSearchRadius, kNoStockFoodSearchRadius);
    int dy = rng.RangeInt(-kNoStockFoodSearchRadius, kNoStockFoodSearchRadius);
    int x = baseX + dx;
    int y = baseY + dy;
    if (!world.InBounds(x, y)) continue;
    const Tile& tile = world.At(x, y);
    if (tile.type != TileType::Land || tile.burning) continue;
    if (tile.food <= 0) continue;
    int dist = std::abs(dx) + std::abs(dy);
    int noise = static_cast<int>(HashNoise(static_cast<uint32_t>(human.id),
                                           static_cast<uint32_t>(tickCount),
                                           static_cast<uint32_t>(x),
                                           static_cast<uint32_t>(y)) &
                                 0xFFu);
    int score = static_cast<int>(tile.food) * 256 - dist * 8 + noise;
    if (score > bestScore) {
      bestScore = score;
      bestX = x;
      bestY = y;
    }
  }
  if (bestX == -1 || bestY == -1) return false;
  human.targetX = bestX;
  human.targetY = bestY;
  return true;
}

void PickNoStockFoodExploreTarget(Human& human, const World& world, const SettlementManager& settlements,
                                  int tickCount) {
  int baseX = human.x;
  int baseY = human.y;
  if (human.settlementId != -1) {
    const Settlement* settlement = settlements.Get(human.settlementId);
    if (settlement) {
      baseX = settlement->centerX;
      baseY = settlement->centerY;
    }
  }

  uint32_t hash = HashNoise(static_cast<uint32_t>(human.id),
                            static_cast<uint32_t>(tickCount),
                            0xF0u, 0x0Du);
  int radius = kNoStockFoodSearchRadius;
  int dx = static_cast<int>(hash % (radius * 2 + 1)) - radius;
  int dy = static_cast<int>((hash >> 8) % (radius * 2 + 1)) - radius;
  int x = ClampInt(baseX + dx, 0, world.width() - 1);
  int y = ClampInt(baseY + dy, 0, world.height() - 1);
  if (world.At(x, y).type == TileType::Ocean) {
    x = human.x;
    y = human.y;
  }
  human.targetX = x;
  human.targetY = y;
}

uint16_t OldAgeDailyChanceQ16(int ageDays, bool legendary) {
  if (ageDays < kOldAgeStartDays) return 0;
  if (ageDays >= kOldAgeMaxDays) return 65535u;
  const uint32_t range = static_cast<uint32_t>(kOldAgeMaxDays - kOldAgeStartDays);
  const uint32_t t = static_cast<uint32_t>(ageDays - kOldAgeStartDays);
  const uint32_t fQ16 = (t << 16) / range;
  const uint32_t f2Q16 = (fQ16 * fQ16) >> 16;
  const uint32_t f4Q16 = (f2Q16 * f2Q16) >> 16;
  const uint32_t f6Q16 = (f4Q16 * f2Q16) >> 16;
  uint32_t chanceQ16 = (f6Q16 * static_cast<uint32_t>(kOldAgeMaxDailyChanceQ16)) >> 16;
  if (legendary) {
    chanceQ16 = (chanceQ16 * 3u) / 4u;
  }
  if (chanceQ16 > 65535u) chanceQ16 = 65535u;
  return static_cast<uint16_t>(chanceQ16);
}

bool RollOldAgeDeath(Random& rng, int ageDays, bool legendary) {
  if (ageDays >= kOldAgeMaxDays) return true;
  uint16_t chanceQ16 = OldAgeDailyChanceQ16(ageDays, legendary);
  if (chanceQ16 == 0) return false;
  return static_cast<uint32_t>(rng.RangeInt(0, 65535)) < static_cast<uint32_t>(chanceQ16);
}

float ChanceWindow(float perDayChance, int days) {
  if (days <= 1) return perDayChance;
  if (!(perDayChance > 0.0f)) return 0.0f;
  if (perDayChance >= 1.0f) return 1.0f;
  float base = 1.0f - perDayChance;
  float pow = 1.0f;
  int exp = days;
  while (exp > 0) {
    if ((exp & 1) != 0) pow *= base;
    base *= base;
    exp >>= 1;
  }
  float out = 1.0f - pow;
  if (out < 0.0f) return 0.0f;
  if (out > 1.0f) return 1.0f;
  return out;
}

uint16_t ChanceWindowQ16(uint16_t perDayChanceQ16, int days) {
  if (days <= 1) return perDayChanceQ16;
  if (perDayChanceQ16 == 0) return 0;
  if (perDayChanceQ16 >= 65535u) return 65535u;
  uint32_t baseQ16 = 65535u - static_cast<uint32_t>(perDayChanceQ16);
  uint32_t powQ16 = 65535u;
  int exp = days;
  while (exp > 0) {
    if ((exp & 1) != 0) {
      powQ16 = static_cast<uint32_t>((static_cast<uint64_t>(powQ16) * baseQ16) / 65535u);
    }
    baseQ16 = static_cast<uint32_t>((static_cast<uint64_t>(baseQ16) * baseQ16) / 65535u);
    exp >>= 1;
  }
  uint32_t out = 65535u - powQ16;
  if (out > 65535u) out = 65535u;
  return static_cast<uint16_t>(out);
}

bool RollOldAgeDeathWindow(Random& rng, int ageDaysStart, int dayDelta, bool legendary) {
  if (ageDaysStart >= kOldAgeMaxDays) return true;
  if (dayDelta <= 1) return RollOldAgeDeath(rng, ageDaysStart, legendary);
  int ageDaysEnd = ageDaysStart + std::max(1, dayDelta);
  if (ageDaysEnd >= kOldAgeMaxDays) return true;
  uint16_t perDayQ16 = OldAgeDailyChanceQ16(ageDaysEnd, legendary);
  uint16_t windowQ16 = ChanceWindowQ16(perDayQ16, dayDelta);
  if (windowQ16 == 0) return false;
  return static_cast<uint32_t>(rng.RangeInt(0, 65535)) < static_cast<uint32_t>(windowQ16);
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

bool TaskTargetsTile(TaskType type) {
  switch (type) {
    case TaskType::CollectFood:
    case TaskType::CollectWood:
    case TaskType::HarvestFarm:
    case TaskType::PlantFarm:
    case TaskType::BuildStructure:
      return true;
    default:
      return false;
  }
}

bool SameFaction(const SettlementManager& settlements, int settlementA, int settlementB) {
  if (settlementA <= 0 || settlementB <= 0) return false;
  const Settlement* a = settlements.Get(settlementA);
  const Settlement* b = settlements.Get(settlementB);
  if (!a || !b) return false;
  return a->factionId == b->factionId;
}

bool IsFriendlyOwner(const SettlementManager& settlements, int settlementId, int ownerId) {
  if (settlementId <= 0 || ownerId <= 0) return false;
  if (settlementId == ownerId) return true;
  return SameFaction(settlements, settlementId, ownerId);
}

bool FindNearbyGranary(const World& world, const SettlementManager& settlements, int settlementId,
                       int cx, int cy, int radius, int& outX, int& outY) {
  if (settlementId <= 0) return false;
  int bestDist = radius + 1;
  int bestX = 0;
  int bestY = 0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int dist = std::abs(dx) + std::abs(dy);
      if (dist > radius) continue;
      int x = cx + dx;
      int y = cy + dy;
      if (!world.InBounds(x, y)) continue;
      const Tile& tile = world.At(x, y);
      if (tile.building != BuildingType::Granary) continue;
      if (!IsFriendlyOwner(settlements, settlementId, tile.buildingOwnerId)) continue;
      if (dist < bestDist) {
        bestDist = dist;
        bestX = x;
        bestY = y;
      }
    }
  }
  if (bestDist <= radius) {
    outX = bestX;
    outY = bestY;
    return true;
  }
  return false;
}

void SelectFoodDropoffTarget(Human& human, const World& world, const SettlementManager& settlements,
                             int sourceX, int sourceY) {
  human.taskX = human.homeX;
  human.taskY = human.homeY;
  human.taskSettlementId = human.settlementId;
  if (human.settlementId <= 0) return;
  if (Manhattan(sourceX, sourceY, human.homeX, human.homeY) <= kGranaryDropRadius) return;
  int gx = 0;
  int gy = 0;
  if (FindNearbyGranary(world, settlements, human.settlementId, sourceX, sourceY,
                        kGranaryDropRadius, gx, gy)) {
    human.taskX = gx;
    human.taskY = gy;
  }
}

bool CanDropOffFoodAt(const World& world, const SettlementManager& settlements, int settlementId,
                      int x, int y) {
  if (settlementId <= 0) return false;
  const Settlement* settlement = settlements.Get(settlementId);
  if (settlement && x == settlement->centerX && y == settlement->centerY) {
    return true;
  }
  int gx = 0;
  int gy = 0;
  return FindNearbyGranary(world, settlements, settlementId, x, y, kGranaryDropRadius, gx, gy);
}

void StayTarget(const Human& human, int& outX, int& outY) {
  if (human.hasTask &&
      (human.taskType == TaskType::HaulToStockpile ||
       human.taskType == TaskType::HaulWoodToStockpile) &&
      (human.taskX != human.homeX || human.taskY != human.homeY)) {
    outX = human.taskX;
    outY = human.taskY;
    return;
  }
  outX = human.homeX;
  outY = human.homeY;
}

int BuildWoodCost(BuildingType type) {
  switch (type) {
    case BuildingType::House:
      return Settlement::kHouseWoodCost;
    case BuildingType::TownHall:
      return Settlement::kTownHallWoodCost;
    case BuildingType::Farm:
      return Settlement::kFarmWoodCost;
    case BuildingType::Granary:
      return Settlement::kGranaryWoodCost;
    case BuildingType::Well:
      return Settlement::kWellWoodCost;
    case BuildingType::WatchTower:
      return Settlement::kWatchTowerWoodCost;
    default:
      return 0;
  }
}

uint16_t RollTraits(Random& rng) {
  uint16_t traits = 0;
  if (rng.Chance(0.12f)) traits |= static_cast<uint16_t>(HumanTrait::Brave);
  if (rng.Chance(0.10f)) traits |= static_cast<uint16_t>(HumanTrait::Lazy);
  if (rng.Chance(0.11f)) traits |= static_cast<uint16_t>(HumanTrait::Wise);
  if (rng.Chance(0.10f)) traits |= static_cast<uint16_t>(HumanTrait::Greedy);
  if (rng.Chance(0.08f)) traits |= static_cast<uint16_t>(HumanTrait::Ambitious);
  if (rng.Chance(0.10f)) traits |= static_cast<uint16_t>(HumanTrait::Kind);
  if (rng.Chance(0.09f)) traits |= static_cast<uint16_t>(HumanTrait::Curious);
  return traits;
}

void ApplyLegendaryBoost(Human& human, Random& rng) {
  human.legendary = true;
  human.legendPower = static_cast<uint8_t>(rng.RangeInt(2, 5));
  human.traits |= static_cast<uint16_t>(HumanTrait::Brave);
  human.traits |= static_cast<uint16_t>(HumanTrait::Wise);
  human.traits |= static_cast<uint16_t>(HumanTrait::Ambitious);
}
}  // namespace

const char* DeathReasonName(DeathReason reason) {
  switch (reason) {
    case DeathReason::Starvation:
      return "starvation";
    case DeathReason::Dehydration:
      return "dehydration";
    case DeathReason::OldAge:
      return "old_age";
    case DeathReason::War:
      return "war";
    default:
      return "unknown";
  }
}

const char* ArmyStateName(ArmyState state) {
  switch (state) {
    case ArmyState::Idle:
      return "idle";
    case ArmyState::Rally:
      return "rally";
    case ArmyState::March:
      return "march";
    case ArmyState::Siege:
      return "siege";
    case ArmyState::Defend:
      return "defend";
    case ArmyState::Retreat:
      return "retreat";
    default:
      return "unknown";
  }
}

const char* HumanTraitName(HumanTrait trait) {
  switch (trait) {
    case HumanTrait::Brave:
      return "brave";
    case HumanTrait::Lazy:
      return "lazy";
    case HumanTrait::Wise:
      return "wise";
    case HumanTrait::Greedy:
      return "greedy";
    case HumanTrait::Ambitious:
      return "ambitious";
    case HumanTrait::Kind:
      return "kind";
    case HumanTrait::Curious:
      return "curious";
    default:
      return "unknown";
  }
}

bool HumanHasTrait(uint16_t traits, HumanTrait trait) {
  return (traits & static_cast<uint16_t>(trait)) != 0;
}

void HumanTraitsToString(char* buffer, size_t size, uint16_t traits, bool legendary) {
  if (!buffer || size == 0) return;
  buffer[0] = '\0';
  bool first = true;
  auto append = [&](const char* word) {
    if (!word || word[0] == '\0') return;
    if (!first) {
      std::snprintf(buffer + std::strlen(buffer), size - std::strlen(buffer), ", ");
    }
    std::snprintf(buffer + std::strlen(buffer), size - std::strlen(buffer), "%s", word);
    first = false;
  };
  if (legendary) {
    append("legendary");
  }
  if (HumanHasTrait(traits, HumanTrait::Brave)) append(HumanTraitName(HumanTrait::Brave));
  if (HumanHasTrait(traits, HumanTrait::Lazy)) append(HumanTraitName(HumanTrait::Lazy));
  if (HumanHasTrait(traits, HumanTrait::Wise)) append(HumanTraitName(HumanTrait::Wise));
  if (HumanHasTrait(traits, HumanTrait::Greedy)) append(HumanTraitName(HumanTrait::Greedy));
  if (HumanHasTrait(traits, HumanTrait::Ambitious)) append(HumanTraitName(HumanTrait::Ambitious));
  if (HumanHasTrait(traits, HumanTrait::Kind)) append(HumanTraitName(HumanTrait::Kind));
  if (HumanHasTrait(traits, HumanTrait::Curious)) append(HumanTraitName(HumanTrait::Curious));
  if (first) {
    std::snprintf(buffer, size, "none");
  }
}

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
  human.foodCooldownDays = static_cast<uint8_t>(
      rng.RangeInt(0, std::max(0, kFoodIntervalDays - 1)));
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
  human.traits = RollTraits(rng);
  if (rng.Chance(0.0015f)) {
    ApplyLegendaryBoost(human, rng);
  }
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
  human.taskBuildType = BuildingType::None;
  human.carrying = false;
  human.carryFood = 0;
  human.carryWood = 0;
  return human;
}

void HumanManager::Spawn(int x, int y, bool female, Random& rng) {
  humans_.push_back(CreateHuman(x, y, female, rng, Human::kAdultAgeDays));
}

void HumanManager::RebuildIdMap() {
  humanIdToIndex_.assign(nextId_, -1);
  for (int i = 0; i < static_cast<int>(humans_.size()); ++i) {
    humanIdToIndex_[humans_[i].id] = i;
  }
}

void HumanManager::RecordDeath(int humanId, int day, DeathReason reason) {
  deathLog_.push_back(DeathRecord{day, humanId, reason});
  switch (reason) {
    case DeathReason::Starvation:
      deathSummary_.starvation++;
      break;
    case DeathReason::Dehydration:
      deathSummary_.dehydration++;
      break;
    case DeathReason::OldAge:
      deathSummary_.oldAge++;
      break;
    case DeathReason::War:
      deathSummary_.war++;
      break;
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

int HumanManager::PopCountAt(int x, int y) const {
  if (crowdGridW_ <= 0 || crowdGridH_ <= 0) return 0;
  if (static_cast<unsigned>(x) >= static_cast<unsigned>(crowdGridW_) ||
      static_cast<unsigned>(y) >= static_cast<unsigned>(crowdGridH_)) {
    return 0;
  }
  const int idx = y * crowdGridW_ + x;
  if (popStampByTile_[static_cast<size_t>(idx)] != crowdGeneration_) return 0;
  return popCountByTile_[static_cast<size_t>(idx)];
}

int HumanManager::AdultMaleCountAt(int x, int y) const {
  if (crowdGridW_ <= 0 || crowdGridH_ <= 0) return 0;
  if (static_cast<unsigned>(x) >= static_cast<unsigned>(crowdGridW_) ||
      static_cast<unsigned>(y) >= static_cast<unsigned>(crowdGridH_)) {
    return 0;
  }
  const int idx = y * crowdGridW_ + x;
  if (adultMaleStampByTile_[static_cast<size_t>(idx)] != crowdGeneration_) return 0;
  return adultMaleCountByTile_[static_cast<size_t>(idx)];
}

int HumanManager::AdultMaleSampleIdAt(int x, int y) const {
  if (crowdGridW_ <= 0 || crowdGridH_ <= 0) return -1;
  if (static_cast<unsigned>(x) >= static_cast<unsigned>(crowdGridW_) ||
      static_cast<unsigned>(y) >= static_cast<unsigned>(crowdGridH_)) {
    return -1;
  }
  const int idx = y * crowdGridW_ + x;
  if (adultMaleStampByTile_[static_cast<size_t>(idx)] != crowdGeneration_) return -1;
  return adultMaleSampleIdByTile_[static_cast<size_t>(idx)];
}

void HumanManager::EnsureCrowdGrids(int w, int h) {
  if (w <= 0 || h <= 0) {
    crowdGridW_ = 0;
    crowdGridH_ = 0;
    crowdGeneration_ = 1;
    popStampByTile_.clear();
    popCountByTile_.clear();
    adultMaleStampByTile_.clear();
    adultMaleCountByTile_.clear();
    adultMaleSampleIdByTile_.clear();
    return;
  }

  if (w == crowdGridW_ && h == crowdGridH_) return;

  crowdGridW_ = w;
  crowdGridH_ = h;
  const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h);
  crowdGeneration_ = 1;
  popStampByTile_.assign(total, 0u);
  popCountByTile_.assign(total, 0);
  adultMaleStampByTile_.assign(total, 0u);
  adultMaleCountByTile_.assign(total, 0);
  adultMaleSampleIdByTile_.assign(total, -1);
}

int HumanManager::FindMateTargetId(const Human& human, const World& world, Random& rng) const {
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
      int count = AdultMaleCountAt(nx, ny);
      if (count <= 0) continue;
      total += count;
      int sampleId = AdultMaleSampleIdAt(nx, ny);
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
  bool adult = human.ageDays >= Human::kAdultAgeDays;

  bool mobilized = (human.role == Role::Soldier && human.armyState != ArmyState::Idle);
  if (mobilized && !nearFire && !thirsty && !hungry) {
    human.hasTask = false;
    human.mateTargetId = -1;

    int targetX = human.homeX;
    int targetY = human.homeY;
    if (human.isGeneral) {
      if (human.armyState == ArmyState::Defend) {
        const Settlement* home = (human.settlementId > 0) ? settlements.Get(human.settlementId) : nullptr;
        if (home && home->hasDefenseTarget) {
          targetX = home->defenseTargetX;
          targetY = home->defenseTargetY;
        }
      } else if (human.armyState == ArmyState::March || human.armyState == ArmyState::Siege) {
        const Settlement* target = (human.warTargetSettlementId > 0)
                                       ? settlements.Get(human.warTargetSettlementId)
                                       : nullptr;
        if (target) {
          targetX = target->centerX;
          targetY = target->centerY;
        }
      }
    } else {
      const Settlement* home = (human.settlementId > 0) ? settlements.Get(human.settlementId) : nullptr;
      const int generalId = home ? home->generalHumanId : -1;
      int gx = 0;
      int gy = 0;
      if (generalId > 0 && GetHumanById(generalId, gx, gy)) {
        int dx = 0;
        int dy = 0;
        FormationOffset(human.formationSlot, dx, dy);
        targetX = gx + dx;
        targetY = gy + dy;
      }
    }

    targetX = ClampInt(targetX, 0, world.width() - 1);
    targetY = ClampInt(targetY, 0, world.height() - 1);
    if (world.At(targetX, targetY).type == TileType::Ocean) {
      targetX = human.x;
      targetY = human.y;
    }

    human.goal = Goal::Wander;
    human.targetX = targetX;
    human.targetY = targetY;
    human.rethinkCooldownTicks = rng.RangeInt(std::max(1, ticksPerDay / 4), std::max(2, ticksPerDay / 2));
    return;
  }

  if (nearFire) {
    human.goal = Goal::FleeFire;
    human.mateTargetId = -1;
  } else if (thirsty) {
    human.goal = Goal::SeekWater;
    human.mateTargetId = -1;
    bool hasTarget = false;
    if (world.InBounds(human.lastWaterX, human.lastWaterY) &&
        world.At(human.lastWaterX, human.lastWaterY).type == TileType::FreshWater) {
      human.targetX = human.lastWaterX;
      human.targetY = human.lastWaterY;
      hasTarget = true;
    } else if (human.settlementId != -1) {
      const Settlement* settlement = settlements.Get(human.settlementId);
      if (settlement && settlement->hasWaterTarget) {
        human.targetX = settlement->waterTargetX;
        human.targetY = settlement->waterTargetY;
        hasTarget = true;
      }
    }
    if (!hasTarget) {
      human.targetX = human.x;
      human.targetY = human.y;
    }
  } else if (hungry) {
    human.goal = Goal::SeekFood;
    human.mateTargetId = -1;
    if (!SettlementHasStockFood(settlements, human)) {
      if (!TryPickNoStockFoodTarget(human, world, settlements, rng, tickCount)) {
        PickNoStockFoodExploreTarget(human, world, settlements, tickCount);
      }
    } else {
      human.targetX = human.x;
      human.targetY = human.y;
    }
  } else if (human.hasTask) {
    switch (human.taskType) {
      case TaskType::CollectFood:
      case TaskType::CollectWood:
      case TaskType::HarvestFarm:
      case TaskType::PlantFarm:
      case TaskType::BuildStructure:
        human.goal = Goal::SeekFood;
        human.targetX = human.taskX;
        human.targetY = human.taskY;
        break;
      case TaskType::HaulToStockpile:
      case TaskType::HaulWoodToStockpile:
        human.goal = Goal::StayHome;
        human.targetX = human.taskX;
        human.targetY = human.taskY;
        break;
      case TaskType::PatrolEdge:
        human.goal = Goal::Wander;
        human.targetX = human.taskX;
        human.targetY = human.taskY;
        break;
    }
  } else {
    bool canMate = adult && human.female && !human.pregnant && human.mateCooldownDays == 0 &&
                   human.daysWithoutFood <= 4 && human.daysWithoutWater <= 4;
    if (canMate && human.settlementId != -1) {
      const Settlement* settlement = settlements.Get(human.settlementId);
      if (!settlement || settlement->stockFood < settlement->population * kMateFoodReservePerPop ||
          settlement->housingCap <= settlement->population) {
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
      int wanderBias = human.wanderlust;
      if (HumanHasTrait(human.traits, HumanTrait::Lazy)) {
        wanderBias = std::max(0, wanderBias - 80);
      }
      if (HumanHasTrait(human.traits, HumanTrait::Curious)) {
        wanderBias = std::min(255, wanderBias + 60);
      }
      if (human.settlementId != -1 &&
          (wanderBias < 100 || human.role == Role::Guard || human.role == Role::Builder ||
           human.role == Role::Farmer || human.role == Role::Soldier)) {
        human.goal = Goal::StayHome;
      } else {
        human.goal = Goal::Wander;
      }
      human.mateTargetId = -1;
    }
  }

  if (human.goal == Goal::StayHome) {
    StayTarget(human, human.targetX, human.targetY);
  } else if (human.goal == Goal::Wander) {
    uint32_t hash = HashNoise(static_cast<uint32_t>(human.id),
                              static_cast<uint32_t>(tickCount), 0xA5u, 0x5Au);
    int radius = 10;
    if (human.role == Role::Gatherer) {
      radius = kGathererWanderRadius;
    } else if (human.role == Role::Scout) {
      radius = kScoutWanderRadius;
    }
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
  float cooldownScale = 1.0f;
  if (HumanHasTrait(human.traits, HumanTrait::Lazy)) {
    cooldownScale = 1.35f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Curious)) {
    cooldownScale *= 0.85f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Wise)) {
    cooldownScale *= 0.92f;
  }
  minTicks = std::max(1, minTicks);
  maxTicks = std::max(minTicks, maxTicks);
  minTicks = std::max(1, static_cast<int>(std::round(minTicks * cooldownScale)));
  maxTicks = std::max(minTicks, static_cast<int>(std::round(maxTicks * cooldownScale)));
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
  bool mobilized = (human.role == Role::Soldier && human.armyState != ArmyState::Idle);
  bool haulingFood = human.hasTask && human.taskType == TaskType::HaulToStockpile &&
                     human.carryFood > 0;
  int stayX = human.homeX;
  int stayY = human.homeY;
  StayTarget(human, stayX, stayY);

  if (nearFire && human.goal != Goal::FleeFire) {
    human.goal = Goal::FleeFire;
    if (!human.carrying) {
      human.hasTask = false;
    }
    human.mateTargetId = -1;
  } else if (thirsty && human.goal != Goal::SeekWater) {
    human.goal = Goal::SeekWater;
    human.hasTask = false;
    human.mateTargetId = -1;
    human.forceReplan = true;
  } else if (hungry && human.goal != Goal::SeekFood) {
    if (haulingFood) {
      human.goal = Goal::StayHome;
      human.targetX = human.taskX;
      human.targetY = human.taskY;
    } else {
      human.goal = Goal::SeekFood;
      human.forceReplan = true;
      if (human.hasTask && !human.carrying && human.taskType != TaskType::CollectFood &&
          human.taskType != TaskType::HarvestFarm) {
        human.hasTask = false;
        human.forceReplan = true;
      }
    }
  }

  if (thirsty && human.goal == Goal::SeekWater) {
    if (world.InBounds(human.lastWaterX, human.lastWaterY) &&
        world.At(human.lastWaterX, human.lastWaterY).type == TileType::FreshWater) {
      human.targetX = human.lastWaterX;
      human.targetY = human.lastWaterY;
    } else if (human.settlementId != -1) {
      const Settlement* settlement = settlements.Get(human.settlementId);
      if (settlement && settlement->hasWaterTarget) {
        human.targetX = settlement->waterTargetX;
        human.targetY = settlement->waterTargetY;
      }
    }
  }

  if (mobilized && !emergency) {
    human.hasTask = false;
    human.mateTargetId = -1;

    int targetX = human.homeX;
    int targetY = human.homeY;
    if (human.isGeneral) {
      if (human.armyState == ArmyState::Defend) {
        const Settlement* home = (human.settlementId > 0) ? settlements.Get(human.settlementId) : nullptr;
        if (home && home->hasDefenseTarget) {
          targetX = home->defenseTargetX;
          targetY = home->defenseTargetY;
        }
      } else if (human.armyState == ArmyState::March || human.armyState == ArmyState::Siege) {
        const Settlement* target = (human.warTargetSettlementId > 0)
                                       ? settlements.Get(human.warTargetSettlementId)
                                       : nullptr;
        if (target) {
          targetX = target->centerX;
          targetY = target->centerY;
        }
      }
    } else {
      const Settlement* home = (human.settlementId > 0) ? settlements.Get(human.settlementId) : nullptr;
      const int generalId = home ? home->generalHumanId : -1;
      int gx = 0;
      int gy = 0;
      if (generalId > 0 && GetHumanById(generalId, gx, gy)) {
        int dx = 0;
        int dy = 0;
        FormationOffset(human.formationSlot, dx, dy);
        targetX = gx + dx;
        targetY = gy + dy;
      }
    }

    targetX = ClampInt(targetX, 0, world.width() - 1);
    targetY = ClampInt(targetY, 0, world.height() - 1);
    if (world.At(targetX, targetY).type == TileType::Ocean) {
      targetX = human.x;
      targetY = human.y;
    }

    human.goal = Goal::Wander;
    human.targetX = targetX;
    human.targetY = targetY;
  }

  if (human.goal == Goal::SeekMate && human.mateTargetId != -1) {
    int mx = 0;
    int my = 0;
    if (!GetHumanById(human.mateTargetId, mx, my)) {
      human.mateTargetId = -1;
      human.forceReplan = true;
    }
  }

  if (!mobilized && !human.hasTask && human.settlementId != -1) {
    Settlement* settlement = settlements.GetMutable(human.settlementId);
    if (settlement) {
      Task task;
      int attempts = 0;
      bool allowFoodTask = emergency;
      bool picked = false;
      while (attempts < 4 && settlement->PopTask(task)) {
        attempts++;
        if (!allowFoodTask ||
            task.type == TaskType::CollectFood || task.type == TaskType::HarvestFarm ||
            task.type == TaskType::PlantFarm) {
          picked = true;
          break;
        }
      }
      if (picked) {
        human.hasTask = true;
        human.taskType = task.type;
        human.taskX = task.x;
        human.taskY = task.y;
        human.taskAmount = task.amount;
        human.taskSettlementId = task.settlementId;
        human.taskBuildType = task.buildType;
        if (TaskTargetsTile(task.type)) {
          human.goal = Goal::SeekFood;
          human.targetX = task.x;
          human.targetY = task.y;
        } else if (task.type == TaskType::HaulToStockpile ||
                   task.type == TaskType::HaulWoodToStockpile) {
          human.goal = Goal::StayHome;
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
    const Tile& candidate = world.At(nx, ny);
    if ((d[0] != 0 || d[1] != 0) && !IsWalkable(candidate)) continue;

    int score = 0;

    int crowdPenalty = kCrowdPenalty;
    if (human.goal == Goal::SeekWater) {
      crowdPenalty =
          std::max(1, static_cast<int>(std::round(static_cast<float>(kCrowdPenalty) *
                                                 kSeekWaterCrowdPenaltyScale)));
    }
    int crowdCount = PopCountAt(nx, ny);
    if (crowdCount > 0) {
      score -= crowdCount * crowdPenalty;
    }
    if (candidate.burning) {
      score -= 200000;
    }

    const bool isGatherOrScout = (human.role == Role::Gatherer || human.role == Role::Scout);
    const bool isSoldier = (human.role == Role::Soldier);
    const bool isMarchingSoldier = (isSoldier && human.armyState != ArmyState::Idle &&
                                    (human.armyState == ArmyState::March || human.armyState == ArmyState::Siege));
    const bool isHomeBoundRole =
        (human.role == Role::Guard || human.role == Role::Builder || human.role == Role::Farmer ||
         (isSoldier && !isMarchingSoldier));
    bool needWaterScent = false;
    bool needFireRisk = false;
    bool needHomeScent = false;

    switch (human.goal) {
      case Goal::SeekFood:
        if (human.targetX != human.x || human.targetY != human.y) {
          int dist = Manhattan(nx, ny, human.targetX, human.targetY);
          score -= dist * 160;
        }
        break;
      case Goal::SeekWater:
        needWaterScent = true;
        if (human.targetX != human.x || human.targetY != human.y) {
          int dist = Manhattan(nx, ny, human.targetX, human.targetY);
          score -= dist * 140;
        }
        break;
      case Goal::FleeFire:
        needFireRisk = true;
        break;
      case Goal::StayHome: {
        int dist = Manhattan(nx, ny, stayX, stayY);
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
        int weight = 40;
        if (isMarchingSoldier) {
          weight = 120;
        }
        score -= dist * weight;
        break;
      }
    }

    if (isHomeBoundRole) {
      int dist = Manhattan(nx, ny, human.homeX, human.homeY);
      score -= dist * 40;
    }

    if (human.settlementId != -1 && human.goal != Goal::SeekWater) {
      bool wanderHeavy =
          (human.goal == Goal::Wander || isGatherOrScout);
      if (!wanderHeavy) {
        needHomeScent = true;
      }
    }

    uint16_t waterScent = 0;
    uint16_t fireRisk = 0;
    uint16_t homeScent = 0;
    if (needWaterScent) waterScent = world.WaterScentAt(nx, ny);
    if (needFireRisk) fireRisk = world.FireRiskAt(nx, ny);
    if (needHomeScent) homeScent = world.HomeScentAt(nx, ny);

    if (human.goal == Goal::SeekWater) {
      score += static_cast<int>(waterScent) * 4;
    } else if (human.goal == Goal::FleeFire) {
      score -= static_cast<int>(fireRisk) * 6;
    }

    if (needHomeScent) {
      float homeBias = 1.0f - (static_cast<float>(human.wanderlust) / 255.0f);
      int homeScore = static_cast<int>(static_cast<float>(homeScent) * 0.015f * homeBias);
      score += homeScore;
    }

    uint32_t noise = HashNoise(static_cast<uint32_t>(human.id),
                               static_cast<uint32_t>(tickCount),
                               static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
    int noiseRange = 200;
    if (isMarchingSoldier) {
      noiseRange = 60;
    }
    score += static_cast<int>(noise % static_cast<uint32_t>(noiseRange)) - (noiseRange / 2);

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
        const Tile& tile = world.At(human.x, human.y);
        if (tile.food > 0) {
          int take = std::max(1, human.taskAmount);
	          take = world.TakeFood(human.x, human.y, take);
	          if (take <= 0) {
	            human.hasTask = false;
	            human.forceReplan = true;
	            // Fall through; replan will happen below.
	          }
          human.carryFood += take;
          human.carrying = true;
          human.taskType = TaskType::HaulToStockpile;
          SelectFoodDropoffTarget(human, world, settlements, human.x, human.y);
          human.goal = Goal::StayHome;
        } else {
          human.hasTask = false;
          human.forceReplan = true;
        }
      }
    } else if (human.taskType == TaskType::CollectWood) {
      if (human.x == human.taskX && human.y == human.taskY) {
        const Tile& tile = world.At(human.x, human.y);
        if (tile.trees > 0) {
          int take = std::max(1, human.taskAmount);
	          take = world.TakeTrees(human.x, human.y, take);
	          if (take <= 0) {
	            human.hasTask = false;
	            human.forceReplan = true;
	            // Fall through; replan will happen below.
	          }
          human.carryWood += take;
          human.carrying = true;
          human.taskType = TaskType::HaulWoodToStockpile;
          human.taskX = human.homeX;
          human.taskY = human.homeY;
          human.taskSettlementId = human.settlementId;
          human.goal = Goal::StayHome;
        } else {
          human.hasTask = false;
          human.forceReplan = true;
        }
      }
    } else if (human.taskType == TaskType::HarvestFarm) {
      if (human.x == human.taskX && human.y == human.taskY) {
        const Tile& tile = world.At(human.x, human.y);
        if (tile.building == BuildingType::Farm && tile.buildingOwnerId == human.taskSettlementId &&
            tile.farmStage >= Settlement::kFarmReadyStage) {
          int yield = (human.taskAmount > 0) ? human.taskAmount : Settlement::kFarmYield;
          human.carryFood += yield;
          human.carrying = true;
          world.EditTile(human.x, human.y, [&](Tile& t) { t.farmStage = 0; });
          human.taskType = TaskType::HaulToStockpile;
          SelectFoodDropoffTarget(human, world, settlements, human.x, human.y);
          human.goal = Goal::StayHome;
        } else {
          human.hasTask = false;
          human.forceReplan = true;
        }
      }
    } else if (human.taskType == TaskType::PlantFarm) {
      if (human.x == human.taskX && human.y == human.taskY) {
        const Tile& tile = world.At(human.x, human.y);
        if (tile.building == BuildingType::Farm && tile.buildingOwnerId == human.taskSettlementId &&
            tile.farmStage == 0) {
          world.EditTile(human.x, human.y, [&](Tile& t) { t.farmStage = 1; });
        }
        human.hasTask = false;
        human.forceReplan = true;
      }
    } else if (human.taskType == TaskType::BuildStructure) {
      if (human.x == human.taskX && human.y == human.taskY) {
        const Tile& tile = world.At(human.x, human.y);
        Settlement* settlement = settlements.GetMutable(human.taskSettlementId);
        int cost = BuildWoodCost(human.taskBuildType);
        if (settlement && tile.type == TileType::Land && !tile.burning &&
            tile.building == BuildingType::None && settlement->stockWood >= cost) {
          settlement->stockWood = std::max(0, settlement->stockWood - cost);
          uint8_t farmStage = (human.taskBuildType == BuildingType::Farm) ? 1u : 0u;
          world.PlaceBuilding(human.x, human.y, human.taskBuildType, settlement->id, farmStage);
        }
        human.hasTask = false;
        human.forceReplan = true;
      }
    } else if (human.taskType == TaskType::HaulToStockpile) {
      bool dropoffReady = (human.x == human.taskX && human.y == human.taskY);
      Settlement* settlement = settlements.GetMutable(human.taskSettlementId);
      if (!dropoffReady && settlement) {
        dropoffReady = CanDropOffFoodAt(world, settlements, settlement->id, human.x, human.y);
      }
      if (dropoffReady) {
        if (settlement && human.carryFood > 0) {
          settlement->stockFood += human.carryFood;
        }
        human.carryFood = 0;
        human.carrying = (human.carryWood > 0);
        human.hasTask = false;
        human.forceReplan = true;
      }
    } else if (human.taskType == TaskType::HaulWoodToStockpile) {
      if (human.x == human.taskX && human.y == human.taskY) {
        Settlement* settlement = settlements.GetMutable(human.taskSettlementId);
        if (settlement && human.carryWood > 0) {
          settlement->stockWood += human.carryWood;
        }
        human.carryWood = 0;
        human.carrying = (human.carryFood > 0);
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

    int stride = 1;
    float speedScale = 1.0f;
    if (human.role == Role::Idle) {
      stride = 3;
      speedScale = 0.6f;
    } else if (human.role == Role::Builder) {
      stride = 2;
      speedScale = 0.8f;
    } else if (human.role == Role::Guard || human.role == Role::Soldier) {
      stride = 2;
      speedScale = 0.9f;
    }
    human.moveAccum += stepsPerTick * speedScale;
    if ((tickCount + human.id) % stride != 0) {
      continue;
    }
    int steps = 0;
    while (human.moveAccum >= 1.0f && steps < 4) {
      human.moveAccum -= 1.0f;
      UpdateMoveStep(human, world, settlements, rng, tickCount, ticksPerDay);
      steps++;
    }
  }
}

void HumanManager::MarkDeadByIndex(int index, int day, DeathReason reason) {
  if (index < 0 || index >= static_cast<int>(humans_.size())) return;
  Human& human = humans_[index];
  if (!human.alive) return;
  RecordDeath(human.id, day, reason);
  human.alive = false;
}

void HumanManager::RecordWarDeaths(int count) {
  if (count <= 0) return;
  deathSummary_.war += count;
}

void HumanManager::UpdateDailyCoarse(World& world, SettlementManager& settlements, Random& rng,
                                     int dayCount, int dayDelta, int& birthsToday,
                                     int& deathsToday) {
  if (macroActive_) return;
  CrashContextSetStage("Humans::UpdateDailyCoarse begin");
  CrashContextSetPopulation(static_cast<int>(humans_.size()));
  birthsToday = 0;
  deathsToday = 0;
  if (dayDelta < 1) dayDelta = 1;

  const int w = world.width();
  const int h = world.height();

  EnsureCrowdGrids(w, h);
  crowdGeneration_++;
  if (crowdGeneration_ == 0) {
    std::fill(popStampByTile_.begin(), popStampByTile_.end(), 0u);
    std::fill(adultMaleStampByTile_.begin(), adultMaleStampByTile_.end(), 0u);
    crowdGeneration_ = 1;
  }

  CrashContextSetStage("Humans::UpdateDailyCoarse count");
  for (const auto& human : humans_) {
    if (!human.alive) continue;
    if (static_cast<unsigned>(human.x) >= static_cast<unsigned>(w) ||
        static_cast<unsigned>(human.y) >= static_cast<unsigned>(h)) {
      continue;
    }
    const int idx = human.y * w + human.x;
    if (popStampByTile_[static_cast<size_t>(idx)] != crowdGeneration_) {
      popStampByTile_[static_cast<size_t>(idx)] = crowdGeneration_;
      popCountByTile_[static_cast<size_t>(idx)] = 0;
    }
    popCountByTile_[static_cast<size_t>(idx)]++;

    if (!human.female && human.ageDays >= Human::kAdultAgeDays) {
      if (adultMaleStampByTile_[static_cast<size_t>(idx)] != crowdGeneration_) {
        adultMaleStampByTile_[static_cast<size_t>(idx)] = crowdGeneration_;
        adultMaleCountByTile_[static_cast<size_t>(idx)] = 0;
        adultMaleSampleIdByTile_[static_cast<size_t>(idx)] = -1;
      }
      int count = ++adultMaleCountByTile_[static_cast<size_t>(idx)];
      int currentSample = adultMaleSampleIdByTile_[static_cast<size_t>(idx)];
      if (currentSample == -1 || rng.RangeInt(0, count - 1) == 0) {
        adultMaleSampleIdByTile_[static_cast<size_t>(idx)] = human.id;
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

    int ageDaysStart = human.ageDays;
    human.ageDays += dayDelta;
    human.mateCooldownDays = std::max(0, human.mateCooldownDays - dayDelta);

    if (human.pregnant) {
      human.gestationDays += dayDelta;
      if (human.gestationDays >= kGestationDays) {
        human.pregnant = false;
        human.gestationDays = 0;
        bool babyFemale = rng.Chance(0.5f);
        Human baby = CreateHuman(human.x, human.y, babyFemale, rng, 0);
        baby.parentIdMother = human.id;
        baby.settlementId = human.settlementId;
        baby.homeX = human.homeX;
        baby.homeY = human.homeY;
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

    const Tile& tile = world.At(human.x, human.y);
    if (human.foodCooldownDays > 0) {
      human.foodCooldownDays--;
      human.daysWithoutFood = 0;
    } else {
      bool ate = false;
      int eatX = human.x;
      int eatY = human.y;

      if (settlement && settlement->stockFood > 0) {
        settlement->stockFood--;
        ate = true;
      }

      if (!ate) {
        if (tile.food > 0) {
          ate = (world.TakeFood(human.x, human.y, 1) > 0);
        } else {
          const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (const auto& d : dirs) {
            int nx = human.x + d[0];
            int ny = human.y + d[1];
            if (!world.InBounds(nx, ny)) continue;
            const Tile& neighbor = world.At(nx, ny);
            if (neighbor.food > 0) {
              if (world.TakeFood(nx, ny, 1) <= 0) continue;
              ate = true;
              eatX = nx;
              eatY = ny;
              break;
            }
          }
        }
      }

      if (!ate) {
        bool canUseOwnedFarm =
            (human.settlementId == -1 ||
             IsFriendlyOwner(settlements, human.settlementId, tile.buildingOwnerId));
        if (tile.building == BuildingType::Farm &&
            tile.farmStage >= Settlement::kFarmReadyStage && canUseOwnedFarm) {
          int yield = Settlement::kFarmYield;
          if (tile.buildingOwnerId > 0) {
            const Settlement* owner = settlements.Get(tile.buildingOwnerId);
            if (owner) {
              yield = FarmYieldForTier(owner->techTier);
            }
          }
          int remaining = std::max(0, yield - 1);
          if (remaining > 0) {
            human.carryFood += remaining;
            human.carrying = true;
            human.hasTask = true;
            human.taskType = TaskType::HaulToStockpile;
            SelectFoodDropoffTarget(human, world, settlements, human.x, human.y);
          }
          world.EditTile(human.x, human.y, [&](Tile& t) { t.farmStage = 0; });
          ate = true;
        } else {
          const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (const auto& d : dirs) {
            int nx = human.x + d[0];
            int ny = human.y + d[1];
            if (!world.InBounds(nx, ny)) continue;
            const Tile& neighbor = world.At(nx, ny);
            bool canUseNeighborFarm =
                (human.settlementId == -1 ||
                 IsFriendlyOwner(settlements, human.settlementId, neighbor.buildingOwnerId));
            if (neighbor.building == BuildingType::Farm &&
                neighbor.farmStage >= Settlement::kFarmReadyStage && canUseNeighborFarm) {
              int yield = Settlement::kFarmYield;
              if (neighbor.buildingOwnerId > 0) {
                const Settlement* owner = settlements.Get(neighbor.buildingOwnerId);
                if (owner) {
                  yield = FarmYieldForTier(owner->techTier);
                }
              }
              int remaining = std::max(0, yield - 1);
              if (remaining > 0) {
                human.carryFood += remaining;
                human.carrying = true;
                human.hasTask = true;
                human.taskType = TaskType::HaulToStockpile;
                SelectFoodDropoffTarget(human, world, settlements, nx, ny);
              }
              world.EditTile(nx, ny, [&](Tile& t) { t.farmStage = 0; });
              ate = true;
              eatX = nx;
              eatY = ny;
              break;
            }
          }
        }
      }

      if (!ate && human.carryFood > 0 && human.daysWithoutFood >= kCarryFoodEmergencyDays) {
        human.carryFood--;
        human.carrying = (human.carryFood > 0 || human.carryWood > 0);
        ate = true;
      }

      if (ate) {
        human.daysWithoutFood = 0;
        human.lastFoodX = eatX;
        human.lastFoodY = eatY;
        human.foodCooldownDays = static_cast<uint8_t>(
            std::max(0, kFoodIntervalDays - 1));
      } else {
        human.daysWithoutFood++;
      }
    }

    auto findDrinkableWater = [&](int cx, int cy, int& outX, int& outY) {
      const Tile& here = world.At(cx, cy);
      if (here.type == TileType::FreshWater ||
          (here.building == BuildingType::Well && world.WellRadiusAt(cx, cy) > 0)) {
        outX = cx;
        outY = cy;
        return true;
      }
      const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& d : dirs) {
        int nx = cx + d[0];
        int ny = cy + d[1];
        if (!world.InBounds(nx, ny)) continue;
        const Tile& neighbor = world.At(nx, ny);
        if (neighbor.type == TileType::FreshWater ||
            (neighbor.building == BuildingType::Well && world.WellRadiusAt(nx, ny) > 0)) {
          outX = nx;
          outY = ny;
          return true;
        }
      }
      return false;
    };

    int drinkX = human.x;
    int drinkY = human.y;
    if (findDrinkableWater(human.x, human.y, drinkX, drinkY)) {
      human.daysWithoutWater = 0;
      human.lastWaterX = drinkX;
      human.lastWaterY = drinkY;
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

    bool adult = human.ageDays >= Human::kAdultAgeDays;
    if (human.female && adult && !human.pregnant && human.mateCooldownDays == 0 &&
        human.daysWithoutFood <= 4 && human.daysWithoutWater <= 4) {
      bool canMate = true;
      if (settlement && (settlement->stockFood < settlement->population * kMateFoodReservePerPop ||
                         settlement->housingCap <= settlement->population)) {
        canMate = false;
      }
      if (canMate) {
        int maleCount = 0;
        for (int dy = -kMateRadius; dy <= kMateRadius; ++dy) {
          for (int dx = -kMateRadius; dx <= kMateRadius; ++dx) {
            int mx = human.x + dx;
            int my = human.y + dy;
            if (mx < 0 || my < 0 || mx >= w || my >= h) continue;
            maleCount += AdultMaleCountAt(mx, my);
          }
        }
          if (maleCount > 0) {
            float chance = kMateBaseChance + static_cast<float>(maleCount) * kMatePerMaleChance;
            if (chance > kMateMaxChance) chance = kMateMaxChance;
            float chanceWindow = ChanceWindow(chance, dayDelta);
            if (rng.Chance(chanceWindow)) {
              human.pregnant = true;
              human.gestationDays = 0;
              human.mateCooldownDays = kMateCooldownDays;
            }
          }
      }
    }

    int foodMaxDays = kFoodMaxDays + (human.legendary ? 5 : 0);
    if (allowStarvationDeath_ && human.daysWithoutFood > kFoodGraceDays) {
      if (human.daysWithoutFood >= foodMaxDays) {
        RecordDeath(human.id, dayCount, DeathReason::Starvation);
        human.alive = false;
        deathsToday++;
        continue;
      }
      float chance = static_cast<float>(human.daysWithoutFood - kFoodGraceDays) /
                     static_cast<float>(foodMaxDays - kFoodGraceDays);
      if (human.legendary) {
        chance *= 0.5f;
      }
      if (rng.Chance(chance)) {
        RecordDeath(human.id, dayCount, DeathReason::Starvation);
        human.alive = false;
        deathsToday++;
        continue;
      }
    }

    int waterMaxDays = kWaterMaxDays + (human.legendary ? 2 : 0);
    if (allowDehydrationDeath_ && human.daysWithoutWater > kWaterGraceDays) {
      if (human.daysWithoutWater >= waterMaxDays) {
        RecordDeath(human.id, dayCount, DeathReason::Dehydration);
        human.alive = false;
        deathsToday++;
        continue;
      }
      float chance = static_cast<float>(human.daysWithoutWater - kWaterGraceDays) /
                     static_cast<float>(waterMaxDays - kWaterGraceDays);
      if (human.legendary) {
        chance *= 0.6f;
      }
      if (rng.Chance(chance)) {
        RecordDeath(human.id, dayCount, DeathReason::Dehydration);
        human.alive = false;
        deathsToday++;
        continue;
      }
    }

    if (RollOldAgeDeathWindow(rng, ageDaysStart, dayDelta, human.legendary)) {
      RecordDeath(human.id, dayCount, DeathReason::OldAge);
      human.alive = false;
      deathsToday++;
      continue;
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
    settlements.RefreshBuildingStats(world);
    for (auto& settlement : settlements.SettlementsMutable()) {
      int popTotal = settlement.MacroTotal();
      settlement.population = popTotal;

      if (settlement.farms > 0) {
        float dailyFarmFood =
            static_cast<float>(settlement.farms) *
            (static_cast<float>(FarmYieldForTier(settlement.techTier)) / 3.0f);
        settlement.macroFarmFoodAccum += dailyFarmFood;
        int farmFood = static_cast<int>(settlement.macroFarmFoodAccum);
        if (farmFood > 0) {
          settlement.stockFood += farmFood;
          settlement.macroFarmFoodAccum -= static_cast<float>(farmFood);
        }
      }

      if (popTotal > 0) {
        settlement.stockFood += popTotal / 5;
        settlement.stockWood += std::max(1, popTotal / 6);
      }

      float dailyNeed = (kFoodIntervalDays > 0)
                            ? (static_cast<float>(popTotal) / static_cast<float>(kFoodIntervalDays))
                            : static_cast<float>(popTotal);
      settlement.macroFoodNeedAccum += dailyNeed;
      int need = static_cast<int>(settlement.macroFoodNeedAccum);
      if (need > 0) {
        settlement.macroFoodNeedAccum -= static_cast<float>(need);
        if (settlement.stockFood > 0) {
          settlement.stockFood = std::max(0, settlement.stockFood - need);
        }
      }

      float foodFactor = 0.0f;
      if (popTotal > 0) {
        foodFactor = static_cast<float>(settlement.stockFood) /
                     static_cast<float>(std::max(1, popTotal * kMateFoodReservePerPop));
        if (foodFactor > 1.0f) foodFactor = 1.0f;
      }

      float waterFactor = static_cast<float>(world.WaterScentAt(settlement.centerX,
                                                                settlement.centerY)) /
                          60000.0f;
      if (waterFactor > 1.0f) waterFactor = 1.0f;

      float housingFactor = (settlement.housingCap > popTotal) ? 1.0f : 0.0f;

      int fertileFemales = settlement.macroPopF[3];
      int adultMales = settlement.macroPopM[3] + settlement.macroPopM[4];
      int mates = std::min(fertileFemales, adultMales);
      float expectedBirths = static_cast<float>(mates) * kMacroBirthRatePerDay * foodFactor *
                             waterFactor * housingFactor;
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
      if (births > 0 && settlement.stockFood > 0) {
        settlement.stockFood = std::max(0, settlement.stockFood - births * 2);
      }

      float fireFactor = static_cast<float>(world.FireRiskAt(settlement.centerX,
                                                             settlement.centerY)) /
                         60000.0f;
      float starvationRate =
          (allowStarvationDeath_ && settlement.stockFood == 0) ? 0.002f : 0.0f;
      for (int bin = 0; bin < kMacroBins; ++bin) {
        int baseDeathsM = ApplyRate(settlement.macroPopM[bin], kMacroDeathRate[bin], rng);
        int baseDeathsF = ApplyRate(settlement.macroPopF[bin], kMacroDeathRate[bin], rng);
        int starveDeathsM = 0;
        int starveDeathsF = 0;
        int fireDeathsM = 0;
        int fireDeathsF = 0;
        if (starvationRate > 0.0f && (bin == 0 || bin == kMacroBins - 1)) {
          starveDeathsM = ApplyRate(settlement.macroPopM[bin], starvationRate, rng);
          starveDeathsF = ApplyRate(settlement.macroPopF[bin], starvationRate, rng);
        }
        if (fireFactor > 0.2f) {
          float fireRate = fireFactor * 0.0008f;
          fireDeathsM = ApplyRate(settlement.macroPopM[bin], fireRate, rng);
          fireDeathsF = ApplyRate(settlement.macroPopF[bin], fireRate, rng);
        }

        settlement.macroPopM[bin] =
            std::max(0, settlement.macroPopM[bin] - baseDeathsM - starveDeathsM - fireDeathsM);
        settlement.macroPopF[bin] =
            std::max(0, settlement.macroPopF[bin] - baseDeathsF - starveDeathsF - fireDeathsF);
        deathsToday += baseDeathsM + baseDeathsF + starveDeathsM + starveDeathsF + fireDeathsM +
                       fireDeathsF;
        deathSummary_.macroNatural += baseDeathsM + baseDeathsF;
        if (starvationRate > 0.0f && (bin == 0 || bin == kMacroBins - 1)) {
          deathSummary_.macroStarvation += starveDeathsM + starveDeathsF;
        }
        if (fireFactor > 0.2f) {
          deathSummary_.macroFire += fireDeathsM + fireDeathsF;
        }
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
          deathSummary_.macroNatural += deathsM + deathsF;
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
