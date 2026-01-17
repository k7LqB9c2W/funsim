#include "ui.h"

#include <imgui.h>

namespace {
const ToolType kToolOrder[] = {
    ToolType::PlaceLand,     ToolType::PlaceFreshWater, ToolType::AddTrees,
    ToolType::AddFood,       ToolType::SpawnMale,       ToolType::SpawnFemale,
    ToolType::Fire,          ToolType::Meteor,          ToolType::GiftFood,
};
}  // namespace

void DrawUI(UIState& state, const SimStats& stats) {
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
  ImGui::Text("Total Food: %d", stats.totalFood);
  ImGui::Text("Total Trees: %d", stats.totalTrees);
  ImGui::Text("Settlements: %d", stats.totalSettlements);

  ImGui::Separator();
  ImGui::Text("Left click: apply tool");
  ImGui::Text("Right click: erase");
  ImGui::End();
}
