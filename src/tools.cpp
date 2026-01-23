#include "tools.h"

const char* ToolName(ToolType tool) {
  switch (tool) {
    case ToolType::SelectKingdom:
      return "Select Kingdom";
    case ToolType::PlaceLand:
      return "Place Land";
    case ToolType::PlaceFreshWater:
      return "Place FreshWater";
    case ToolType::AddTrees:
      return "Add Trees";
    case ToolType::AddFood:
      return "Add Food";
    case ToolType::SpawnMale:
      return "Spawn Male";
    case ToolType::SpawnFemale:
      return "Spawn Female";
    case ToolType::Fire:
      return "Ignite Fire";
    case ToolType::Meteor:
      return "Meteor Strike";
    case ToolType::GiftFood:
      return "Gift Food";
    default:
      return "Unknown";
  }
}
