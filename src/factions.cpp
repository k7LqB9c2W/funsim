#include "factions.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "humans.h"
#include "settlements.h"
#include "util.h"

namespace {
constexpr int kRelationAllyThreshold = 30;
constexpr int kRelationHostileThreshold = -30;
constexpr int kWarBorderPressureThreshold = 4;
constexpr int kWarMinDays = 30;
constexpr float kWarExhaustionGain = 0.02f;
constexpr float kWarExhaustionRecover = 0.015f;

const FactionColor kFactionPalette[] = {
    {230, 83, 77},   {242, 164, 68}, {248, 207, 92}, {120, 196, 109},
    {78, 176, 186},  {91, 139, 220}, {158, 108, 230}, {210, 86, 164},
    {208, 115, 82},  {156, 182, 92}, {88, 168, 132}, {188, 188, 196},
};

const char* kNamePrefixes[] = {"Iron", "River", "Stone", "Silver", "Golden", "Ash", "High",
                               "Amber", "North", "South", "East", "West", "Wind", "Sun",
                               "Moon", "Red", "Green", "Blue", "Gray", "Bright", "Deep"};
const char* kNameSuffixes[] = {"Kingdom", "Realm", "Union", "Tribe", "Hold", "Dominion",
                               "March", "League", "Council", "Throne", "Clans", "Reach"};

const char* kLeaderTitlePacifist[] = {"Elder", "Caretaker", "Sage", "Speaker"};
const char* kLeaderTitleNeutral[] = {"Steward", "Regent", "Chief", "Warden"};
const char* kLeaderTitleWarmonger[] = {"Warlord", "High Marshal", "Steel King", "Iron Queen"};

const char* kIdeologies[] = {"Agrarian", "Maritime", "Crafted", "Militarist", "Mercantile",
                             "Spiritual", "Expansionist", "Isolationist", "Scholastic",
                             "Frontier", "Harmony", "Order"};

const char* kLeaderFirst[] = {"Arin", "Bela", "Cal", "Dorin", "Elara", "Fenn", "Garin", "Hala",
                              "Ira", "Jora", "Korin", "Lysa", "Mara", "Nolan", "Orin", "Pera"};
const char* kLeaderLast[] = {"Stone", "Ridge", "River", "Ash", "Vale", "Crest", "Bloom", "Hollow",
                             "Glen", "Forge", "Dawn", "Pike", "Flint", "Shade"};

uint32_t Hash32(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B9u;
  h ^= b * 0x85EBCA6Bu;
  h ^= (h >> 13);
  h *= 0xC2B2AE35u;
  h ^= (h >> 16);
  return h;
}

FactionColor PickFactionColor(int index) {
  constexpr int paletteCount = static_cast<int>(sizeof(kFactionPalette) / sizeof(kFactionPalette[0]));
  if (paletteCount <= 0) {
    return FactionColor{180, 180, 180};
  }
  return kFactionPalette[index % paletteCount];
}

FactionTemperament RandomTemperament(Random& rng) {
  int roll = rng.RangeInt(0, 2);
  if (roll == 0) return FactionTemperament::Pacifist;
  if (roll == 1) return FactionTemperament::Neutral;
  return FactionTemperament::Warmonger;
}

FactionOutlook RandomOutlook(Random& rng) {
  int roll = rng.RangeInt(0, 1);
  return (roll == 0) ? FactionOutlook::Isolationist : FactionOutlook::Interactive;
}

std::string MakeFactionName(Random& rng) {
  const int prefixCount = static_cast<int>(sizeof(kNamePrefixes) / sizeof(kNamePrefixes[0]));
  const int suffixCount = static_cast<int>(sizeof(kNameSuffixes) / sizeof(kNameSuffixes[0]));
  const char* prefix = kNamePrefixes[rng.RangeInt(0, prefixCount - 1)];
  const char* suffix = kNameSuffixes[rng.RangeInt(0, suffixCount - 1)];
  return std::string(prefix) + " " + suffix;
}

std::string MakeLeaderNameFromId(int id) {
  const int firstCount = static_cast<int>(sizeof(kLeaderFirst) / sizeof(kLeaderFirst[0]));
  const int lastCount = static_cast<int>(sizeof(kLeaderLast) / sizeof(kLeaderLast[0]));
  uint32_t h = Hash32(static_cast<uint32_t>(id), 0xB5297A4Du);
  const char* first = kLeaderFirst[h % firstCount];
  const char* last = kLeaderLast[(h >> 8) % lastCount];
  return std::string(first) + " " + last;
}

std::string PickLeaderTitle(const FactionTraits& traits, Random& rng) {
  const char* const* titles = kLeaderTitleNeutral;
  int count = static_cast<int>(sizeof(kLeaderTitleNeutral) / sizeof(kLeaderTitleNeutral[0]));
  if (traits.temperament == FactionTemperament::Pacifist) {
    titles = kLeaderTitlePacifist;
    count = static_cast<int>(sizeof(kLeaderTitlePacifist) / sizeof(kLeaderTitlePacifist[0]));
  } else if (traits.temperament == FactionTemperament::Warmonger) {
    titles = kLeaderTitleWarmonger;
    count = static_cast<int>(sizeof(kLeaderTitleWarmonger) / sizeof(kLeaderTitleWarmonger[0]));
  }
  return std::string(titles[rng.RangeInt(0, count - 1)]);
}

std::string PickIdeology(Random& rng) {
  const int count = static_cast<int>(sizeof(kIdeologies) / sizeof(kIdeologies[0]));
  return std::string(kIdeologies[rng.RangeInt(0, count - 1)]);
}

int ClampRelation(int score) {
  if (score > 100) return 100;
  if (score < -100) return -100;
  return score;
}

int RelationBiasFromTraits(const FactionTraits& traits) {
  float bias = (traits.diplomacyBias - 0.5f) * 40.0f;
  bias -= traits.aggressionBias * 25.0f;
  return static_cast<int>(std::round(bias));
}

float ClampInfluence(float value) {
  if (value > 0.5f) return 0.5f;
  if (value < -0.5f) return -0.5f;
  return value;
}

float ClampFloat(float value, float min_value, float max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}

LeaderInfluence InfluenceFromHuman(const Human& human) {
  LeaderInfluence influence;
  if (HumanHasTrait(human.traits, HumanTrait::Wise)) {
    influence.diplomacy += 0.18f;
    influence.tech += 0.22f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Brave)) {
    influence.aggression += 0.18f;
    influence.stability += 0.08f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Ambitious)) {
    influence.expansion += 0.18f;
    influence.aggression += 0.06f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Kind)) {
    influence.diplomacy += 0.14f;
    influence.stability += 0.12f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Greedy)) {
    influence.diplomacy -= 0.12f;
    influence.expansion += 0.06f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Lazy)) {
    influence.expansion -= 0.18f;
    influence.tech -= 0.12f;
  }
  if (HumanHasTrait(human.traits, HumanTrait::Curious)) {
    influence.tech += 0.16f;
    influence.expansion += 0.08f;
  }
  if (human.legendary) {
    influence.legendary = true;
    influence.expansion += 0.12f;
    influence.tech += 0.18f;
    influence.stability += 0.12f;
  }

  influence.expansion = ClampInfluence(influence.expansion);
  influence.aggression = ClampInfluence(influence.aggression);
  influence.diplomacy = ClampInfluence(influence.diplomacy);
  influence.stability = ClampInfluence(influence.stability);
  influence.tech = ClampInfluence(influence.tech);
  return influence;
}
}  // namespace

