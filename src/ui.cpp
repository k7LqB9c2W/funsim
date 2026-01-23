#include "ui.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#include <imgui.h>

#include "factions.h"
#include "settlements.h"

namespace {
const ToolType kToolOrder[] = {
    ToolType::SelectKingdom,
    ToolType::PlaceLand,     ToolType::PlaceFreshWater, ToolType::AddTrees,
    ToolType::AddFood,       ToolType::SpawnMale,       ToolType::SpawnFemale,
    ToolType::Fire,          ToolType::Meteor,          ToolType::GiftFood,
};

void CopyToBuf(char* dst, size_t dstSize, const std::string& src) {
  if (!dst || dstSize == 0) return;
  std::strncpy(dst, src.c_str(), dstSize - 1);
  dst[dstSize - 1] = '\0';
}
}  // namespace

void DrawUI(UIState& state, const SimStats& stats, FactionManager& factions,
            const SettlementManager& settlements, const HoverInfo& hover) {
  state.stepDay = false;
  state.saveMap = false;
  state.loadMap = false;
  state.newWorld = false;

  ImGui::Begin("Tools");
  ImGui::Text("Tools");
  for (ToolType tool : kToolOrder) {
    bool selected = (state.tool == tool);
    if (ImGui::Selectable(ToolName(tool), selected)) {
      state.tool = tool;
    }
  }

  ImGui::Separator();
  ImGui::Text("Brush Size");
  ImGui::RadioButton("1", &state.brushSize, 1);
  ImGui::SameLine();
  ImGui::RadioButton("3", &state.brushSize, 3);
  ImGui::SameLine();
  ImGui::RadioButton("5", &state.brushSize, 5);
  ImGui::SameLine();
  ImGui::RadioButton("10", &state.brushSize, 10);
  ImGui::SameLine();
  ImGui::RadioButton("15", &state.brushSize, 15);

  ImGui::Separator();
  ImGui::Text("View");
  const char* overlayNames[] = {"None", "Faction Borders", "Settlement Influence",
                                "Population Heat", "Conflict"};
  int overlayIndex = static_cast<int>(state.overlayMode);
  if (ImGui::Combo("Overlay", &overlayIndex, overlayNames,
                   static_cast<int>(sizeof(overlayNames) / sizeof(overlayNames[0])))) {
    state.overlayMode = static_cast<OverlayMode>(overlayIndex);
  }
  ImGui::Checkbox("Whole Map View", &state.wholeMapView);

  ImGui::Separator();
  ImGui::Text("Map");
  ImGui::InputText("Path", state.mapPath, sizeof(state.mapPath));
  if (ImGui::Button("Save Map")) {
    state.saveMap = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Map")) {
    state.loadMap = true;
  }
  const char* worldSizes[] = {"1x", "4x"};
  ImGui::Combo("New World Size", &state.worldSizeIndex, worldSizes,
               static_cast<int>(sizeof(worldSizes) / sizeof(worldSizes[0])));
  if (ImGui::Button("New World")) {
    state.newWorld = true;
  }

  ImGui::Separator();
  if (ImGui::Button(state.paused ? "Play" : "Pause")) {
    state.paused = !state.paused;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Day")) {
    state.stepDay = true;
  }

  ImGui::Text("Speed");
  if (ImGui::RadioButton("1x", state.speedIndex == 0)) state.speedIndex = 0;
  ImGui::SameLine();
  if (ImGui::RadioButton("5x", state.speedIndex == 1)) state.speedIndex = 1;
  ImGui::SameLine();
  if (ImGui::RadioButton("20x", state.speedIndex == 2)) state.speedIndex = 2;
  ImGui::SameLine();
  if (ImGui::RadioButton("200x", state.speedIndex == 3)) state.speedIndex = 3;
  ImGui::SameLine();
  if (ImGui::RadioButton("2000x", state.speedIndex == 4)) state.speedIndex = 4;

  ImGui::Separator();
  ImGui::Checkbox("Allow War", &state.warEnabled);
  ImGui::Checkbox("Allow Starvation Death", &state.starvationDeathEnabled);
  ImGui::Checkbox("Allow Dehydration Death", &state.dehydrationDeathEnabled);
  ImGui::Separator();
  ImGui::Text("Stats");
  ImGui::Text("Day: %d", stats.dayCount);
  ImGui::Text("Population: %lld", static_cast<long long>(stats.totalPop));
  ImGui::Text("Births Today: %d", stats.birthsToday);
  ImGui::Text("Deaths Today: %d", stats.deathsToday);
  ImGui::Text("Total Births: %lld", static_cast<long long>(stats.totalBirths));
  ImGui::Text("Total Deaths: %lld", static_cast<long long>(stats.totalDeaths));
  ImGui::Text("Total Food: %lld", static_cast<long long>(stats.totalFood));
  ImGui::Text("Total Trees: %lld", static_cast<long long>(stats.totalTrees));
  ImGui::Text("Settlements: %lld", static_cast<long long>(stats.totalSettlements));
  ImGui::Text("Stock Food: %lld", static_cast<long long>(stats.totalStockFood));
  ImGui::Text("Stock Wood: %lld", static_cast<long long>(stats.totalStockWood));
  ImGui::Text("Houses: %lld", static_cast<long long>(stats.totalHouses));
  ImGui::Text("Farms: %lld", static_cast<long long>(stats.totalFarms));
  ImGui::Text("Granaries: %lld", static_cast<long long>(stats.totalGranaries));
  ImGui::Text("Wells: %lld", static_cast<long long>(stats.totalWells));
  ImGui::Text("Town Halls: %lld", static_cast<long long>(stats.totalTownHalls));
  ImGui::Text("Housing Cap: %lld", static_cast<long long>(stats.totalHousingCap));
  ImGui::Text("Villages/Towns/Cities: %lld/%lld/%lld",
              static_cast<long long>(stats.totalVillages),
              static_cast<long long>(stats.totalTowns),
              static_cast<long long>(stats.totalCities));
  ImGui::Text("Soldiers: %lld | Scouts: %lld", static_cast<long long>(stats.totalSoldiers),
              static_cast<long long>(stats.totalScouts));
  ImGui::Text("Legendary: %lld | Wars: %lld", static_cast<long long>(stats.totalLegendary),
              static_cast<long long>(stats.totalWars));

  ImGui::Separator();
  if (state.tool == ToolType::SelectKingdom) {
    ImGui::Text("Left click: select kingdom");
  } else {
    ImGui::Text("Left click: apply tool");
    ImGui::Text("Right click: erase");
  }
  ImGui::End();

  ImGui::Begin("Kingdoms");
  if (hover.valid && hover.settlementId > 0) {
    const Settlement* settlement = settlements.Get(hover.settlementId);
    const Faction* faction = factions.Get(hover.factionId);
    if (settlement && faction) {
      ImVec4 color(faction->color.r / 255.0f, faction->color.g / 255.0f,
                   faction->color.b / 255.0f, 1.0f);
      ImGui::Text("Hover: (%d, %d)", hover.tileX, hover.tileY);
      ImGui::TextColored(color, "%s", faction->name.c_str());
      ImGui::Text("Leader: %s %s", faction->leaderTitle.c_str(), faction->leaderName.c_str());
      ImGui::Text("Ideology: %s", faction->ideology.c_str());
      ImGui::Text("Settlement %d | Pop %d | Stock %d food, %d wood", settlement->id,
                  settlement->population, settlement->stockFood, settlement->stockWood);
      ImGui::Text("Tier: %s | Tech %d | Stability %d", SettlementTierName(settlement->tier),
                  settlement->techTier, settlement->stability);
      ImGui::Text("Border Pressure: %d | War Pressure: %d | Claim Radius %d",
                  settlement->borderPressure, settlement->warPressure, settlement->influenceRadius);
      if (settlement->isCapital) {
        ImGui::Text("Capital Seat");
      }
      if (ImGui::SmallButton("Edit This Kingdom") && faction->id > 0) {
        state.selectedFactionId = faction->id;
        state.factionEditorOpen = true;
      }
      ImGui::Separator();
    }
  }

  if (factions.Count() == 0) {
    ImGui::Text("No kingdoms yet.");
    ImGui::End();
  } else {
    for (const auto& faction : factions.Factions()) {
      ImVec4 color(faction.color.r / 255.0f, faction.color.g / 255.0f,
                   faction.color.b / 255.0f, 1.0f);
      ImGui::Separator();
      ImGui::TextColored(color, "%s", faction.name.c_str());
      ImGui::Text("Leader: %s %s", faction.leaderTitle.c_str(), faction.leaderName.c_str());
      ImGui::Text("Ideology: %s", faction.ideology.c_str());
      ImGui::Text("Traits: %s, %s", FactionTemperamentName(faction.traits.temperament),
                  FactionOutlookName(faction.traits.outlook));
      ImGui::Text("Tech Tier: %d | Stability: %d | War Exhaustion: %.2f", faction.techTier,
                  faction.stability, faction.warExhaustion);
      ImGui::Text("Population: %d", faction.stats.population);
      ImGui::Text("Settlements: %d | Territory Zones: %d", faction.stats.settlements,
                  faction.stats.territoryZones);
      ImGui::Text("Resources: %d food, %d wood", faction.stats.stockFood, faction.stats.stockWood);

      ImGui::PushID(faction.id);
      if (ImGui::SmallButton("Edit")) {
        state.selectedFactionId = faction.id;
        state.factionEditorOpen = true;
      }
      if (ImGui::TreeNode("Relations")) {
        for (const auto& other : factions.Factions()) {
          if (other.id == faction.id) continue;
          int score = factions.RelationScore(faction.id, other.id);
          const char* rel = FactionRelationName(factions.RelationType(faction.id, other.id));
          if (factions.IsAtWar(faction.id, other.id)) {
            ImGui::Text("%s: war (%d)", other.name.c_str(), score);
          } else {
            ImGui::Text("%s: %s (%d)", other.name.c_str(), rel, score);
          }
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::End();
  }

  if (state.factionEditorOpen && state.selectedFactionId > 0) {
    bool open = true;
    Faction* faction = factions.GetMutable(state.selectedFactionId);
    if (!faction) {
      state.selectedFactionId = -1;
      state.factionEditorOpen = false;
      open = false;
    } else {
      if (state.lastFactionEditorId != faction->id) {
        CopyToBuf(state.factionNameBuf, sizeof(state.factionNameBuf), faction->name);
        CopyToBuf(state.factionIdeologyBuf, sizeof(state.factionIdeologyBuf), faction->ideology);
        CopyToBuf(state.factionLeaderNameBuf, sizeof(state.factionLeaderNameBuf), faction->leaderName);
        CopyToBuf(state.factionLeaderTitleBuf, sizeof(state.factionLeaderTitleBuf), faction->leaderTitle);
        state.lastFactionEditorId = faction->id;
      }

      ImGui::Begin("Kingdom Editor", &open);
      ImGui::Text("Editing kingdom #%d", faction->id);

      float color[3] = {faction->color.r / 255.0f, faction->color.g / 255.0f,
                        faction->color.b / 255.0f};
      if (ImGui::ColorEdit3("Color", color)) {
        faction->color.r = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(color[0] * 255.0f))));
        faction->color.g = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(color[1] * 255.0f))));
        faction->color.b = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(color[2] * 255.0f))));
      }

      if (ImGui::InputText("Name", state.factionNameBuf, sizeof(state.factionNameBuf))) {
        faction->name = state.factionNameBuf;
      }
      if (ImGui::InputText("Ideology", state.factionIdeologyBuf, sizeof(state.factionIdeologyBuf))) {
        faction->ideology = state.factionIdeologyBuf;
      }

      if (ImGui::InputText("Leader Name", state.factionLeaderNameBuf,
                           sizeof(state.factionLeaderNameBuf))) {
        faction->leaderName = state.factionLeaderNameBuf;
      }
      if (ImGui::InputText("Leader Title", state.factionLeaderTitleBuf,
                           sizeof(state.factionLeaderTitleBuf))) {
        faction->leaderTitle = state.factionLeaderTitleBuf;
      }

      const char* temperaments[] = {"Pacifist", "Neutral", "Warmonger"};
      int temperament = static_cast<int>(faction->traits.temperament);
      if (ImGui::Combo("Temperament", &temperament, temperaments,
                       static_cast<int>(sizeof(temperaments) / sizeof(temperaments[0])))) {
        faction->traits.temperament = static_cast<FactionTemperament>(temperament);
      }

      const char* outlooks[] = {"Isolationist", "Interactive"};
      int outlook = static_cast<int>(faction->traits.outlook);
      if (ImGui::Combo("Outlook", &outlook, outlooks,
                       static_cast<int>(sizeof(outlooks) / sizeof(outlooks[0])))) {
        faction->traits.outlook = static_cast<FactionOutlook>(outlook);
      }

      ImGui::SliderFloat("Expansion Bias", &faction->traits.expansionBias, 0.2f, 2.0f, "%.2f");
      ImGui::SliderFloat("Aggression Bias", &faction->traits.aggressionBias, 0.0f, 1.5f, "%.2f");
      ImGui::SliderFloat("Diplomacy Bias", &faction->traits.diplomacyBias, 0.0f, 1.5f, "%.2f");

      ImGui::SliderInt("Tech Tier", &faction->techTier, 0, 6);
      ImGui::SliderInt("Stability", &faction->stability, 0, 100);
      ImGui::SliderFloat("War Exhaustion", &faction->warExhaustion, 0.0f, 1.0f, "%.2f");

      ImGui::Separator();
      ImGui::Text("Population: %d | Settlements: %d | Zones: %d", faction->stats.population,
                  faction->stats.settlements, faction->stats.territoryZones);
      ImGui::Text("Stock: %d food, %d wood", faction->stats.stockFood, faction->stats.stockWood);

      ImGui::End();
    }
    if (!open) {
      state.factionEditorOpen = false;
    }
  }

  ImGui::Begin("Settlement Economy");
  if (settlements.Count() == 0) {
    ImGui::Text("No settlements yet.");
    ImGui::End();
    return;
  }

  for (const auto& settlement : settlements.Settlements()) {
    ImGui::Separator();
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      ImVec4 color(faction->color.r / 255.0f, faction->color.g / 255.0f,
                   faction->color.b / 255.0f, 1.0f);
      ImGui::TextColored(color, "Settlement %d (%s)", settlement.id, faction->name.c_str());
    } else {
      ImGui::Text("Settlement %d", settlement.id);
    }

    ImGui::Text("Population: %d | Stock Food: %d | Stock Wood: %d", settlement.population,
                settlement.stockFood, settlement.stockWood);
    ImGui::Text("Tier: %s | Tech %d | Stability %d", SettlementTierName(settlement.tier),
                settlement.techTier, settlement.stability);
    if (settlement.isCapital) {
      ImGui::Text("Capital Seat");
    }
    ImGui::Text("Guards: %d | Soldiers: %d | Scouts: %d", settlement.guards, settlement.soldiers,
                settlement.scouts);
    ImGui::Text("Farms: %d total | %d planted | %d ready", settlement.farms, settlement.farmsPlanted,
                settlement.farmsReady);
    ImGui::Text("Granaries: %d", settlement.granaries);
    ImGui::Text("Farmers: %d | Gatherers: %d", settlement.farmers, settlement.gatherers);

    int harvestTasks = 0;
    int haulDistanceSum = 0;
    int haulDistanceCount = 0;
    for (int idx = settlement.taskHead; idx != settlement.taskTail;
         idx = (idx + 1) % Settlement::kTaskCap) {
      const Task& task = settlement.tasks[idx];
      if (task.type == TaskType::HarvestFarm) {
        harvestTasks++;
      }
      if (task.type == TaskType::HarvestFarm || task.type == TaskType::CollectFood) {
        int dist = std::abs(task.x - settlement.centerX) + std::abs(task.y - settlement.centerY);
        haulDistanceSum += dist;
        haulDistanceCount++;
      }
    }

    float avgHaul = (haulDistanceCount > 0)
                        ? static_cast<float>(haulDistanceSum) / static_cast<float>(haulDistanceCount)
                        : 0.0f;
    ImGui::Text("Harvest Tasks: %d | Avg Haul Dist: %.1f", harvestTasks, avgHaul);
  }
  ImGui::End();

  ImGui::Begin("Legends");
  if (stats.legendaryShown == 0) {
    ImGui::Text("No legendary humans yet.");
  } else {
    for (int i = 0; i < stats.legendaryShown; ++i) {
      const auto& info = stats.legendary[i];
      const Faction* faction = factions.Get(info.factionId);
      const char* factionName = faction ? faction->name.c_str() : "Wanderer";
      ImGui::Separator();
      ImGui::Text("Legend #%d | Age %d", info.id, info.ageDays / 365);
      ImGui::Text("Traits: %s", info.traitsText);
      ImGui::Text("Faction: %s | Settlement %d", factionName, info.settlementId);
    }
  }
  ImGui::End();
}
