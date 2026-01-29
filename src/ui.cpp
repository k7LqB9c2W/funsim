#include "ui.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#include <imgui.h>

#include "factions.h"
#include "humans.h"
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

const Human* FindHumanById(const HumanManager& humans, int id) {
  if (id <= 0) return nullptr;
  for (const auto& human : humans.Humans()) {
    if (!human.alive) continue;
    if (human.id == id) return &human;
  }
  return nullptr;
}
}  // namespace

void DrawUI(UIState& state, const SimStats& stats, FactionManager& factions,
            const SettlementManager& settlements, const HumanManager& humans, const HoverInfo& hover) {
  state.stepDay = false;
  state.saveMap = false;
  state.loadMap = false;
  state.newWorld = false;
  state.requestArmyOrdersRefresh = false;

  ImGui::Begin("Tools");
  ImGui::Text("Tools");
  ImGui::Separator();
  ImGui::Text("Debug");
  ImGui::Checkbox("War Debug Window", &state.warDebugOpen);
  ImGui::Separator();
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
  ImGui::Checkbox("Allow Rebellions", &state.rebellionsEnabled);
  ImGui::Checkbox("Allow Starvation Death", &state.starvationDeathEnabled);
  ImGui::Checkbox("Allow Dehydration Death", &state.dehydrationDeathEnabled);
  ImGui::Separator();
  ImGui::Text("War Visuals");
  ImGui::Checkbox("War Zone Glow", &state.showWarZones);
  ImGui::Checkbox("War Arrows", &state.showWarArrows);
  ImGui::Checkbox("Troop Counts", &state.showTroopCounts);
  if (state.showTroopCounts) {
    ImGui::SameLine();
    ImGui::Checkbox("All Zones", &state.showTroopCountsAllZones);
  }
  ImGui::Separator();
  ImGui::Text("War Logging");
  ImGui::Checkbox("Write war_debug.csv", &state.warLoggingEnabled);
  if (state.warLoggingEnabled) {
    ImGui::SameLine();
    ImGui::Checkbox("Only Selected", &state.warLogOnlySelected);
    ImGui::Text("Files: war_debug.csv, war_events.csv");
  }
  ImGui::Separator();
  ImGui::Text("Overlay Tuning");
  ImGui::SliderInt("Territory Alpha", &state.territoryOverlayAlpha, 0, 200);
  ImGui::SliderFloat("Territory Darken", &state.territoryOverlayDarken, 0.2f, 1.0f, "%.2f");
  ImGui::Separator();
  ImGui::Text("Stats");
  ImGui::Text("Day: %d", stats.dayCount);
  ImGui::Text("Population: %lld", static_cast<long long>(stats.totalPop));
  ImGui::Text("Births (last step): %d", stats.birthsToday);
  ImGui::Text("Deaths (last step): %d", stats.deathsToday);
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
      ImGui::Text("Army: %d soldiers | General: %s", settlement->soldiers,
                  (settlement->generalHumanId > 0) ? "yes" : "no");
      ImGui::Text("Watchtowers: %d", settlement->watchtowers);
      if (settlement->warId > 0) {
        ImGui::Text("War #%d | Target settlement %d", settlement->warId, settlement->warTargetSettlementId);
      }
      if (settlement->captureProgress > 0.0f) {
        const Faction* capFaction =
            (settlement->captureLeaderFactionId > 0) ? factions.Get(settlement->captureLeaderFactionId) : nullptr;
        ImGui::Text("Capture: %.1f%% by %s", settlement->captureProgress,
                    capFaction ? capFaction->name.c_str() : "unknown");
      }
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
      if (faction.allianceId > 0) {
        const Alliance* alliance = factions.GetAlliance(faction.allianceId);
        if (alliance) {
          ImGui::Text("Alliance: %s (L%d) | Members: %d", alliance->name.c_str(), alliance->level,
                      static_cast<int>(alliance->members.size()));
        } else {
          ImGui::Text("Alliance: #%d", faction.allianceId);
        }
      }

      ImGui::PushID(faction.id);
      if (ImGui::SmallButton("Edit")) {
        state.selectedFactionId = faction.id;
        state.factionEditorOpen = true;
      }
      if (ImGui::TreeNode("Wars")) {
        for (const auto& war : factions.Wars()) {
          if (!war.active) continue;
          bool involved =
              std::find(war.attackers.factions.begin(), war.attackers.factions.end(), faction.id) !=
                  war.attackers.factions.end() ||
              std::find(war.defenders.factions.begin(), war.defenders.factions.end(), faction.id) !=
                  war.defenders.factions.end();
          if (!involved) continue;
          const char* side = factions.WarIsAttacker(war.id, faction.id) ? "attackers" : "defenders";
          ImGui::Text("War #%d (%s) | Days %d | Deaths A/D %d/%d", war.id, side,
                      std::max(0, stats.dayCount - war.startDay), war.deathsAttackers, war.deathsDefenders);
        }
        ImGui::TreePop();
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

      ImGui::Separator();
      ImGui::Text("Diplomacy (Force)");
      if (state.diplomacyOtherFactionId <= 0 || state.diplomacyOtherFactionId == faction->id ||
          !factions.Get(state.diplomacyOtherFactionId)) {
        state.diplomacyOtherFactionId = -1;
        for (const auto& other : factions.Factions()) {
          if (other.id != faction->id) {
            state.diplomacyOtherFactionId = other.id;
            break;
          }
        }
      }
      const Faction* otherFaction = factions.Get(state.diplomacyOtherFactionId);
      const char* otherName = otherFaction ? otherFaction->name.c_str() : "None";
      if (ImGui::BeginCombo("Target Kingdom", otherName)) {
        for (const auto& other : factions.Factions()) {
          if (other.id == faction->id) continue;
          bool selected = (state.diplomacyOtherFactionId == other.id);
          if (ImGui::Selectable(other.name.c_str(), selected)) {
            state.diplomacyOtherFactionId = other.id;
          }
        }
        ImGui::EndCombo();
      }

      if (otherFaction) {
        int score = factions.RelationScore(faction->id, otherFaction->id);
        const char* rel = FactionRelationName(factions.RelationType(faction->id, otherFaction->id));
        bool atWar = factions.IsAtWar(faction->id, otherFaction->id);
        ImGui::Text("Current: %s | Score %d | %s", atWar ? "war" : rel, score,
                    (faction->allianceId > 0 && faction->allianceId == otherFaction->allianceId) ? "same alliance"
                                                                                                 : "");

        if (ImGui::Button("Force War")) {
          factions.SetWar(faction->id, otherFaction->id, true, stats.dayCount, faction->id);
          state.requestArmyOrdersRefresh = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Force Peace")) {
          factions.SetWar(faction->id, otherFaction->id, false, stats.dayCount);
          state.requestArmyOrdersRefresh = true;
        }
        if (ImGui::Button("Force Alliance")) {
          factions.ForceAlliance(faction->id, otherFaction->id, stats.dayCount);
          state.requestArmyOrdersRefresh = true;
        }
      }

      if (faction->allianceId > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Leave Alliance")) {
          factions.ForceLeaveAlliance(faction->id);
          state.requestArmyOrdersRefresh = true;
        }
      }

      ImGui::End();
    }
    if (!open) {
      state.factionEditorOpen = false;
    }
  }

  if (state.warDebugOpen) {
    bool open = true;
    ImGui::Begin("War Debug", &open);
    ImGui::Checkbox("Follow Hover", &state.warDebugFollowHover);
    if (ImGui::Button("Reissue Army Orders")) {
      state.requestArmyOrdersRefresh = true;
    }
    ImGui::SameLine();
    ImGui::Text("wantsMacro=%s speed=%d", (state.speedIndex == 4) ? "yes" : "no", state.speedIndex);
    if (state.warDebugFollowHover && hover.valid) {
      if (hover.settlementId > 0) state.warDebugSettlementId = hover.settlementId;
      if (hover.factionId > 0) state.warDebugFactionId = hover.factionId;
    }
    ImGui::Text("Hover: tile (%d,%d) settlement %d faction %d",
                hover.valid ? hover.tileX : -1, hover.valid ? hover.tileY : -1,
                hover.valid ? hover.settlementId : -1, hover.valid ? hover.factionId : -1);

    ImGui::InputInt("Settlement Id", &state.warDebugSettlementId);
    ImGui::InputInt("Faction Id", &state.warDebugFactionId);

    ImGui::Separator();
    ImGui::Text("Active wars: %d", factions.WarCount());
    for (const auto& war : factions.Wars()) {
      if (!war.active) continue;
      ImGui::BulletText("War #%d days %d deaths A/D %d/%d (decl %d def %d)", war.id,
                        std::max(0, stats.dayCount - war.startDay), war.deathsAttackers, war.deathsDefenders,
                        war.declaringFactionId, war.defendingFactionId);
    }

    if (state.warDebugSettlementId > 0) {
      const Settlement* settlement = settlements.Get(state.warDebugSettlementId);
      if (!settlement) {
        ImGui::Text("Settlement %d not found.", state.warDebugSettlementId);
      } else {
        const Faction* fac = factions.Get(settlement->factionId);
        int warId = (settlement->factionId > 0) ? factions.ActiveWarIdForFaction(settlement->factionId) : -1;
        bool attackerSide = (warId > 0 && factions.WarIsAttacker(warId, settlement->factionId));
        ImGui::Separator();
        ImGui::Text("Settlement %d (%s) center (%d,%d)", settlement->id,
                    fac ? fac->name.c_str() : "no faction", settlement->centerX, settlement->centerY);
        ImGui::Text("Pop %d soldiers %d border %d warPressure %d", settlement->population, settlement->soldiers,
                    settlement->borderPressure, settlement->warPressure);
        ImGui::Text("Stock: food %d wood %d | Stability %d unrest %d", settlement->stockFood, settlement->stockWood,
                    settlement->stability, settlement->unrest);
        ImGui::Text("Role targets: F%d G%d B%d Guard%d Soldier%d Scout%d Idle%d",
                    settlement->debugTargetFarmers, settlement->debugTargetGatherers, settlement->debugTargetBuilders,
                    settlement->debugTargetGuards, settlement->debugTargetSoldiers, settlement->debugTargetScouts,
                    settlement->debugTargetIdle);
        ImGui::Text("FoodEmergency=%s soldiersPreEmergency=%d warFloor=%d",
                    settlement->debugFoodEmergency ? "yes" : "no", settlement->debugSoldiersPreEmergency,
                    settlement->debugWarSoldierFloor);
        ImGui::Text("WarId %d (%s) targetSettlement %d capture %.1f%%", warId,
                    (warId > 0 ? (attackerSide ? "attacker" : "defender") : "none"),
                    settlement->warTargetSettlementId, settlement->captureProgress);
        if (settlement->hasDefenseTarget) {
          ImGui::Text("Defense target (%d,%d)", settlement->defenseTargetX, settlement->defenseTargetY);
        }

        const Human* general = FindHumanById(humans, settlement->generalHumanId);
        if (general) {
          ImGui::Text("General #%d pos (%d,%d) state %s", general->id, general->x, general->y,
                      ArmyStateName(general->armyState));
        } else {
          ImGui::Text("General: none");
        }

        int totalSoldiers = 0;
        int roleCounts[7] = {};
        int stateCounts[6] = {};
        int soldiersInEnemyTerritory = 0;
        int soldiersInTargetTerritory = 0;

        for (const auto& h : humans.Humans()) {
          if (!h.alive) continue;
          if (h.settlementId != settlement->id) continue;
          int roleIndex = static_cast<int>(h.role);
          if (roleIndex >= 0 && roleIndex < 7) roleCounts[roleIndex]++;
          if (h.role != Role::Soldier) continue;
          totalSoldiers++;
          int stateIndex = static_cast<int>(h.armyState);
          if (stateIndex >= 0 && stateIndex < 6) {
            stateCounts[stateIndex]++;
          }

          int ownerSettlementId = settlements.ZoneOwnerForTile(h.x, h.y);
          if (ownerSettlementId > 0 && ownerSettlementId != settlement->id) {
            const Settlement* owner = settlements.Get(ownerSettlementId);
            if (owner && owner->factionId > 0 && factions.IsAtWar(settlement->factionId, owner->factionId)) {
              soldiersInEnemyTerritory++;
              if (ownerSettlementId == settlement->warTargetSettlementId) {
                soldiersInTargetTerritory++;
              }
            }
          }
        }

        ImGui::Text("Soldiers tracked: %d", totalSoldiers);
        ImGui::Text("Role counts: idle %d gather %d farm %d build %d guard %d soldier %d scout %d",
                    roleCounts[0], roleCounts[1], roleCounts[2], roleCounts[3], roleCounts[4], roleCounts[5],
                    roleCounts[6]);
        ImGui::Text("ArmyState counts: idle %d rally %d march %d siege %d defend %d retreat %d",
                    stateCounts[0], stateCounts[1], stateCounts[2], stateCounts[3], stateCounts[4], stateCounts[5]);
        ImGui::Text("In enemy territory: %d (in target: %d)", soldiersInEnemyTerritory, soldiersInTargetTerritory);
      }
    }

    if (state.warDebugFactionId > 0) {
      const Faction* fac = factions.Get(state.warDebugFactionId);
      if (fac) {
        int warId = factions.ActiveWarIdForFaction(fac->id);
        bool attackerSide = (warId > 0 && factions.WarIsAttacker(warId, fac->id));
        ImGui::Separator();
        ImGui::Text("Faction %d (%s)", fac->id, fac->name.c_str());
        ImGui::Text("WarId %d (%s) settlements %d pop %d stockFood %d", warId,
                    (warId > 0 ? (attackerSide ? "attacker" : "defender") : "none"),
                    fac->stats.settlements, fac->stats.population, fac->stats.stockFood);

        int stateCounts[6] = {};
        int totalSoldiers = 0;
        int inEnemyTerritory = 0;
        for (const auto& h : humans.Humans()) {
          if (!h.alive) continue;
          if (h.role != Role::Soldier) continue;
          const Settlement* home = (h.settlementId > 0) ? settlements.Get(h.settlementId) : nullptr;
          if (!home || home->factionId != fac->id) continue;
          totalSoldiers++;
          int stateIndex = static_cast<int>(h.armyState);
          if (stateIndex >= 0 && stateIndex < 6) stateCounts[stateIndex]++;
          int ownerSettlementId = settlements.ZoneOwnerForTile(h.x, h.y);
          if (ownerSettlementId > 0) {
            const Settlement* owner = settlements.Get(ownerSettlementId);
            if (owner && owner->factionId > 0 && factions.IsAtWar(fac->id, owner->factionId)) {
              inEnemyTerritory++;
            }
          }
        }
        ImGui::Text("Soldiers tracked: %d (in enemy territory: %d)", totalSoldiers, inEnemyTerritory);
        ImGui::Text("ArmyState counts: idle %d rally %d march %d siege %d defend %d retreat %d",
                    stateCounts[0], stateCounts[1], stateCounts[2], stateCounts[3], stateCounts[4], stateCounts[5]);
      } else {
        ImGui::Text("Faction %d not found.", state.warDebugFactionId);
      }
    }

    ImGui::End();
    if (!open) state.warDebugOpen = false;
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

void DrawUI(UIState& state, const SimStats& stats, FactionManager& factions,
            const SettlementManager& settlements, const HoverInfo& hover) {
  static HumanManager dummyHumans;
  DrawUI(state, stats, factions, settlements, dummyHumans, hover);
}