const char* FactionTemperamentName(FactionTemperament temperament) {
  switch (temperament) {
    case FactionTemperament::Pacifist:
      return "pacifist";
    case FactionTemperament::Neutral:
      return "neutral";
    case FactionTemperament::Warmonger:
      return "warmonger";
    default:
      return "unknown";
  }
}

const char* FactionOutlookName(FactionOutlook outlook) {
  switch (outlook) {
    case FactionOutlook::Isolationist:
      return "isolationist";
    case FactionOutlook::Interactive:
      return "interactive";
    default:
      return "unknown";
  }
}

const char* FactionRelationName(FactionRelation relation) {
  switch (relation) {
    case FactionRelation::Ally:
      return "ally";
    case FactionRelation::Neutral:
      return "neutral";
    case FactionRelation::Hostile:
      return "hostile";
    default:
      return "unknown";
  }
}

int FactionManager::IndexForId(int id) const {
  if (id <= 0) return -1;
  int index = id - 1;
  if (index < 0 || index >= static_cast<int>(factions_.size())) return -1;
  return index;
}

const Faction* FactionManager::Get(int id) const {
  int index = IndexForId(id);
  if (index < 0) return nullptr;
  return &factions_[index];
}

Faction* FactionManager::GetMutable(int id) {
  int index = IndexForId(id);
  if (index < 0) return nullptr;
  return &factions_[index];
}

int FactionManager::CreateFaction(Random& rng) {
  Faction faction;
  faction.id = static_cast<int>(factions_.size()) + 1;
  faction.name = MakeFactionName(rng);
  faction.color = PickFactionColor(faction.id - 1);
  faction.traits.temperament = RandomTemperament(rng);
  faction.traits.outlook = RandomOutlook(rng);
  faction.traits.expansionBias = (faction.traits.temperament == FactionTemperament::Pacifist) ? 0.8f
                             : (faction.traits.temperament == FactionTemperament::Warmonger) ? 1.25f
                                                                                             : 1.0f;
  faction.traits.aggressionBias = (faction.traits.temperament == FactionTemperament::Pacifist) ? 0.2f
                             : (faction.traits.temperament == FactionTemperament::Warmonger) ? 0.85f
                                                                                             : 0.5f;
  faction.traits.diplomacyBias = (faction.traits.outlook == FactionOutlook::Interactive) ? 0.7f : 0.3f;
  faction.leaderTitle = PickLeaderTitle(faction.traits, rng);
  faction.leaderName = "Unassigned";
  faction.ideology = PickIdeology(rng);

  factions_.push_back(faction);
  EnsureRelationsForNewFaction(rng);
  EnsureWarsForNewFaction();
  return faction.id;
}

