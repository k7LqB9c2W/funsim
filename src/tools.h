#pragma once

enum class ToolType {
  SelectKingdom,
  PlaceLand,
  PlaceFreshWater,
  AddTrees,
  AddFood,
  SpawnMale,
  SpawnFemale,
  Fire,
  Meteor,
  GiftFood,
};

const char* ToolName(ToolType tool);
