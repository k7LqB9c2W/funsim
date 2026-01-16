#pragma once

#include <cstdint>
#include <random>

class Random {
 public:
  Random();
  explicit Random(uint32_t seed);

  int RangeInt(int min_inclusive, int max_inclusive);
  float RangeFloat(float min_inclusive, float max_inclusive);
  bool Chance(float probability);

 private:
  std::mt19937 rng_;
};