void FactionManager::EnsureRelationsForNewFaction(Random& rng) {
  int count = static_cast<int>(factions_.size());
  if (count == 0) return;

  int oldCount = count - 1;
  std::vector<int> next(count * count, 0);

  for (int y = 0; y < oldCount; ++y) {
    for (int x = 0; x < oldCount; ++x) {
      next[y * count + x] = relations_[y * oldCount + x];
    }
  }

  for (int i = 0; i < count; ++i) {
    next[i * count + i] = 100;
  }

  const Faction& added = factions_[count - 1];
  int addedBias = RelationBiasFromTraits(added.traits);

  for (int i = 0; i < oldCount; ++i) {
    const Faction& other = factions_[i];
    int base = rng.RangeInt(-40, 40);
    int score = base + addedBias + RelationBiasFromTraits(other.traits) / 2;
    score = ClampRelation(score);
    next[(count - 1) * count + i] = score;
    next[i * count + (count - 1)] = score;
  }

  relations_.swap(next);
}

void FactionManager::EnsureWarsForNewFaction() {
  int count = static_cast<int>(factions_.size());
  if (count == 0) return;

  int oldCount = count - 1;
  std::vector<uint8_t> nextWars(count * count, 0);
  std::vector<int> nextDays(count * count, 0);
  for (int y = 0; y < oldCount; ++y) {
    for (int x = 0; x < oldCount; ++x) {
      nextWars[y * count + x] = wars_.empty() ? 0 : wars_[y * oldCount + x];
      nextDays[y * count + x] = warDays_.empty() ? 0 : warDays_[y * oldCount + x];
    }
  }
  for (int i = 0; i < count; ++i) {
    nextWars[i * count + i] = 0;
    nextDays[i * count + i] = 0;
  }
  wars_.swap(nextWars);
  warDays_.swap(nextDays);
}

void FactionManager::ResetStats() {
  for (auto& faction : factions_) {
    faction.stats = FactionStats{};
    faction.techTier = 0;
    faction.stability = 0;
  }
}

void FactionManager::UpdateTerritory(const SettlementManager& settlements) {
  int zonesX = settlements.ZonesX();
  int zonesY = settlements.ZonesY();
  if (zonesX <= 0 || zonesY <= 0) return;

  for (int zy = 0; zy < zonesY; ++zy) {
    for (int zx = 0; zx < zonesX; ++zx) {
      int ownerId = settlements.ZoneOwnerAt(zx, zy);
      if (ownerId <= 0) continue;
      const Settlement* settlement = settlements.Get(ownerId);
      if (!settlement) continue;
      Faction* faction = GetMutable(settlement->factionId);
      if (!faction) continue;
      faction->stats.territoryZones++;
    }
  }
}

void FactionManager::UpdateStats(const SettlementManager& settlements) {
  ResetStats();
  std::vector<int> stabilityCounts(factions_.size(), 0);
  for (const auto& settlement : settlements.Settlements()) {
    if (settlement.factionId <= 0) continue;
    Faction* faction = GetMutable(settlement.factionId);
    if (!faction) continue;
    int index = IndexForId(settlement.factionId);
    if (index >= 0 && index < static_cast<int>(stabilityCounts.size())) {
      faction->stability += settlement.stability;
      stabilityCounts[index]++;
    }
    faction->stats.settlements++;
    faction->stats.population += settlement.population;
    faction->stats.stockFood += settlement.stockFood;
    faction->stats.stockWood += settlement.stockWood;
    faction->techTier = std::max(faction->techTier, settlement.techTier);
  }
  for (size_t i = 0; i < factions_.size(); ++i) {
    if (stabilityCounts[i] > 0) {
      factions_[i].stability =
          static_cast<int>(std::round(static_cast<float>(factions_[i].stability) /
                                      static_cast<float>(stabilityCounts[i])));
    } else {
      factions_[i].stability = 100;
    }
  }
  UpdateTerritory(settlements);
}

void FactionManager::UpdateLeaders(const SettlementManager& settlements, const HumanManager& humans) {
  if (factions_.empty()) return;

  std::vector<int> bestAge(factions_.size(), -1);
  std::vector<int> bestIndex(factions_.size(), -1);

  for (int i = 0; i < static_cast<int>(humans.Humans().size()); ++i) {
    const auto& human = humans.Humans()[i];
    if (!human.alive) continue;
    if (human.settlementId <= 0) continue;
    const Settlement* settlement = settlements.Get(human.settlementId);
    if (!settlement) continue;
    int factionId = settlement->factionId;
    int index = IndexForId(factionId);
    if (index < 0) continue;
    if (human.ageDays > bestAge[index]) {
      bestAge[index] = human.ageDays;
      bestIndex[index] = i;
    }
  }

  for (size_t i = 0; i < factions_.size(); ++i) {
    Faction& faction = factions_[i];
    if (bestIndex[i] >= 0) {
      const auto& human = humans.Humans()[bestIndex[i]];
      faction.leaderId = human.id;
      faction.leaderName = MakeLeaderNameFromId(human.id);
      faction.leaderInfluence = InfluenceFromHuman(human);
    } else {
      if (faction.leaderName.empty() || faction.leaderName == "Unassigned") {
        faction.leaderId = -1;
        faction.leaderName = "Council";
      }
      faction.leaderInfluence = LeaderInfluence{};
    }
  }
}

