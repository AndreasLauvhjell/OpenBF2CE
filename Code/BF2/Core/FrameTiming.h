// Optional per-frame timing breakdown for the client.
//
// [CHOSEN, not recovered] OpenBF2 diagnostic; retail ships its Profiler as no-ops and has nothing
// like this. Needs an issue discussing the design before an upstream PR.
//
// Measurement runs only while a user has asked for it: the openbf2.stats.perf overlay
// (UserPerfGraph) or openbf2.stats.frame (UserFrameTiming, overlay plus log file). The environment
// variable BF2_FRAME_TIMING=1 turns openbf2.stats.frame on at startup. BF2_FRAME_TIMING_LOG names
// the log file (default: frame-timing.log in the working directory); it is opened the first time
// openbf2.stats.frame is switched on. While nobody uses it every hook is one pointer test and one
// flag test. The dedicated image and non-Windows builds compile every hook to nothing.
//
// BF2.exe and RendDX9.dll are separate images, so each has its own copy of the inline statics
// below. The first image that calls table() allocates the shared table and publishes its address
// in the process environment (BF2_FRAME_TIMING_TABLE); the other image picks it up from there.
//
// The engine main loop calls lap() between its stages, so the lap sections add up to the real
// frame interval. RendDX9 adds nested measurements (Present, GPU fence wait, draw calls) that are
// part of the Render lap.

#ifndef CORE_FRAMETIMING_H
#define CORE_FRAMETIMING_H

#if !DEDICATED && defined(_WIN32)
#  define BF2_FRAME_TIMING_ENABLED 1
#else
#  define BF2_FRAME_TIMING_ENABLED 0
#endif

#if BF2_FRAME_TIMING_ENABLED
#  include <windows.h>     // include sites already have it; QueryPerformanceCounter, environment
#  include <cstdio>
#  include <cstdlib>
#  include <cstring>
#endif

namespace dice {
namespace hfe {
namespace frametiming {

// Main-loop stages in the order BF2Engine::mainLoop runs them.
enum Section
{
    SecOutside = 0,     // between two mainLoop calls: the Windows message pump
    SecSetup,           // idle sleep, shutdown checks, demo begin
    SecLockFps,         // the game.lockFps busy-wait
    SecInput,           // clocks, tick calculation, input, movie queue
    SecMenu,            // main menu / movie drawing
    SecSoundHud,        // sound update and HUD information layer
    SecSimulation,      // game->update (fixed 30 Hz ticks)
    SecPython,          // python host update
    SecWorld,           // frame events, object grid, ragdolls, object/player frame updates
    SecRender,          // BF2Render beforeFrame + render (scene, HUD, Present, GPU fence)
    SecConsole,         // console, queued commands, file monitor, debug counters, status
    SecFrameEnd,        // dedicated limiter (unused on the client), arena, demo end
    SecLapCount,

    // Nested inside SecRender: Renderer::renderFrame in RendDX9, split with renderLap() into
    // consecutive stages in this order. They add up to the renderFrame time; the rest of the
    // SecRender lap is engine-side BF2Render work (reported as "eng").
    SecRUpdate = SecLapCount,   // frame setup and per-frame updates (wind, sky, weather, lights,
                                //   particles, decals, HUD prep), and the tail after the fence
    SecRCull,           // ObjectDrawer view/time passes in the fog block (object visibility)
    SecRShadows,        // light frame prep and LightManager::renderShadowMaps
    SecRObjects,        // frame clear, shader constants, ObjectDrawer draw-list pass
    SecROpaque,         // z-only, terrain, overgrowth, world queues, undergrowth, roads, sky
    SecRTransparent,    // fog pass, transparency, decals, water, particles, weather
    SecRFirstPerson,    // first-person queues (weapon, hands)
    SecRNametags,       // NametagManager draw
    SecRHudPaint,       // BF2HudManager paint (the in-game HUD; Flash is only the menu)
    SecRFrameEvents,    // debug-graph samples and the per-frame ECPhysics/ECCore/6000 event posts,
                        //   whose handlers run inline (menu and debug-text drawing hang off them)
    SecRHud,            // the rest up to endScene: frame-rate stats, debug overlays, console,
                        //   particle end-frame, endScene
    SecPresent,         // Present
    SecGpuFence,        // the GPU-throttle fence wait

