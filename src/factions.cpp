#include "factions.h"

#include <algorithm>
#include <cmath>

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
  (void)dayCount;
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

      if (atWar) {
        warDays_[idx]++;
        warDays_[j * count + i] = warDays_[idx];
      } else {
        warDays_[idx] = 0;
        warDays_[j * count + i] = 0;
      }

      if (!atWar) {
        float aggression = (factions_[i].traits.aggressionBias + factions_[j].traits.aggressionBias) * 0.5f;
        aggression += (factions_[i].leaderInfluence.aggression + factions_[j].leaderInfluence.aggression) * 0.5f;
        bool pressure = border >= kWarBorderPressureThreshold;
        bool stressTrigger = (stress[i] > 0.75f || stress[j] > 0.75f);
        if (score <= kRelationHostileThreshold - 5 && (pressure || stressTrigger) &&
            (aggression >= 0.55f || rng.Chance(0.06f))) {
          SetWar(factions_[i].id, factions_[j].id, true);
        }
      } else {
        int daysAtWar = warDays_[idx];
        float peaceChance = 0.02f +
                            (factions_[i].leaderInfluence.diplomacy +
                             factions_[j].leaderInfluence.diplomacy) *
                                0.03f;
        peaceChance = ClampFloat(peaceChance, 0.01f, 0.12f);
        if (daysAtWar > kWarMinDays && score > -20 && rng.Chance(peaceChance)) {
          SetWar(factions_[i].id, factions_[j].id, false);
        }
      }
    }
  }

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
  int count = static_cast<int>(factions_.size());
  int total = 0;
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (IsAtWar(factions_[i].id, factions_[j].id)) {
        total++;
      }
    }
  }
  return total;
}

void FactionManager::SetWar(int factionA, int factionB, bool atWar) {
  if (factionA == factionB || factionA <= 0 || factionB <= 0) return;
  if (!warEnabled_ && atWar) return;
  int indexA = IndexForId(factionA);
  int indexB = IndexForId(factionB);
  if (indexA < 0 || indexB < 0) return;
  int count = static_cast<int>(factions_.size());
  if (static_cast<int>(wars_.size()) != count * count) {
    EnsureWarsForNewFaction();
  }
  wars_[indexA * count + indexB] = atWar ? 1u : 0u;
  wars_[indexB * count + indexA] = atWar ? 1u : 0u;
  int dayValue = atWar ? 1 : 0;
  warDays_[indexA * count + indexB] = dayValue;
  warDays_[indexB * count + indexA] = dayValue;
  if (atWar) {
    relations_[indexA * count + indexB] = std::min(relations_[indexA * count + indexB], -40);
    relations_[indexB * count + indexA] = std::min(relations_[indexB * count + indexA], -40);
  }
}

void FactionManager::SetWarEnabled(bool enabled) {
  if (warEnabled_ == enabled) return;
  warEnabled_ = enabled;
  if (!warEnabled_) {
    std::fill(wars_.begin(), wars_.end(), 0);
    std::fill(warDays_.begin(), warDays_.end(), 0);
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