void FactionManager::UpdateDiplomacy(const SettlementManager& settlements, Random& rng, int dayCount) {
  int count = static_cast<int>(factions_.size());
  if (count == 0) return;

  if (static_cast<int>(relations_.size()) != count * count) {
    relations_.assign(count * count, 0);
    for (int i = 0; i < count; ++i) {
      relations_[i * count + i] = 100;
    }
  }
  if (static_cast<int>(wars_.size()) != count * count) {
    EnsureWarsForNewFaction();
  }

  std::vector<int> borderPressure(count * count, 0);
  int zonesX = settlements.ZonesX();
  int zonesY = settlements.ZonesY();
  if (zonesX > 0 && zonesY > 0) {
    for (int zy = 0; zy < zonesY; ++zy) {
      for (int zx = 0; zx < zonesX; ++zx) {
        int ownerId = settlements.ZoneOwnerAt(zx, zy);
        if (ownerId <= 0) continue;
        const Settlement* ownerSettlement = settlements.Get(ownerId);
        if (!ownerSettlement || ownerSettlement->factionId <= 0) continue;
        int factionA = ownerSettlement->factionId;
        int indexA = IndexForId(factionA);
        if (indexA < 0) continue;

        auto handleNeighbor = [&](int nx, int ny) {
          if (nx < 0 || ny < 0 || nx >= zonesX || ny >= zonesY) return;
          int neighborOwner = settlements.ZoneOwnerAt(nx, ny);
          if (neighborOwner <= 0 || neighborOwner == ownerId) return;
          const Settlement* neighborSettlement = settlements.Get(neighborOwner);
          if (!neighborSettlement || neighborSettlement->factionId <= 0) return;
          int factionB = neighborSettlement->factionId;
          if (factionA == factionB) return;
          int indexB = IndexForId(factionB);
          if (indexB < 0) return;
          borderPressure[indexA * count + indexB]++;
          borderPressure[indexB * count + indexA]++;
        };

        handleNeighbor(zx + 1, zy);
        handleNeighbor(zx, zy + 1);
      }
    }
  }

  std::vector<float> stress(count, 0.0f);
  for (int i = 0; i < count; ++i) {
    const Faction& faction = factions_[i];
    int pop = faction.stats.population;
    float foodRatio =
        (pop > 0) ? static_cast<float>(faction.stats.stockFood) /
                         static_cast<float>(std::max(1, pop * 30))
                  : 1.0f;
    float woodRatio =
        (pop > 0) ? static_cast<float>(faction.stats.stockWood) /
                         static_cast<float>(std::max(1, pop * 4))
                  : 1.0f;
    float supply = (foodRatio + woodRatio) * 0.5f;
    supply = ClampFloat(supply, 0.0f, 1.2f);
    float value = 1.0f - (supply / 1.2f);
    if (faction.warExhaustion > 0.4f) {
      value += 0.1f;
    }
    stress[i] = ClampFloat(value, 0.0f, 1.5f);
  }

  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      int idx = i * count + j;
      int score = relations_[idx];
      int border = borderPressure[idx];
      int delta = 0;
      if (border > 0) {
        delta -= std::min(border, 6);
      } else {
        delta += 1;
      }
      if (stress[i] > 0.7f || stress[j] > 0.7f) {
        delta -= 2;
      }

      float dipBias = factions_[i].leaderInfluence.diplomacy + factions_[j].leaderInfluence.diplomacy;
      float aggrBias =
          factions_[i].leaderInfluence.aggression + factions_[j].leaderInfluence.aggression;
      delta += static_cast<int>(std::round(dipBias * 4.0f));
      delta -= static_cast<int>(std::round(aggrBias * 4.0f));

      bool atWar = IsAtWar(factions_[i].id, factions_[j].id);
      if (atWar) {
        delta -= 1;
      }

      score = ClampRelation(score + delta);
      relations_[idx] = score;
      relations_[j * count + i] = score;
    }
  }

  UpdateAlliances(settlements, rng, dayCount);
  UpdateWars(settlements, rng, dayCount);
  SyncWarMatrixFromWars(dayCount);
  UpdateWarExhaustion();
}

int FactionManager::RelationScore(int factionA, int factionB) const {
  if (factionA == factionB && factionA > 0) return 100;
  int indexA = IndexForId(factionA);
  int indexB = IndexForId(factionB);
  if (indexA < 0 || indexB < 0) return 0;
  int count = static_cast<int>(factions_.size());
  return relations_[indexA * count + indexB];
}

FactionRelation FactionManager::RelationType(int factionA, int factionB) const {
  int score = RelationScore(factionA, factionB);
  if (score >= kRelationAllyThreshold) return FactionRelation::Ally;
  if (score <= kRelationHostileThreshold) return FactionRelation::Hostile;
  return FactionRelation::Neutral;
}

bool FactionManager::IsAtWar(int factionA, int factionB) const {
  if (!warEnabled_) return false;
  if (factionA == factionB && factionA > 0) return false;
  int indexA = IndexForId(factionA);
  int indexB = IndexForId(factionB);
  if (indexA < 0 || indexB < 0) return false;
  int count = static_cast<int>(factions_.size());
  return wars_.empty() ? false : wars_[indexA * count + indexB] != 0;
}

int FactionManager::WarCount() const {
  int total = 0;
  for (const auto& war : warsList_) {
    if (war.active) total++;
  }
  return total;
}

