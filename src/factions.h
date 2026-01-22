#pragma once

#include <cstdint>
#include <string>
#include <vector>

class HumanManager;
class Random;
class SettlementManager;

enum class FactionTemperament : uint8_t { Pacifist, Neutral, Warmonger };
enum class FactionOutlook : uint8_t { Isolationist, Interactive };
enum class FactionRelation : uint8_t { Ally, Neutral, Hostile };

const char* FactionTemperamentName(FactionTemperament temperament);
const char* FactionOutlookName(FactionOutlook outlook);
const char* FactionRelationName(FactionRelation relation);

struct FactionTraits {
  FactionTemperament temperament = FactionTemperament::Neutral;
  FactionOutlook outlook = FactionOutlook::Interactive;
  float expansionBias = 1.0f;
  float aggressionBias = 0.5f;
  float diplomacyBias = 0.5f;
};

struct FactionStats {
  int population = 0;
  int settlements = 0;
  int territoryZones = 0;
  int stockFood = 0;
  int stockWood = 0;
};

struct LeaderInfluence {
  float expansion = 0.0f;
  float aggression = 0.0f;
  float diplomacy = 0.0f;
  float stability = 0.0f;
  float tech = 0.0f;
  bool legendary = false;
};

struct FactionColor {
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
};

struct Faction {
  int id = 0;
  std::string name;
  FactionColor color;
  int leaderId = -1;
  std::string leaderName;
  std::string leaderTitle;
  std::string ideology;
  FactionTraits traits;
  FactionStats stats;
  int techTier = 0;
  float techProgress = 0.0f;
  float warExhaustion = 0.0f;
  int stability = 100;
  LeaderInfluence leaderInfluence;
};

class FactionManager {
 public:
  int Count() const { return static_cast<int>(factions_.size()); }
  const std::vector<Faction>& Factions() const { return factions_; }
  const Faction* Get(int id) const;
  Faction* GetMutable(int id);

  int CreateFaction(Random& rng);
  void UpdateStats(const SettlementManager& settlements);
  void UpdateLeaders(const SettlementManager& settlements, const HumanManager& humans);
  void UpdateDiplomacy(const SettlementManager& settlements, Random& rng, int dayCount);

  int RelationScore(int factionA, int factionB) const;
  FactionRelation RelationType(int factionA, int factionB) const;
  bool IsAtWar(int factionA, int factionB) const;
  int WarCount() const;
  bool CanExpandInto(int sourceFactionId, int targetFactionId, bool resourceStress) const;
  void SetWar(int factionA, int factionB, bool atWar);
  void SetWarEnabled(bool enabled);
  bool WarEnabled() const { return warEnabled_; }

 private:
  int IndexForId(int id) const;
  void EnsureRelationsForNewFaction(Random& rng);
  void EnsureWarsForNewFaction();
  void ResetStats();
  void UpdateTerritory(const SettlementManager& settlements);
  void UpdateWarExhaustion();

  std::vector<Faction> factions_;
  std::vector<int> relations_;
  std::vector<uint8_t> wars_;
  std::vector<int> warDays_;
  bool warEnabled_ = true;
};
