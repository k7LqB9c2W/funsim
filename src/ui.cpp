#include "ui.h"

#include <imgui.h>

#include "factions.h"
#include "settlements.h"

namespace {
const ToolType kToolOrder[] = {
    ToolType::PlaceLand,     ToolType::PlaceFreshWater, ToolType::AddTrees,
    ToolType::AddFood,       ToolType::SpawnMale,       ToolType::SpawnFemale,
    ToolType::Fire,          ToolType::Meteor,          ToolType::GiftFood,
};
}  // namespace

void DrawUI(UIState& state, const SimStats& stats, const FactionManager& factions,
            const SettlementManager& settlements, const HoverInfo& hover) {
  state.stepDay = false;

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

  ImGui::Separator();
  ImGui::Text("View");
  ImGui::Checkbox("Show Territory", &state.showTerritoryOverlay);

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
  ImGui::Text("Stats");
  ImGui::Text("Day: %d", stats.dayCount);
  ImGui::Text("Population: %d", stats.totalPop);
  ImGui::Text("Births Today: %d", stats.birthsToday);
  ImGui::Text("Deaths Today: %d", stats.deathsToday);
  ImGui::Text("Total Births: %d", stats.totalBirths);
  ImGui::Text("Total Deaths: %d", stats.totalDeaths);
  ImGui::Text("Total Food: %d", stats.totalFood);
  ImGui::Text("Total Trees: %d", stats.totalTrees);
  ImGui::Text("Settlements: %d", stats.totalSettlements);
  ImGui::Text("Stock Food: %d", stats.totalStockFood);
  ImGui::Text("Stock Wood: %d", stats.totalStockWood);
  ImGui::Text("Houses: %d", stats.totalHouses);
  ImGui::Text("Farms: %d", stats.totalFarms);
  ImGui::Text("Town Halls: %d", stats.totalTownHalls);
  ImGui::Text("Housing Cap: %d", stats.totalHousingCap);

  ImGui::Separator();
  ImGui::Text("Left click: apply tool");
  ImGui::Text("Right click: erase");
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
      ImGui::Separator();
    }
  }

  if (factions.Count() == 0) {
    ImGui::Text("No kingdoms yet.");
    ImGui::End();
    return;
  }

  for (const auto& faction : factions.Factions()) {
    ImVec4 color(faction.color.r / 255.0f, faction.color.g / 255.0f,
                 faction.color.b / 255.0f, 1.0f);
    ImGui::Separator();
    ImGui::TextColored(color, "%s", faction.name.c_str());
    ImGui::Text("Leader: %s %s", faction.leaderTitle.c_str(), faction.leaderName.c_str());
    ImGui::Text("Ideology: %s", faction.ideology.c_str());
    ImGui::Text("Traits: %s, %s", FactionTemperamentName(faction.traits.temperament),
                FactionOutlookName(faction.traits.outlook));
    ImGui::Text("Population: %d", faction.stats.population);
    ImGui::Text("Settlements: %d | Territory Zones: %d", faction.stats.settlements,
                faction.stats.territoryZones);
    ImGui::Text("Resources: %d food, %d wood", faction.stats.stockFood, faction.stats.stockWood);

    ImGui::PushID(faction.id);
    if (ImGui::TreeNode("Relations")) {
      for (const auto& other : factions.Factions()) {
        if (other.id == faction.id) continue;
        int score = factions.RelationScore(faction.id, other.id);
        const char* rel = FactionRelationName(factions.RelationType(faction.id, other.id));
        ImGui::Text("%s: %s (%d)", other.name.c_str(), rel, score);
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::End();
}