void FactionManager::SetWar(int factionA, int factionB, bool atWar, int dayCount,
                            int initiatorFactionId) {
  if (factionA == factionB || factionA <= 0 || factionB <= 0) return;
  if (!warEnabled_ && atWar) return;
  if (!Get(factionA) || !Get(factionB)) return;

  if (atWar) {
    int declarer = (initiatorFactionId > 0) ? initiatorFactionId : factionA;
    int defender = (declarer == factionA) ? factionB : factionA;
    StartWar(declarer, defender, dayCount);
  } else {
    int warId = ActiveWarIdBetweenFactions(factionA, factionB);
    int warIndex = (warId > 0) ? FindWarIndexById(warId) : -1;
    if (warIndex >= 0) {
      EndWarByIndex(warIndex, dayCount);
    }
  }

  SyncWarMatrixFromWars(dayCount);
}

void FactionManager::SetWarEnabled(bool enabled) {
  if (warEnabled_ == enabled) return;
  warEnabled_ = enabled;
  if (!warEnabled_) {
    std::fill(wars_.begin(), wars_.end(), 0);
    std::fill(warDays_.begin(), warDays_.end(), 0);
    warsList_.clear();
  }
}

bool FactionManager::CanExpandInto(int sourceFactionId, int targetFactionId,
                                   bool resourceStress) const {
  if (sourceFactionId <= 0 || targetFactionId <= 0) return true;
  if (sourceFactionId == targetFactionId) return true;
  if (IsAtWar(sourceFactionId, targetFactionId)) return true;

  FactionRelation relation = RelationType(sourceFactionId, targetFactionId);
  if (relation != FactionRelation::Hostile) return true;

  const Faction* source = Get(sourceFactionId);
  if (!source) return false;
  float aggression = source->traits.aggressionBias + source->leaderInfluence.aggression;
  if (aggression >= 0.9f) return true;
  if (resourceStress && aggression >= 0.65f) return true;
  return false;
}

void FactionManager::UpdateWarExhaustion() {
  int count = static_cast<int>(factions_.size());
  for (int i = 0; i < count; ++i) {
    bool atWar = false;
    for (int j = 0; j < count; ++j) {
      if (i == j) continue;
      if (IsAtWar(factions_[i].id, factions_[j].id)) {
        atWar = true;
        break;
      }
    }
    if (atWar) {
      factions_[i].warExhaustion = ClampFloat(factions_[i].warExhaustion + kWarExhaustionGain, 0.0f, 1.0f);
    } else {
      factions_[i].warExhaustion =
          ClampFloat(factions_[i].warExhaustion - kWarExhaustionRecover, 0.0f, 1.0f);
    }
  }
}

const Alliance* FactionManager::GetAlliance(int id) const {
  if (id <= 0) return nullptr;
  int index = FindAllianceIndexById(id);
  return (index >= 0) ? &alliances_[index] : nullptr;
}

const War* FactionManager::GetWar(int id) const {
  if (id <= 0) return nullptr;
  int index = FindWarIndexById(id);
  return (index >= 0) ? &warsList_[index] : nullptr;
}

War* FactionManager::GetWarMutable(int id) {
  if (id <= 0) return nullptr;
  int index = FindWarIndexById(id);
  return (index >= 0) ? &warsList_[index] : nullptr;
}

int FactionManager::FindWarIndexById(int id) const {
  if (id <= 0) return -1;
  for (int i = 0; i < static_cast<int>(warsList_.size()); ++i) {
    if (warsList_[i].id == id) return i;
  }
  return -1;
}

int FactionManager::FindAllianceIndexById(int id) const {
  if (id <= 0) return -1;
  for (int i = 0; i < static_cast<int>(alliances_.size()); ++i) {
    if (alliances_[i].id == id) return i;
  }
  return -1;
}

bool FactionManager::AnyActiveWarForFaction(int factionId) const {
  if (factionId <= 0) return false;
  for (const auto& war : warsList_) {
    if (!war.active) continue;
    if (std::find(war.attackers.factions.begin(), war.attackers.factions.end(), factionId) !=
            war.attackers.factions.end() ||
        std::find(war.defenders.factions.begin(), war.defenders.factions.end(), factionId) !=
            war.defenders.factions.end()) {
      return true;
    }
  }
  return false;
}

int FactionManager::ActiveWarIdForFaction(int factionId) const {
  if (factionId <= 0) return -1;
  for (const auto& war : warsList_) {
    if (!war.active) continue;
    if (std::find(war.attackers.factions.begin(), war.attackers.factions.end(), factionId) !=
            war.attackers.factions.end() ||
        std::find(war.defenders.factions.begin(), war.defenders.factions.end(), factionId) !=
            war.defenders.factions.end()) {
      return war.id;
    }
  }
  return -1;
}

int FactionManager::ActiveWarIdBetweenFactions(int factionA, int factionB) const {
  if (factionA <= 0 || factionB <= 0 || factionA == factionB) return -1;
  for (const auto& war : warsList_) {
    if (!war.active) continue;
    bool aAttacker = std::find(war.attackers.factions.begin(), war.attackers.factions.end(), factionA) !=
                     war.attackers.factions.end();
    bool aDefender = std::find(war.defenders.factions.begin(), war.defenders.factions.end(), factionA) !=
                     war.defenders.factions.end();
    bool bAttacker = std::find(war.attackers.factions.begin(), war.attackers.factions.end(), factionB) !=
                     war.attackers.factions.end();
    bool bDefender = std::find(war.defenders.factions.begin(), war.defenders.factions.end(), factionB) !=
                     war.defenders.factions.end();
    if ((aAttacker && bDefender) || (aDefender && bAttacker)) return war.id;
  }
  return -1;
}

