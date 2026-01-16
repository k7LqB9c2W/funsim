#include "humans.h"

#include <algorithm>

namespace {
constexpr int kAdultAgeDays = 16 * 365;
constexpr int kGestationDays = 90;
constexpr float kMoveRandomWeight = 0.15f;

bool IsWalkable(const Tile& tile) {
  return tile.type != TileType::Ocean;
}

float ScoreTile(const World& world, Random& rng, int x, int y) {
  const Tile& tile = world.At(x, y);
  float score = rng.RangeFloat(0.0f, 1.0f) * kMoveRandomWeight;
  score += static_cast<float>(tile.food) * 0.03f;
  if (tile.type == TileType::FreshWater) score += 0.6f;
  if (tile.trees > 0) score += 0.1f;
  return score;
}
}  // namespace

HumanManager::HumanManager() = default;

Human HumanManager::CreateHuman(int x, int y, bool female) {
  Human human;
  human.id = nextId_++;
  human.female = female;
  human.x = x;
  human.y = y;
  human.alive = true;
  human.pregnant = false;
  human.gestationDays = 0;
  human.daysWithoutFood = 0;
  human.animTimer = 0.0f;
  human.animFrame = 0;
  human.moving = false;
  return human;
}

void HumanManager::Spawn(int x, int y, bool female) {
  humans_.push_back(CreateHuman(x, y, female));
}

void HumanManager::UpdateDaily(World& world, Random& rng, int& birthsToday, int& deathsToday) {
  birthsToday = 0;
  deathsToday = 0;

  const int w = world.width();
  const int h = world.height();
  std::vector<int> adultMaleCounts(w * h, 0);
  for (const auto& human : humans_) {
    if (!human.alive) continue;
    if (human.female) continue;
    if (human.ageDays < kAdultAgeDays) continue;
    adultMaleCounts[human.y * w + human.x]++;
  }

  std::vector<Human> newborns;
  newborns.reserve(16);

  for (auto& human : humans_) {
    if (!human.alive) continue;

    human.ageDays++;

    if (human.pregnant) {
      human.gestationDays++;
      if (human.gestationDays >= kGestationDays) {
        human.pregnant = false;
        human.gestationDays = 0;
        bool babyFemale = rng.Chance(0.5f);
        newborns.push_back(CreateHuman(human.x, human.y, babyFemale));
        birthsToday++;
      }
    }

    if (human.female && human.ageDays >= kAdultAgeDays && !human.pregnant) {
      if (adultMaleCounts[human.y * w + human.x] > 0) {
        if (rng.Chance(0.03f)) {
          human.pregnant = true;
          human.gestationDays = 0;
        }
      }
    }

    Tile& tile = world.At(human.x, human.y);
    if (tile.food > 0) {
      tile.food--;
      human.daysWithoutFood = 0;
    } else {
      human.daysWithoutFood++;
    }

    int bestX = human.x;
    int bestY = human.y;
    float bestScore = ScoreTile(world, rng, human.x, human.y);

    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : dirs) {
      int nx = human.x + d[0];
      int ny = human.y + d[1];
      if (!world.InBounds(nx, ny)) continue;
      if (!IsWalkable(world.At(nx, ny))) continue;
      float score = ScoreTile(world, rng, nx, ny);
      if (score > bestScore) {
        bestScore = score;
        bestX = nx;
        bestY = ny;
      }
    }

    human.moving = (bestX != human.x || bestY != human.y);
    human.x = bestX;
    human.y = bestY;

    if (human.daysWithoutFood > 5) {
      float chance = 0.08f * static_cast<float>(human.daysWithoutFood - 4);
      if (chance > 0.8f) chance = 0.8f;
      if (rng.Chance(chance)) {
        human.alive = false;
        deathsToday++;
      }
    }
  }

  if (!newborns.empty()) {
    humans_.insert(humans_.end(), newborns.begin(), newborns.end());
  }

  humans_.erase(std::remove_if(humans_.begin(), humans_.end(),
                               [](const Human& h) { return !h.alive; }),
                humans_.end());
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
