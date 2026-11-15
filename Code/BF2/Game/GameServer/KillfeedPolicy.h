// [CHOSEN, not recovered] The server's kill feed policy (openbf2.sv.killfeed.*), mod/combo. Taken from
// mod/killfeed c8ab882, where it lived in NetTuning next to the net-tuning settings, which mod/combo does
// not have. It overrides the clients' openbf2.hud.killfeed.* settings as limits: a show switch at 0 turns
// that piece off for every OpenBF2 client; a minrange is a floor and a maxrange (0 = none) a ceiling the
// clients can only tighten. allowPersonalKillfeed still lets the killer's "You killed" and the victim's
// "You were killed by" lines show a piece the public kill feed hides; a hidden distance shows only on the
// killer's line, never the victim's (no revenge info on a sniper). The policy reaches the clients as
// MSG_KILLFEED_POLICY (Game/Common/OpenBF2Channel.h) after the welcome and on every change. Ranges are
// whole metres, indexed headshot / vehicle / longkill like killfeed_settings::KillKind. Console: bound in
// Game/Main/OpenBF2Console.cpp (dedicated image).
#ifndef GAME_GAMESERVER_KILLFEEDPOLICY_H
#define GAME_GAMESERVER_KILLFEEDPOLICY_H

namespace dice {
namespace hfe {
namespace killfeed_policy {

struct KillfeedPolicy
{
    bool headshotShow;          // sv.killfeed.headshot.show (default 1)
    bool vehicleShow;           // sv.killfeed.destroyedvehicle.show (default 1)
    bool distanceShow;          // sv.killfeed.distance.show (default 1)
    bool allowPersonalKillfeed; // sv.killfeed.allowpersonalkillfeed (default 1)
    int  minRange[3];           // sv.killfeed.distance.minrange.<headshot|vehicle|longkill> (default 0)
    int  maxRange[3];           // sv.killfeed.distance.maxrange.<...> (default 0 = no ceiling)
};

const KillfeedPolicy& killfeedPolicy();
// True when the server sends kill distances / destroyed vehicles at all: to everyone while the piece is
// shown, or only to the killer and the victim while it is hidden but personal lines are allowed.
bool killfeedSendsDistance();
bool killfeedSendsVehicle();
// openbf2.sv.killfeed.* (see KillfeedPolicy). Each re-announces the capabilities and the policy to the
// OpenBF2 clients.
void setKillfeedHeadshotShow(bool on);
void setKillfeedVehicleShow(bool on);
void setKillfeedDistanceShow(bool on);
void setKillfeedAllowPersonal(bool on);
void setKillfeedMinRangeHeadshot(int metres);
void setKillfeedMinRangeVehicle(int metres);
void setKillfeedMinRangeLongkill(int metres);
void setKillfeedMaxRangeHeadshot(int metres);
void setKillfeedMaxRangeVehicle(int metres);
void setKillfeedMaxRangeLongkill(int metres);

} // namespace killfeed_policy
} // namespace hfe
} // namespace dice

#endif // GAME_GAMESERVER_KILLFEEDPOLICY_H
