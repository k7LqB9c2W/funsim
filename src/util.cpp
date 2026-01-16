#include "util.h"

Random::Random() {
  std::random_device rd;
  rng_.seed(rd());
}

Random::Random(uint32_t seed) : rng_(seed) {}

int Random::RangeInt(int min_inclusive, int max_inclusive) {
  std::uniform_int_distribution<int> dist(min_inclusive, max_inclusive);
  return dist(rng_);
}

float Random::RangeFloat(float min_inclusive, float max_inclusive) {
  std::uniform_real_distribution<float> dist(min_inclusive, max_inclusive);
  return dist(rng_);
}

bool Random::Chance(float probability) {
  if (probability <= 0.0f) return false;
  if (probability >= 1.0f) return true;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng_) < probability;
}
