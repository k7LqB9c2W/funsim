#pragma once

enum class ToolType {
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