bool FactionManager::WarIsAttacker(int warId, int factionId) const {
  const War* war = GetWar(warId);
  if (!war || !war->active) return false;
  return std::find(war->attackers.factions.begin(), war->attackers.factions.end(), factionId) !=
         war->attackers.factions.end();
}

int FactionManager::CaptureRecipientFaction(int warId, int occupyingFactionId,
                                            int targetFactionId) const {
  const War* war = GetWar(warId);
  if (!war || !war->active) return occupyingFactionId;
  bool occAttacker = WarIsAttacker(warId, occupyingFactionId);
  bool targetAttacker = WarIsAttacker(warId, targetFactionId);
  if (occAttacker && !targetAttacker) {
    return (war->declaringFactionId > 0) ? war->declaringFactionId : occupyingFactionId;
  }
  if (!occAttacker && targetAttacker) {
    return (war->defendingFactionId > 0) ? war->defendingFactionId : occupyingFactionId;
  }
  return occupyingFactionId;
}

AllianceBonus FactionManager::BonusForFaction(int factionId, int dayCount) const {
  AllianceBonus bonus;
  const Faction* faction = Get(factionId);
  if (!faction || faction->allianceId <= 0) return bonus;
  const Alliance* alliance = GetAlliance(faction->allianceId);
  if (!alliance) return bonus;

  int ageDays = std::max(0, dayCount - alliance->createdDay);
  int years = ageDays / Human::kDaysPerYear;
  int level = 1;
  if (years >= 100) {
    level = 5;
  } else if (years >= 50) {
    level = 4;
  } else if (years >= 25) {
    level = 3;
  } else if (years >= 10) {
    level = 2;
  }

  bonus.soldierCapMult = 1.0f + 0.05f * static_cast<float>(level);
  bonus.defenderCasualtyMult = (level >= 3) ? 0.92f : 1.0f;
  bonus.attackerCasualtyMult = (level >= 4) ? 1.08f : 1.0f;
  return bonus;
}

void FactionManager::RecomputeAllianceLevels(int dayCount) {
  for (auto& alliance : alliances_) {
    int ageDays = std::max(0, dayCount - alliance.createdDay);
    int years = ageDays / Human::kDaysPerYear;
    int level = 1;
    if (years >= 100) {
      level = 5;
    } else if (years >= 50) {
      level = 4;
    } else if (years >= 25) {
      level = 3;
    } else if (years >= 10) {
      level = 2;
    }
    alliance.level = level;
  }
}

int FactionManager::CreateAlliance(const std::string& name, int founderFactionId, int dayCount) {
  Alliance alliance;
  alliance.id = nextAllianceId_++;
  alliance.name = name;
  alliance.founderFactionId = founderFactionId;
  alliance.createdDay = dayCount;
  alliance.level = 1;
  alliances_.push_back(alliance);
  return alliance.id;
}

bool FactionManager::AddFactionToAlliance(int allianceId, int factionId) {
  if (allianceId <= 0 || factionId <= 0) return false;
  Faction* faction = GetMutable(factionId);
  if (!faction) return false;
  if (faction->allianceId == allianceId) return true;
  if (faction->allianceId > 0) return false;
  int idx = FindAllianceIndexById(allianceId);
  if (idx < 0) return false;
  Alliance& alliance = alliances_[idx];
  if (std::find(alliance.members.begin(), alliance.members.end(), factionId) != alliance.members.end()) {
    faction->allianceId = allianceId;
    return true;
  }
  alliance.members.push_back(factionId);
  faction->allianceId = allianceId;
  return true;
}

void FactionManager::RemoveFactionFromAlliance(int factionId) {
  Faction* faction = GetMutable(factionId);
  if (!faction || faction->allianceId <= 0) return;
  int allianceId = faction->allianceId;
  int idx = FindAllianceIndexById(allianceId);
  faction->allianceId = -1;
  if (idx < 0) return;
  Alliance& alliance = alliances_[idx];
  alliance.members.erase(std::remove(alliance.members.begin(), alliance.members.end(), factionId),
                         alliance.members.end());
  if (alliance.members.empty()) {
    DissolveAlliance(allianceId);
  }
}

void FactionManager::DissolveAlliance(int allianceId) {
  int idx = FindAllianceIndexById(allianceId);
  if (idx < 0) return;
  Alliance& alliance = alliances_[idx];
  for (int memberId : alliance.members) {
    Faction* faction = GetMutable(memberId);
    if (faction && faction->allianceId == allianceId) {
      faction->allianceId = -1;
    }
  }
  alliances_.erase(alliances_.begin() + idx);
}

