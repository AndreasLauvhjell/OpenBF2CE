// [CHOSEN, not recovered] Own-use bot sniper tuning (mod/bot-sniper, no upstream issue). Not
// part of the retail AI image: the values are set from the console (aiSettings.sniper*, see
// AIModule.cpp) and read by EntryTrigger::execute and BAPWrapperAiming::execute.

#ifndef AI_AIDLL_AI_MAIN_SNIPERTUNING_H
#define AI_AIDLL_AI_MAIN_SNIPERTUNING_H

#include <cstdlib>

namespace dice {
namespace hfe {
namespace ai {

struct SniperTuning
{
    // Master switch for the range scale (EntryTrigger). 0 keeps retail deviation for snipers too.
    bool accuracyFix;
    // The distance at which a sniper keeps the plain co-op/AI deviation; closer targets stay at
    // the plain value, targets out to the weapon's hold distance get the same angular hit
    // chance as at this distance.
    float referenceRange;
    // Aim at chest height instead of the retail standing aim point (BAPWrapperAiming).
    bool aimChest;
};

// Start values: the defaults, overridden by the environment variables OPENBF2_SNIPER_FIX,
// OPENBF2_SNIPER_REF and OPENBF2_SNIPER_AIM_CHEST when they are set, so a
// start script can configure one server process without editing the shared .ai files.
inline SniperTuning makeSniperTuning()
{
    SniperTuning tuning = { true, 30.0f, true };
    if (const char* value = std::getenv("OPENBF2_SNIPER_FIX"))
        tuning.accuracyFix = std::atoi(value) != 0;
    if (const char* value = std::getenv("OPENBF2_SNIPER_REF"))
        tuning.referenceRange = static_cast<float>(std::atof(value));
    if (const char* value = std::getenv("OPENBF2_SNIPER_AIM_CHEST"))
        tuning.aimChest = std::atoi(value) != 0;
    return tuning;
}

// The single instance, shared by every translation unit of the AI image.
inline SniperTuning& sniperTuning()
{
    static SniperTuning tuning = makeSniperTuning();
    return tuning;
}

} // namespace ai
} // namespace hfe
} // namespace dice

#endif // AI_AIDLL_AI_MAIN_SNIPERTUNING_H
