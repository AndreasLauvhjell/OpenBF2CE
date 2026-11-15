// [CHOSEN, not recovered] OpenBF2 client kill feed settings (mod/combo): the kill feed part of
// mod/killfeed's ClientNetSettings, without the net-tuning settings. Console (client, openbf2 object;
// bound in Game/Main/OpenBF2Console.cpp):
//   openbf2.hud.killfeed.distance.show <0|1> kill feed and "you killed" lines show the shot distance the
//                                      server sent (MSG_KILL_DISTANCE, OpenBF2 servers only): 1 (default)
//                                      shows it, 0 = retail line.
//   openbf2.hud.killfeed.headshot.show <0|1> the same lines show HEADSHOT when the killing hit's surface
//                                      material (KilledByData +4, sent by every server) is Human_head:
//                                      1 (default) shows it, 0 = retail line.
//   openbf2.hud.killfeedIcons <0|1|2>  inline icons in the kill feed (Menu/InlineIcon.h): 0 = text only,
//                                      1 = hand weapon icon and headshot icon, 2 (default) = headshot
//                                      icon only. A missing texture falls back to text.
//   openbf2.hud.killfeed.headshot.soundvolume <0-100>  volume of the 2D cue (hand grenade explosion) for your
//                                      own headshot on an enemy; 100 (default), 0 = off.
//   openbf2.hud.killfeed.destroyedvehicle.show <0|1>  kill lines show "HELI DESTROYED" (and so on) when the server says
//                                      the kill destroyed the victim's vehicle (MSG_KILL_VEHICLE, OpenBF2
//                                      servers only): 1 (default) shows it, 0 = retail line.
//   openbf2.hud.killfeed.distance.minrange.<headshot|vehicle|longkill> <m>  the distance shows only when
//   openbf2.hud.killfeed.distance.maxrange.<headshot|vehicle|longkill> <m>  the kill lies within the
//                                      range of its kind: a headshot (min 0), else a destroyed vehicle
//                                      (min 0), else any other kill (min 150). maxrange 0 (default) = no
//                                      upper limit. Whole metres.
//   Placement and wording: the lexicon lines HUD_HUD_KILLEDBY_*, HUD_CHAT_YOUGOTAKILLSCORE and
//   HUD_CHAT_YOUWEREKILLEDBY may hold the tokens #HEADSHOT#, #VEHICLE# and #DISTANCE#; nothing shows
//   where a line has none (Menu/BF2HudManagerGameLogicEvents.cpp).
#ifndef GAME_MAIN_KILLFEEDSETTINGS_H
#define GAME_MAIN_KILLFEEDSETTINGS_H

namespace dice {
namespace hfe {
namespace killfeed_settings {

// openbf2.hud.killfeed.distance.show and openbf2.hud.killfeed.headshot.show (Menu/BF2HudManagerGameLogicEvents.cpp).
bool  killfeedDistance();
void  setKillfeedDistance(bool on);
bool  killfeedHeadshot();
void  setKillfeedHeadshot(bool on);
// openbf2.hud.killfeedIcons and openbf2.hud.killfeed.headshot.soundvolume (the same file).
int   killfeedIcons();
void  setKillfeedIcons(int mode);
int   killfeedHeadshotSoundVolume();
void  setKillfeedHeadshotSoundVolume(int volume);
// openbf2.hud.killfeed.destroyedvehicle.show (the same file).
bool  killfeedVehicle();
void  setKillfeedVehicle(bool on);
// The kind of kill whose distance limits apply: a headshot first, then a destroyed vehicle, otherwise a
// plain (long) kill. Each kill falls in exactly one kind, so its distance is judged once.
enum KillKind
{
    KILL_HEADSHOT = 0,
    KILL_VEHICLE  = 1,
    KILL_LONG     = 2,
    KILL_KIND_COUNT
};
// openbf2.hud.killfeed.distance.minrange.<headshot|vehicle|longkill> and maxrange.<...> (whole metres;
// maxrange 0 = no upper limit). Negative values are clamped to 0.
int   killfeedMinRange(KillKind kind);
int   killfeedMaxRange(KillKind kind);
int   killfeedMinRangeHeadshot();
void  setKillfeedMinRangeHeadshot(int metres);
int   killfeedMinRangeVehicle();
void  setKillfeedMinRangeVehicle(int metres);
int   killfeedMinRangeLongkill();
void  setKillfeedMinRangeLongkill(int metres);
int   killfeedMaxRangeHeadshot();
void  setKillfeedMaxRangeHeadshot(int metres);
int   killfeedMaxRangeVehicle();
void  setKillfeedMaxRangeVehicle(int metres);
int   killfeedMaxRangeLongkill();
void  setKillfeedMaxRangeLongkill(int metres);

} // namespace killfeed_settings
} // namespace hfe
} // namespace dice

#endif // GAME_MAIN_KILLFEEDSETTINGS_H
