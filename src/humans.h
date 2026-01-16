#pragma once

#include <vector>

#include "util.h"
#include "world.h"

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
  float animTimer = 0.0f;
  int animFrame = 0;
  bool moving = false;
};

class HumanManager {
 public:
  HumanManager();

  void Spawn(int x, int y, bool female);
  void UpdateDaily(World& world, Random& rng, int& birthsToday, int& deathsToday);
  void UpdateAnimation(float dt);

  int CountAlive() const;
  const std::vector<Human>& Humans() const { return humans_; }

 private:
  Human CreateHuman(int x, int y, bool female);

  int nextId_ = 1;
  std::vector<Human> humans_;
};
