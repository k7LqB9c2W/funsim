#include "factions.h"

#include <cmath>

#include "humans.h"
#include "settlements.h"
#include "util.h"

namespace {
constexpr int kRelationAllyThreshold = 30;
constexpr int kRelationHostileThreshold = -30;

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

void FactionManager::ResetStats() {
  for (auto& faction : factions_) {
    faction.stats = FactionStats{};
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
  for (const auto& settlement : settlements.Settlements()) {
    if (settlement.factionId <= 0) continue;
    Faction* faction = GetMutable(settlement.factionId);
    if (!faction) continue;
    faction->stats.settlements++;
    faction->stats.population += settlement.population;
    faction->stats.stockFood += settlement.stockFood;
    faction->stats.stockWood += settlement.stockWood;
  }
  UpdateTerritory(settlements);
}

void FactionManager::UpdateLeaders(const SettlementManager& settlements, const HumanManager& humans) {
  if (factions_.empty()) return;

  std::vector<int> bestAge(factions_.size(), -1);
  std::vector<int> bestId(factions_.size(), -1);

  for (const auto& human : humans.Humans()) {
    if (!human.alive) continue;
    if (human.settlementId <= 0) continue;
    const Settlement* settlement = settlements.Get(human.settlementId);
    if (!settlement) continue;
    int factionId = settlement->factionId;
    int index = IndexForId(factionId);
    if (index < 0) continue;
    if (human.ageDays > bestAge[index]) {
      bestAge[index] = human.ageDays;
      bestId[index] = human.id;
    }
  }

  for (size_t i = 0; i < factions_.size(); ++i) {
    Faction& faction = factions_[i];
    if (bestId[i] > 0) {
      faction.leaderId = bestId[i];
      faction.leaderName = MakeLeaderNameFromId(bestId[i]);
    } else if (faction.leaderName.empty() || faction.leaderName == "Unassigned") {
      faction.leaderId = -1;
      faction.leaderName = "Council";
    }
  }
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

bool FactionManager::CanExpandInto(int sourceFactionId, int targetFactionId,
                                   bool resourceStress) const {
  if (sourceFactionId <= 0 || targetFactionId <= 0) return true;
  if (sourceFactionId == targetFactionId) return true;

  FactionRelation relation = RelationType(sourceFactionId, targetFactionId);
  if (relation != FactionRelation::Hostile) return true;

  const Faction* source = Get(sourceFactionId);
  if (!source) return false;
  if (source->traits.aggressionBias >= 0.9f) return true;
  if (resourceStress && source->traits.aggressionBias >= 0.65f) return true;
  return false;
}