    // Nested inside SecSimulation: GameClient::update split with simLap() (and simAdd() for the
    // prediction replays, which run inside receive). They add up to the client's game->update time.
    SecSimFirst,
    SecSPlayer = SecSimFirst,   // local Player::update, prediction setting, before receive
    SecSReceive,        // receive: packet decode and ghost updates (replays excluded)
    SecSReplay,         // prediction replays: handleControlObjectState re-simulating the local vehicle
    SecSEvents,         // stepWorld: events, triggers, gameLogic, remote players' weapon/sprint state
    SecSObjects,        // sampleLocalInput + objectManager->updateObjects (every object's handleUpdate)
    SecSPhysics,        // local vehicle mobile physics + physicsNodeManager->update
    SecSRemote,         // predictRemoteObjects + yaw stabilisation
    SecSCollision,      // updateWorldCollision
    SecSLocalPred,      // stepLocalPrediction + physics/response post-frame
    SecSSound,          // the sound update on the last step
    SecSSend,           // channel state, jitter summary, votes, transmit, net graph
    SecCount
};

// Who asked for measurement. Measurement runs while at least one bit is set.
enum User
{
    UserPerfGraph   = 1,    // openbf2.stats.perf
    UserFrameTiming = 2     // openbf2.stats.frame (also writes the log)
};

// Averages of the last finished report interval (about two seconds), for on-screen display.
struct Snapshot
{
    bool     valid;
    double   fps;
    double   frameMs;       // average frame time
    double   maxFrameMs;    // worst single frame
    double   gameMs;        // simulation + python + world
    double   renderMs;      // the whole render lap, Present and fence included
    double   capWaitMs;     // game.lockFps busy-wait
    double   presentMs;
    double   fenceMs;       // GPU-throttle fence wait
    double   engRenderMs;   // the part of the render lap outside Renderer::renderFrame
    double   sectionMs[SecCount];   // every section, ms per frame
    unsigned drawCalls;
    unsigned long long primitives;
};

#if BF2_FRAME_TIMING_ENABLED

struct Table
{
    long long   frequency;
    long long   lastLap;
    long long   lastReport;
    long long   frameStart;
    long long   renderLast;
    long long   simLast;        // GameClient::update stopwatch
    long long   simCarry;       // time simAdd charged inside the running simLap
    long long   accum[SecCount];
    long long   maxFrame;
    unsigned    frames;
    unsigned    drawCalls;
    unsigned long long primitives;
    unsigned    users;      // User bits; zero means every hook returns at once
    bool        logTried;   // the log file was opened (or the attempt failed) once
    std::FILE*  log;
    Snapshot    snapshot;
};

inline long long now()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

// Clears the running interval and restarts every stopwatch from now.
inline void restartInterval(Table* t)
{
    for (int i = 0; i < SecCount; ++i)
        t->accum[i] = 0;
    t->maxFrame   = 0;
    t->frames     = 0;
    t->drawCalls  = 0;
    t->primitives = 0;
    const long long n = now();
    t->lastLap    = n;
    t->lastReport = n;
    t->frameStart = n;
    t->renderLast = n;
    t->simLast    = n;
    t->simCarry   = 0;
}

// Opens the log once and writes its column header.
inline void openLog(Table* t)
{
    if (t->logTried)
        return;
    t->logTried = true;

    char path[512];
    const DWORD n = GetEnvironmentVariableA("BF2_FRAME_TIMING_LOG", path, sizeof(path));
    if (n == 0 || n >= sizeof(path))
        std::strcpy(path, "frame-timing.log");
    t->log = std::fopen(path, "w");
    // Confirmation on stderr (the start bats capture it), so a run without a timing log shows
    // where it tried to write.
    std::fprintf(stderr, "[frame-timing] log %s: %s\n", t->log ? "open" : "FAILED to open", path);
    std::fflush(stderr);
    if (t->log)
    {
        std::fprintf(t->log,
            "# BF2 frame timing. Each line averages one report interval; times are ms per frame.\n"
            "# The lap columns (outside..end) add up to the frame time. The second block splits\n"
            "# render: eng (engine-side BF2Render) + update..fence add up to render. The third block\n"
            "# splits the client sim (game->update): splayr..ssend add up to sim.\n"
            "# draws/prims are per frame.\n"
            "#  secs frames    fps  frame    max | outside  setup lockfps  input   menu sndhud"
            "    sim python  world render console    end |    eng update   cull shadow objdrw"
            " opaque transp 1stpers  ntags hudpnt frmevt hudrst present  fence |"
            " splayr  srecv sreplay sevent  sobj  sphys sremote scoll slpred ssound  ssend |  draws    prims\n");
        std::fflush(t->log);
    }
}

// The shared table. Null only if the allocation failed.
inline Table* table()
{
    static bool   s_initialized = false;
    static Table* s_table       = 0;
    if (s_initialized)
        return s_table;
    s_initialized = true;

    char value[64];
    DWORD n = GetEnvironmentVariableA("BF2_FRAME_TIMING_TABLE", value, sizeof(value));
    if (n > 0 && n < sizeof(value))
    {
        void* published = 0;
        if (std::sscanf(value, "%p", &published) == 1 && published)
        {
            s_table = static_cast<Table*>(published);
            return s_table;
        }
    }

    // First image to get here owns the table. It lives until process exit.
    Table* t = static_cast<Table*>(std::calloc(1, sizeof(Table)));
    if (!t)
        return 0;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    t->frequency = f.QuadPart;
    restartInterval(t);

    n = GetEnvironmentVariableA("BF2_FRAME_TIMING", value, sizeof(value));
    if (n > 0 && n < sizeof(value) && value[0] != '0')
    {
        t->users = UserFrameTiming;
        openLog(t);
    }

    std::sprintf(value, "%p", static_cast<void*>(t));
    SetEnvironmentVariableA("BF2_FRAME_TIMING_TABLE", value);
    s_table = t;
    return s_table;
}

// The table while measurement is running, otherwise null.
inline Table* activeTable()
{
    Table* t = table();
    return (t && t->users) ? t : 0;
}

// Turns one user's request on or off. Starting from idle restarts the interval, so the first
// report does not include the idle time; switching openbf2.stats.frame on opens the log.
inline void setUser(User user, bool on)
{
    Table* t = table();
    if (!t)
        return;
    const unsigned before = t->users;
    t->users = on ? (before | user) : (before & ~static_cast<unsigned>(user));
    if (!before && t->users)
    {
        restartInterval(t);
        t->snapshot.valid = false;
    }
    if (on && user == UserFrameTiming)
        openLog(t);
}

inline bool hasUser(User user)
{
    Table* t = table();
    return t && (t->users & user) != 0;
}

// Charges the time since the previous lap to the given main-loop section.
inline void lap(Section section)
{
    Table* t = activeTable();
    if (!t)
        return;
    const long long n = now();
    t->accum[section] += n - t->lastLap;
    t->lastLap = n;
}

// Starts the renderFrame stopwatch (RendDX9).
inline void renderBegin()
{
    Table* t = activeTable();
    if (t)
        t->renderLast = now();
}

// Charges the time since the previous render lap to the given nested render section.
inline void renderLap(Section section)
{
    Table* t = activeTable();
    if (!t)
        return;
    const long long n = now();
    t->accum[section] += n - t->renderLast;
    t->renderLast = n;
}

// Starts the GameClient::update stopwatch.
inline void simBegin()
{
    Table* t = activeTable();
    if (t)
    {
        t->simLast  = now();
        t->simCarry = 0;
    }
}

// Charges the time since the previous sim lap, minus what simAdd charged meanwhile, to section.
inline void simLap(Section section)
{
    Table* t = activeTable();
    if (!t)
        return;
    const long long n = now();
    t->accum[section] += n - t->simLast - t->simCarry;
    t->simLast  = n;
    t->simCarry = 0;
}

// Charges an inner measurement (start = now() before it) to section and takes it out of the
// running sim lap.
inline void simAdd(Section section, long long start)
{
    Table* t = activeTable();
    if (!t || !start)
        return;
    const long long d = now() - start;
    t->accum[section] += d;
    t->simCarry       += d;
}

// now() while measurement runs, else 0 (pair with simAdd).
inline long long simStart()
{
    return activeTable() ? now() : 0;
}

// Closes one engine frame; publishes a snapshot (and a log line while openbf2.stats.frame is on)
// about every two seconds.
inline void endFrame()
{
    Table* t = activeTable();
    if (!t)
        return;
    const long long n = now();
    const long long frame = n - t->frameStart;
    t->frameStart = n;
    if (frame > t->maxFrame)
        t->maxFrame = frame;
    ++t->frames;

    const long long span = n - t->lastReport;
    if (span < 2 * t->frequency || t->frames == 0)
        return;

    const double toMs = 1000.0 / static_cast<double>(t->frequency) / t->frames;
    const double secs = static_cast<double>(span) / static_cast<double>(t->frequency);
    double nested = 0.0;
    for (int i = SecLapCount; i < SecSimFirst; ++i)          // the render-nested sections only
        nested += static_cast<double>(t->accum[i]);
    {
        Snapshot& snap   = t->snapshot;
        snap.fps         = t->frames / secs;
        snap.frameMs     = secs * 1000.0 / t->frames;
        snap.maxFrameMs  = static_cast<double>(t->maxFrame) * 1000.0 / static_cast<double>(t->frequency);
        snap.gameMs      = static_cast<double>(t->accum[SecSimulation] + t->accum[SecPython]
                                               + t->accum[SecWorld]) * toMs;
        snap.renderMs    = static_cast<double>(t->accum[SecRender]) * toMs;
        snap.capWaitMs   = static_cast<double>(t->accum[SecLockFps]) * toMs;
        snap.presentMs   = static_cast<double>(t->accum[SecPresent]) * toMs;
        snap.fenceMs     = static_cast<double>(t->accum[SecGpuFence]) * toMs;
        snap.engRenderMs = (static_cast<double>(t->accum[SecRender]) - nested) * toMs;
        for (int i = 0; i < SecCount; ++i)
            snap.sectionMs[i] = static_cast<double>(t->accum[i]) * toMs;
        snap.drawCalls   = t->drawCalls / t->frames;
        snap.primitives  = t->primitives / t->frames;
        snap.valid       = true;
    }

    if (t->log && (t->users & UserFrameTiming))
    {
        double lapSum = 0.0;
        for (int i = 0; i < SecLapCount; ++i)
            lapSum += static_cast<double>(t->accum[i]);
        std::fprintf(t->log, "%7.2f %6u %6.1f %6.3f %6.3f |",
                     secs, t->frames, t->frames / secs, lapSum * toMs,
                     static_cast<double>(t->maxFrame) * 1000.0 / static_cast<double>(t->frequency));
        for (int i = 0; i < SecLapCount; ++i)
            std::fprintf(t->log, " %6.3f", static_cast<double>(t->accum[i]) * toMs);
        std::fprintf(t->log, " | %6.3f", (static_cast<double>(t->accum[SecRender]) - nested) * toMs);
        for (int i = SecLapCount; i < SecSimFirst; ++i)
            std::fprintf(t->log, " %6.3f", static_cast<double>(t->accum[i]) * toMs);
        std::fprintf(t->log, " |");
        for (int i = SecSimFirst; i < SecCount; ++i)
            std::fprintf(t->log, " %6.3f", static_cast<double>(t->accum[i]) * toMs);
        std::fprintf(t->log, " | %6u %8llu\n",
                     t->drawCalls / t->frames,
                     t->primitives / t->frames);
        std::fflush(t->log);
    }

    for (int i = 0; i < SecCount; ++i)
        t->accum[i] = 0;
    t->maxFrame   = 0;
    t->frames     = 0;
    t->drawCalls  = 0;
    t->primitives = 0;
    t->lastReport = n;
}

// The last interval's averages, or null when measurement is off or no interval has finished.
inline const Snapshot* snapshot()
{
    Table* t = activeTable();
    return (t && t->snapshot.valid) ? &t->snapshot : 0;
}

// Measures a nested section (RendDX9) for the lifetime of the object.
class Scope
{
public:
    explicit Scope(Section section) : m_table(activeTable()), m_section(section), m_start(0)
    {
        if (m_table)
            m_start = now();
    }
    ~Scope()
    {
        if (m_table)
            m_table->accum[m_section] += now() - m_start;
    }
private:
    Scope(const Scope&);
    Scope& operator=(const Scope&);
    Table*    m_table;
    Section   m_section;
    long long m_start;
};

// Counts one draw call of the given primitive count.
inline void countDraw(unsigned int primitives)
{
    Table* t = activeTable();
    if (!t)
        return;
    ++t->drawCalls;
    t->primitives += primitives;
}

#else   // BF2_FRAME_TIMING_ENABLED

inline void setUser(User, bool)       {}
inline bool hasUser(User)             { return false; }
inline void lap(Section)              {}
inline void renderBegin()             {}
inline void renderLap(Section)        {}
inline void endFrame()                {}
inline void countDraw(unsigned int)   {}
inline const Snapshot* snapshot()     { return 0; }
class Scope
{
public:
    explicit Scope(Section) {}
};

#endif  // BF2_FRAME_TIMING_ENABLED

} // namespace frametiming
} // namespace hfe
} // namespace dice

#endif // CORE_FRAMETIMING_H
