// [CHOSEN, not recovered] The server's kill feed policy. See KillfeedPolicy.h.

#include "KillfeedPolicy.h"
#include "../Common/OpenBF2Channel.h"

namespace dice {
namespace hfe {
namespace killfeed_policy {

namespace {

KillfeedPolicy& mutablePolicy()
{
    static KillfeedPolicy value = {true, true, true, true, {0, 0, 0}, {0, 0, 0}};
    return value;
}

int clampRange(int metres) { return metres < 0 ? 0 : (metres > 65535 ? 65535 : metres); }

// Re-announces the capabilities (distance / vehicle sending may have switched) and the policy itself.
void killfeedPolicyChanged()
{
    openbf2_channel::serverCapabilitiesChanged();
    openbf2_channel::serverKillfeedPolicyChanged();
}

} // namespace

const KillfeedPolicy& killfeedPolicy()  { return mutablePolicy(); }
bool killfeedSendsDistance()            { return mutablePolicy().distanceShow || mutablePolicy().allowPersonalKillfeed; }
bool killfeedSendsVehicle()             { return mutablePolicy().vehicleShow || mutablePolicy().allowPersonalKillfeed; }
void setKillfeedHeadshotShow(bool on)   { mutablePolicy().headshotShow = on;          killfeedPolicyChanged(); }
void setKillfeedVehicleShow(bool on)    { mutablePolicy().vehicleShow = on;           killfeedPolicyChanged(); }
void setKillfeedDistanceShow(bool on)   { mutablePolicy().distanceShow = on;          killfeedPolicyChanged(); }
void setKillfeedAllowPersonal(bool on)  { mutablePolicy().allowPersonalKillfeed = on; killfeedPolicyChanged(); }
void setKillfeedMinRangeHeadshot(int m) { mutablePolicy().minRange[0] = clampRange(m); killfeedPolicyChanged(); }
void setKillfeedMinRangeVehicle(int m)  { mutablePolicy().minRange[1] = clampRange(m); killfeedPolicyChanged(); }
void setKillfeedMinRangeLongkill(int m) { mutablePolicy().minRange[2] = clampRange(m); killfeedPolicyChanged(); }
void setKillfeedMaxRangeHeadshot(int m) { mutablePolicy().maxRange[0] = clampRange(m); killfeedPolicyChanged(); }
void setKillfeedMaxRangeVehicle(int m)  { mutablePolicy().maxRange[1] = clampRange(m); killfeedPolicyChanged(); }
void setKillfeedMaxRangeLongkill(int m) { mutablePolicy().maxRange[2] = clampRange(m); killfeedPolicyChanged(); }

} // namespace killfeed_policy
} // namespace hfe
} // namespace dice