void FactionManager::UpdateAlliances(const SettlementManager& settlements, Random& rng, int dayCount) {
  (void)settlements;
  if (factions_.size() < 2) return;

  RecomputeAllianceLevels(dayCount);

  int supremeFactionId = -1;
  int bestPower = std::numeric_limits<int>::min();
  for (const auto& faction : factions_) {
    int power = faction.stats.population + faction.stats.settlements * 40 + faction.stats.territoryZones / 2;
    if (power > bestPower) {
      bestPower = power;
      supremeFactionId = faction.id;
    }
  }

  auto canAllianceAct = [&](int factionId) {
    if (factionId <= 0) return false;
    if (factionId == supremeFactionId) return false;
    if (AnyActiveWarForFaction(factionId)) return false;
    return true;
  };

  for (auto& faction : factions_) {
    if (faction.id == supremeFactionId) continue;
    if (faction.allianceId > 0) continue;
    if (!canAllianceAct(faction.id)) continue;

    if (!rng.Chance(0.02f)) continue;

    int bestCandidate = -1;
    int bestScore = -9999;
    for (const auto& other : factions_) {
      if (other.id == faction.id) continue;
      if (other.allianceId > 0) continue;
      if (!canAllianceAct(other.id)) continue;
      int score = RelationScore(faction.id, other.id);
      if (score < kRelationAllyThreshold + 5) continue;
      if (score > bestScore) {
        bestScore = score;
        bestCandidate = other.id;
      }
    }
    if (bestCandidate <= 0) continue;
    std::string name = faction.name + " Alliance";
    int allianceId = CreateAlliance(name, faction.id, dayCount);
    AddFactionToAlliance(allianceId, faction.id);
    AddFactionToAlliance(allianceId, bestCandidate);
  }

  for (auto& faction : factions_) {
    if (faction.allianceId > 0) continue;
    if (!canAllianceAct(faction.id)) continue;
    if (!rng.Chance(0.03f)) continue;

    int chosenAllianceId = -1;
    int bestScore = -9999;
    for (const auto& alliance : alliances_) {
      if (alliance.members.size() >= 6) continue;
      int founder = alliance.founderFactionId;
      int score = (founder > 0) ? RelationScore(faction.id, founder) : 0;
      if (score < kRelationAllyThreshold) continue;
      if (score > bestScore) {
        bestScore = score;
        chosenAllianceId = alliance.id;
      }
    }
    if (chosenAllianceId > 0) {
      AddFactionToAlliance(chosenAllianceId, faction.id);
    }
  }
}

