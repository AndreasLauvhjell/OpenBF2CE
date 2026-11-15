// [CHOSEN, not recovered] OpenBF2 client kill feed settings. See KillfeedSettings.h.

#include "KillfeedSettings.h"

namespace dice {
namespace hfe {
namespace killfeed_settings {

namespace {

struct State
{
    bool  killfeedDistance;
    bool  killfeedHeadshot;
    int   killfeedIcons;
    int   killfeedHeadshotSoundVolume;
    bool  killfeedVehicle;
    int   killfeedMinRange[KILL_KIND_COUNT];   // metres, by KillKind
    int   killfeedMaxRange[KILL_KIND_COUNT];   // metres, 0 = no limit

    State() : killfeedDistance(true), killfeedHeadshot(true), killfeedIcons(2),
              killfeedHeadshotSoundVolume(100), killfeedVehicle(true)
    {
        killfeedMinRange[KILL_HEADSHOT] = 0;
        killfeedMinRange[KILL_VEHICLE]  = 0;
        killfeedMinRange[KILL_LONG]     = 150;
        for (int kind = 0; kind < KILL_KIND_COUNT; ++kind)
            killfeedMaxRange[kind] = 0;
    }
};

int clampRange(int metres) { return metres < 0 ? 0 : metres; }

State& state()
{
    static State value;
    return value;
}

} // namespace

bool killfeedDistance()                     { return state().killfeedDistance; }
void setKillfeedDistance(bool on)            { state().killfeedDistance = on; }
bool killfeedHeadshot()                     { return state().killfeedHeadshot; }
void setKillfeedHeadshot(bool on)            { state().killfeedHeadshot = on; }
int  killfeedIcons()                        { return state().killfeedIcons; }
void setKillfeedIcons(int mode)              { state().killfeedIcons = mode < 0 ? 0 : (mode > 2 ? 2 : mode); }
int  killfeedHeadshotSoundVolume()          { return state().killfeedHeadshotSoundVolume; }
void setKillfeedHeadshotSoundVolume(int v)   { state().killfeedHeadshotSoundVolume = v < 0 ? 0 : (v > 100 ? 100 : v); }
bool killfeedVehicle()                      { return state().killfeedVehicle; }
void setKillfeedVehicle(bool on)             { state().killfeedVehicle = on; }
int  killfeedMinRange(KillKind kind)        { return state().killfeedMinRange[kind]; }
int  killfeedMaxRange(KillKind kind)        { return state().killfeedMaxRange[kind]; }
int  killfeedMinRangeHeadshot()             { return state().killfeedMinRange[KILL_HEADSHOT]; }
void setKillfeedMinRangeHeadshot(int m)      { state().killfeedMinRange[KILL_HEADSHOT] = clampRange(m); }
int  killfeedMinRangeVehicle()              { return state().killfeedMinRange[KILL_VEHICLE]; }
void setKillfeedMinRangeVehicle(int m)       { state().killfeedMinRange[KILL_VEHICLE] = clampRange(m); }
int  killfeedMinRangeLongkill()             { return state().killfeedMinRange[KILL_LONG]; }
void setKillfeedMinRangeLongkill(int m)      { state().killfeedMinRange[KILL_LONG] = clampRange(m); }
int  killfeedMaxRangeHeadshot()             { return state().killfeedMaxRange[KILL_HEADSHOT]; }
void setKillfeedMaxRangeHeadshot(int m)      { state().killfeedMaxRange[KILL_HEADSHOT] = clampRange(m); }
int  killfeedMaxRangeVehicle()              { return state().killfeedMaxRange[KILL_VEHICLE]; }
void setKillfeedMaxRangeVehicle(int m)       { state().killfeedMaxRange[KILL_VEHICLE] = clampRange(m); }
int  killfeedMaxRangeLongkill()             { return state().killfeedMaxRange[KILL_LONG]; }
void setKillfeedMaxRangeLongkill(int m)      { state().killfeedMaxRange[KILL_LONG] = clampRange(m); }

} // namespace killfeed_settings
} // namespace hfe
} // namespace dice
