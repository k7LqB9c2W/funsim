#pragma once

enum class ToolType {
  SelectKingdom,
  PlaceGrass,
  PlaceSand,
  PlaceFreshWater,
  AddTrees,
  AddFood,
  SpawnMale,
  SpawnFemale,
  Fire,
  Meteor,
  GiftFood,
  InspireResearch,
  StartDisease,
};

const char* ToolName(ToolType tool);