int FactionManager::StartWar(int declaringFactionId, int defendingFactionId, int dayCount) {
  if (!warEnabled_) return -1;
  if (declaringFactionId <= 0 || defendingFactionId <= 0 || declaringFactionId == defendingFactionId) {
    return -1;
  }
  if (!Get(declaringFactionId) || !Get(defendingFactionId)) return -1;
  if (ActiveWarIdBetweenFactions(declaringFactionId, defendingFactionId) > 0) return -1;

  int attAlliance = Get(declaringFactionId)->allianceId;
  int defAlliance = Get(defendingFactionId)->allianceId;
  if (attAlliance > 0 && attAlliance == defAlliance) return -1;

  War war;
  war.id = nextWarId_++;
  war.declaringFactionId = declaringFactionId;
  war.defendingFactionId = defendingFactionId;
  war.startDay = dayCount;
  war.lastMajorEventDay = dayCount;
  war.active = true;
  war.attackers.allianceId = attAlliance;
  war.defenders.allianceId = defAlliance;

  if (attAlliance > 0) {
    const Alliance* alliance = GetAlliance(attAlliance);
    if (alliance) war.attackers.factions = alliance->members;
  } else {
    war.attackers.factions.push_back(declaringFactionId);
  }
  if (defAlliance > 0) {
    const Alliance* alliance = GetAlliance(defAlliance);
    if (alliance) war.defenders.factions = alliance->members;
  } else {
    war.defenders.factions.push_back(defendingFactionId);
  }

  auto uniqueSort = [](std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  uniqueSort(war.attackers.factions);
  uniqueSort(war.defenders.factions);

  if (war.attackers.factions.empty() || war.defenders.factions.empty()) return -1;
  for (int f : war.attackers.factions) {
    if (AnyActiveWarForFaction(f)) return -1;
  }
  for (int f : war.defenders.factions) {
    if (AnyActiveWarForFaction(f)) return -1;
  }

  for (int a : war.attackers.factions) {
    for (int d : war.defenders.factions) {
      int ia = IndexForId(a);
      int id = IndexForId(d);
      if (ia < 0 || id < 0) continue;
      int count = static_cast<int>(factions_.size());
      relations_[ia * count + id] = std::min(relations_[ia * count + id], -40);
      relations_[id * count + ia] = std::min(relations_[id * count + ia], -40);
    }
  }

  warsList_.push_back(war);
  return war.id;
}

void FactionManager::EndWarByIndex(int warIndex, int dayCount) {
  if (warIndex < 0 || warIndex >= static_cast<int>(warsList_.size())) return;
  War& war = warsList_[warIndex];
  if (!war.active) return;
  war.active = false;
  war.lastMajorEventDay = dayCount;
}

void FactionManager::UpdateWars(const SettlementManager& settlements, Random& rng, int dayCount) {
  if (!warEnabled_) return;
  if (factions_.size() < 2) return;

  std::vector<int> settlementCount(factions_.size() + 1, 0);
  std::vector<int> soldierCount(factions_.size() + 1, 0);
  for (const auto& settlement : settlements.Settlements()) {
    if (settlement.factionId <= 0) continue;
    if (settlement.factionId >= static_cast<int>(settlementCount.size())) continue;
    settlementCount[settlement.factionId]++;
    soldierCount[settlement.factionId] += settlement.soldiers;
  }

  for (int wi = 0; wi < static_cast<int>(warsList_.size()); ++wi) {
    War& war = warsList_[wi];
    if (!war.active) continue;

    bool attackersHaveSettlements = false;
    bool defendersHaveSettlements = false;
    for (int f : war.attackers.factions) {
      if (f > 0 && f < static_cast<int>(settlementCount.size()) && settlementCount[f] > 0) {
        attackersHaveSettlements = true;
        break;
      }
    }
    for (int f : war.defenders.factions) {
      if (f > 0 && f < static_cast<int>(settlementCount.size()) && settlementCount[f] > 0) {
        defendersHaveSettlements = true;
        break;
      }
    }
    if (!attackersHaveSettlements || !defendersHaveSettlements) {
      EndWarByIndex(wi, dayCount);
      continue;
    }

    int duration = std::max(0, dayCount - war.startDay);
    int score = (war.declaringFactionId > 0 && war.defendingFactionId > 0)
                    ? RelationScore(war.declaringFactionId, war.defendingFactionId)
                    : -40;
    float peaceChance = 0.0f;
    if (duration > kWarMinDays && score > -20) {
      const Faction* a = Get(war.declaringFactionId);
      const Faction* b = Get(war.defendingFactionId);
      float dip = 0.0f;
      if (a) dip += a->leaderInfluence.diplomacy;
      if (b) dip += b->leaderInfluence.diplomacy;
      peaceChance = ClampFloat(0.01f + dip * 0.04f, 0.01f, 0.12f);
      if (rng.Chance(peaceChance)) {
        EndWarByIndex(wi, dayCount);
        continue;
      }
    }
  }

  int count = static_cast<int>(factions_.size());
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      int factionA = factions_[i].id;
      int factionB = factions_[j].id;
      if (factionA <= 0 || factionB <= 0) continue;
      if (AnyActiveWarForFaction(factionA) || AnyActiveWarForFaction(factionB)) continue;
      if (factions_[i].allianceId > 0 && factions_[i].allianceId == factions_[j].allianceId) continue;
      int idx = i * count + j;
      if (idx >= 0 && idx < static_cast<int>(warDays_.size()) && warDays_[idx] < 0) continue;
      if (settlementCount[factionA] <= 0 || settlementCount[factionB] <= 0) continue;

      int score = relations_[idx];
      if (score > kRelationHostileThreshold - 5) continue;

      int soldiersA = soldierCount[factionA];
      int soldiersB = soldierCount[factionB];
      if (soldiersA <= 0 || soldiersB <= 0) continue;
      int maxSoldiers = std::max(soldiersA, soldiersB);
      if (maxSoldiers > 0) {
        int minSoldiers = std::min(soldiersA, soldiersB);
        if (minSoldiers * 10 < maxSoldiers * 7) continue;
      }

      float aggression =
          (factions_[i].traits.aggressionBias + factions_[j].traits.aggressionBias) * 0.5f;
      aggression += (factions_[i].leaderInfluence.aggression + factions_[j].leaderInfluence.aggression) * 0.5f;
      if (aggression < 0.55f && !rng.Chance(0.03f)) continue;

      int declaring = factionA;
      int defending = factionB;
      float aAgg = factions_[i].traits.aggressionBias + factions_[i].leaderInfluence.aggression;
      float bAgg = factions_[j].traits.aggressionBias + factions_[j].leaderInfluence.aggression;
      if (bAgg > aAgg + 0.05f) {
        declaring = factionB;
        defending = factionA;
      }

      StartWar(declaring, defending, dayCount);
      if (AnyActiveWarForFaction(factionA) || AnyActiveWarForFaction(factionB)) {
        SyncWarMatrixFromWars(dayCount);
      }
    }
  }
}

void FactionManager::SyncWarMatrixFromWars(int dayCount) {
  int count = static_cast<int>(factions_.size());
  if (count <= 0) return;
  if (static_cast<int>(wars_.size()) != count * count || static_cast<int>(warDays_.size()) != count * count) {
    EnsureWarsForNewFaction();
  }

  std::vector<uint8_t> next(count * count, 0);
  for (const auto& war : warsList_) {
    if (!war.active) continue;
    for (int a : war.attackers.factions) {
      for (int d : war.defenders.factions) {
        int ia = IndexForId(a);
        int id = IndexForId(d);
        if (ia < 0 || id < 0) continue;
        next[ia * count + id] = 1u;
        next[id * count + ia] = 1u;
      }
    }
  }

  constexpr int kWarCooldownDays = 120;

  for (int i = 0; i < count; ++i) {
    next[i * count + i] = 0u;
  }

  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < count; ++j) {
      if (i == j) continue;
      int idx = i * count + j;
      bool was = wars_[idx] != 0;
      bool now = next[idx] != 0;
      if (now) {
        if (was && warDays_[idx] > 0) {
          warDays_[idx] += 1;
        } else {
          warDays_[idx] = 1;
        }
      } else {
        if (was && warDays_[idx] > 0) {
          warDays_[idx] = -kWarCooldownDays;
        } else if (warDays_[idx] < 0) {
          warDays_[idx] = std::min(0, warDays_[idx] + 1);
        } else {
          warDays_[idx] = 0;
        }
      }
    }
  }

  wars_.swap(next);
  (void)dayCount;
}
