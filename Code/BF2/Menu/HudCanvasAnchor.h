// [CHOSEN, not recovered] Widescreen HUD anchoring for the retail 800x600 HUD scripts.
// Retail has no anchoring: every HUD node keeps its 800x600 position, stretched over the screen.
// Here the HUD canvas follows the window aspect and HUD groups are moved towards the edges of the
// wider canvas with the cross rule: a group fully on one side of a centre line moves to that side.
#ifndef MENU_HUDCANVASANCHOR_H
#define MENU_HUDCANVASANCHOR_H
#include "../Core/BuildConfig.h"
#if !DEDICATED
#include "../Math/Vec3.h"
#include <string>
#include <unordered_map>

namespace dice { namespace meme { class System; class Node; } }
namespace dice { namespace hfe { namespace hud {

// How far the Ingame scene's transform nodes (Global, TopLayer and the areas, all 800x600-based)
// extend their pointer hit test: left, top, right, bottom in HUD units. Without it clicks on
// anything moved outside the old 800x600 block never reach its buttons.
inline float* hudSceneHitExpand()
{
    static float expand[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    return expand;
}

// Per-axis deltas for nodes whose position is rewritten every frame from a variable.
// Nodes with a static position are moved once instead and do not appear here.
inline std::unordered_map<const void*, hfe::Vec2>& hudAnchorFrameDeltas()
{
    static std::unordered_map<const void*, hfe::Vec2> deltas;
    return deltas;
}

inline const hfe::Vec2* hudAnchorFrameDelta(const void* node)
{
    std::unordered_map<const void*, hfe::Vec2>& deltas = hudAnchorFrameDeltas();
    if (deltas.empty()) return nullptr;
    const auto it = deltas.find(node);
    return it == deltas.end() ? nullptr : &it->second;
}

// Returns the move for a rectangle given in 800x600 HUD coordinates (origin top left).
hfe::Vec2 hudAnchorRectDelta(float x, float y, float width, float height);

// Records a node registered by the HUD builder, in creation order.
void hudAnchorRecordNode(const std::string& parent, meme::Node* node);

// Moves the built-in HUD areas and the recorded groups. Called once after the HUD scripts ran.
void hudAnchorApply(meme::System* system);

} } }
#endif
#endif
