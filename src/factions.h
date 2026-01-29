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
  int allianceId = -1;
};

struct AllianceBonus {
  float soldierCapMult = 1.0f;
  int watchtowerCapBonus = 0;
  float defenderCasualtyMult = 1.0f;
  float attackerCasualtyMult = 1.0f;
};

struct Alliance {
  int id = 0;
  std::string name;
  int founderFactionId = -1;
  std::vector<int> members;
  int createdDay = 0;
  int level = 1;
};

struct WarSide {
  std::vector<int> factions;
  int allianceId = -1;
};

struct War {
  int id = 0;
  int declaringFactionId = -1;
  int defendingFactionId = -1;
  WarSide attackers;
  WarSide defenders;
  int startDay = 0;
  int lastMajorEventDay = 0;
  int deathsAttackers = 0;
  int deathsDefenders = 0;
  bool active = false;
};

class FactionManager {
 public:
  int Count() const { return static_cast<int>(factions_.size()); }
  const std::vector<Faction>& Factions() const { return factions_; }
  const Faction* Get(int id) const;
  Faction* GetMutable(int id);
  const std::vector<Alliance>& Alliances() const { return alliances_; }
  const std::vector<War>& Wars() const { return warsList_; }
  const Alliance* GetAlliance(int id) const;
  const War* GetWar(int id) const;
  War* GetWarMutable(int id);
  int ActiveWarIdForFaction(int factionId) const;
  int ActiveWarIdBetweenFactions(int factionA, int factionB) const;
  bool WarIsAttacker(int warId, int factionId) const;
  int CaptureRecipientFaction(int warId, int occupyingFactionId, int targetFactionId) const;
  AllianceBonus BonusForFaction(int factionId, int dayCount) const;

  int CreateFaction(Random& rng);
  void UpdateStats(const SettlementManager& settlements);
  void UpdateLeaders(const SettlementManager& settlements, const HumanManager& humans);
  void UpdateDiplomacy(const SettlementManager& settlements, Random& rng, int dayCount);

  int RelationScore(int factionA, int factionB) const;
  FactionRelation RelationType(int factionA, int factionB) const;
  bool IsAtWar(int factionA, int factionB) const;
  int WarCount() const;
  bool CanExpandInto(int sourceFactionId, int targetFactionId, bool resourceStress) const;
  void SetWar(int factionA, int factionB, bool atWar, int dayCount, int initiatorFactionId = -1);
  void ForceAlliance(int factionA, int factionB, int dayCount) {
    if (factionA == factionB || factionA <= 0 || factionB <= 0) return;
    Faction* a = GetMutable(factionA);
    Faction* b = GetMutable(factionB);
    if (!a || !b) return;

    // Alliances imply peace between members.
    SetWar(factionA, factionB, false, dayCount);

    int allianceA = a->allianceId;
    int allianceB = b->allianceId;
    if (allianceA > 0 && allianceA == allianceB) return;

    if (allianceA > 0 && allianceB > 0) {
      const Alliance* source = GetAlliance(allianceB);
      std::vector<int> moveList = source ? source->members : std::vector<int>{factionB};
      for (int memberId : moveList) {
        RemoveFactionFromAlliance(memberId);
        AddFactionToAlliance(allianceA, memberId);
      }
      RecomputeAllianceLevels(dayCount);
      return;
    }

    if (allianceA > 0) {
      RemoveFactionFromAlliance(factionB);
      AddFactionToAlliance(allianceA, factionB);
      RecomputeAllianceLevels(dayCount);
      return;
    }
    if (allianceB > 0) {
      RemoveFactionFromAlliance(factionA);
      AddFactionToAlliance(allianceB, factionA);
      RecomputeAllianceLevels(dayCount);
      return;
    }

    std::string name = a->name + " Alliance";
    int newAllianceId = CreateAlliance(name, factionA, dayCount);
    AddFactionToAlliance(newAllianceId, factionA);
    AddFactionToAlliance(newAllianceId, factionB);
    RecomputeAllianceLevels(dayCount);
  }

  void ForceLeaveAlliance(int factionId) {
    if (factionId <= 0) return;
    Faction* faction = GetMutable(factionId);
    if (!faction || faction->allianceId <= 0) return;
    RemoveFactionFromAlliance(factionId);
  }
  void SetWarEnabled(bool enabled);
  bool WarEnabled() const { return warEnabled_; }

 private:
  int IndexForId(int id) const;
  void EnsureRelationsForNewFaction(Random& rng);
  void EnsureWarsForNewFaction();
  void ResetStats();
  void UpdateTerritory(const SettlementManager& settlements);
  void UpdateWarExhaustion();
  void UpdateAlliances(const SettlementManager& settlements, Random& rng, int dayCount);
  void UpdateWars(const SettlementManager& settlements, Random& rng, int dayCount);
  void RecomputeAllianceLevels(int dayCount);
  int CreateAlliance(const std::string& name, int founderFactionId, int dayCount);
  bool AddFactionToAlliance(int allianceId, int factionId);
  void RemoveFactionFromAlliance(int factionId);
  void DissolveAlliance(int allianceId);
  bool AnyActiveWarForFaction(int factionId) const;
  int FindWarIndexById(int id) const;
  int FindAllianceIndexById(int id) const;
  int StartWar(int declaringFactionId, int defendingFactionId, int dayCount);
  void EndWarByIndex(int warIndex, int dayCount);
  void SyncWarMatrixFromWars(int dayCount);

  std::vector<Faction> factions_;
  std::vector<int> relations_;
  std::vector<uint8_t> wars_;
  std::vector<int> warDays_;
  std::vector<Alliance> alliances_;
  std::vector<War> warsList_;
  int nextAllianceId_ = 1;
  int nextWarId_ = 1;
  bool warEnabled_ = true;
};
