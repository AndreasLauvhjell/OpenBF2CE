// [CHOSEN, not recovered] The `openbf2.*` console object. This is an OpenBF2 extension, not a
// recovered Battlefield 2 1.50 command; no retail image defines it. `openbf2.addModDll <name>` loads
// a native module at run time - a mod, or an integration shim such as bf2hub. <name> is a module base
// name, normally WITHOUT an extension (e.g. `bf2hub`); this binding appends the platform's library
// extension - .dll on Windows, .so on ELF - so one config line works on every build.
// Issue: B2BF/OpenBF2#39 (approach discussed there before implementation).
//
// Where it is driven from: the dedicated server runs an optional `openbf2.con` from the active mod
// directory before Python initialization (GameServer::init), so an addModDll line there takes effect
// before the Python host comes up. It is equally callable from ServerSettings.con, Init.con, rcon,
// Python, or the server console.
//
// Safety model (discussed in the issue):
//   - Dedicated-server image only. The whole binding compiles out of the client and editor, so a
//     downloaded map cannot reach it on a player's machine.
//   - It loads native code, so it is meant for operator-controlled config only: the active mod's
//     loose openbf2.con, ServerSettings.con, Init.con, rcon, Python, or the server console. Do not
//     expose it to untrusted content.
//
// Loader contract (lenient, so both plugin styles work):
//   - A module exporting initDll(IClassManager*) is initialised through the same handshake the
//     engine uses for its own plugins (Core/PluginDll.cpp); a false return unloads it and fails.
//   - A module without initDll is kept loaded, because its DllMain has already run. That is what a
//     DllMain-based integration such as bf2hub needs: its DllMain (server path, gated on the host
//     image name containing "w32ded") patches the engine's dice_py import slots Py_InitModule4 and
//     PyImport_ImportModule, so bf2hublib and bf2.bf2hub are injected when the engine registers its
//     `host` module and imports `bf2`. This is why openbf2.con runs before Python init - load it
//     afterward and the hooks are too late.
//   - This loader does not itself reroute sockets. bf2hub's GameSpy/master redirection relied on an
//     install-time patch of the host's import table that a runtime load does not reproduce; the
//     online services' domain is selected with +gamespy (Game/MatchMaking/OnlineProvider.h).
//
// Client image: the same object carries OpenBF2's client-only diagnostic overlays (see the client
// block at the end of this file). They show only local figures and change no game state. They need
// an issue discussing the design before an upstream PR.

#include "../../Core/ConsoleObjectMacro.h"
#include "../../Core/TempLog.h"   // [TEMP] openbf2.debug.log

#if DEDICATED

// All STL and engine headers are included before <windows.h> so its min/max macros cannot break them.
#include "../../IO/Console/Console.h"

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace dice {
namespace hfe {

class ClassManager;
extern ClassManager* classManager;   // Core/Globals.cpp - the host singleton handed to a plugin initDll.

namespace {

const char* const kOpenBf2ConsoleTag = "openbf2.console";

// Matches the initDll signature Core/PluginDll.cpp loads for engine plugins.
typedef bool (__cdecl* InitDllFn)(void*);

struct LoadedModule
{
    std::string requested;   // the name exactly as typed in the console
    std::string resolved;    // the platform path actually handed to the loader
    void*       handle;
};

// Function-local so it is constructed before any binding's static constructor runs and never
// becomes a load-order problem. It exists only to dedupe repeat loads and keep handles alive.
std::vector<LoadedModule>& loadedModules()
{
    static std::vector<LoadedModule> modules;
    return modules;
}

// Case-insensitive suffix test. Used only to avoid doubling an extension the operator already typed.
bool endsWithNoCase(const std::string& s, const char* suffix)
{
    const std::size_t n = std::strlen(suffix);
    if (s.size() < n)
        return false;
    for (std::size_t i = 0; i < n; ++i)
    {
        char a = s[s.size() - n + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

// Resolves the console argument - a module base name, normally WITHOUT extension (e.g. "bf2hub") -
// to the platform's library file(s) to try, in order. The platform extension is appended here, so a
// single openbf2.con line (openbf2.addModDll bf2hub) works on every build: .dll on Windows, .so on
// ELF. A name that already carries a path separator is taken as given (an explicit operator path),
// and an extension the operator already typed is not doubled. On ELF both lib<name>.so and <name>.so
// are tried - the engine's lib-prefixed plugin convention and a bare .so. Case is preserved
// (filenames are case-sensitive on ELF, so the name must match the file there).
std::vector<std::string> resolveModuleCandidates(const std::string& name)
{
    std::vector<std::string> out;
#if defined(_WIN32)
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
    {
        out.push_back(name);                            // explicit path, used verbatim
        return out;
    }
    std::string file = name;
    if (!endsWithNoCase(file, ".dll"))
        file += ".dll";
    out.push_back(std::string(".\\") + file);           // from the current directory, not the system DLL search path
#else
    if (name.find('/') != std::string::npos)
    {
        out.push_back(name);
        return out;
    }
    // [CHOSEN, not recovered] macOS uses .dylib for the same lib<name> convention; a typed .so or
    // .dll is still stripped so one config line keeps working on every build.
#if defined(__APPLE__)
    const char* const suffix = ".dylib";
#else
    const char* const suffix = ".so";
#endif
    std::string base = name;
    if (endsWithNoCase(base, ".so"))
        base.erase(base.size() - 3);
#if defined(__APPLE__)
    else if (endsWithNoCase(base, ".dylib"))
        base.erase(base.size() - 6);
#endif
    else if (endsWithNoCase(base, ".dll"))
        base.erase(base.size() - 4);
    const std::string stem = (base.compare(0, 3, "lib") == 0) ? base.substr(3) : base;
    out.push_back(std::string("lib") + stem + suffix);
    if (base != std::string("lib") + stem)              // skip the duplicate when the name already had lib
        out.push_back(base + suffix);
#endif
    return out;
}

std::string lastOsError()
{
#if defined(_WIN32)
    char buf[32];
    std::sprintf(buf, "error %lu", static_cast<unsigned long>(::GetLastError()));
    return std::string(buf);
#else
    const char* msg = ::dlerror();
    return msg ? std::string(msg) : std::string("unknown error");
#endif
}

void* openModule(const std::string& path)
{
#if defined(_WIN32)
    return ::LoadLibraryA(path.c_str());
#else
    return ::dlopen(path.c_str(), RTLD_LAZY);
#endif
}

void closeModule(void* handle)
{
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

void* moduleSymbol(void* handle, const char* symbol)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), symbol));
#else
    return ::dlsym(handle, symbol);
#endif
}

// Loads the named module. Returns a human-readable status string (empty is never returned, so the
// outcome always reaches an operator over rcon or the server console). Also echoed to the server
// console so the result is visible when a script discards the return value.
std::string openBf2AddModDll(const std::string& name)
{
    std::string result;

    if (name.empty())
    {
        result = "openbf2.addModDll: no module name given";
    }
    else
    {
        std::vector<LoadedModule>& mods = loadedModules();
        bool already = false;
        for (std::vector<LoadedModule>::const_iterator it = mods.begin(); it != mods.end(); ++it)
        {
            if (it->requested == name)
            {
                already = true;
                break;
            }
        }

        if (already)
        {
            result = "openbf2.addModDll: '" + name + "' already loaded";
        }
        else
        {
            const std::vector<std::string> candidates = resolveModuleCandidates(name);
            void* handle = 0;
            std::string resolved;
            std::string tried;
            std::string lastErr;
            for (std::vector<std::string>::const_iterator c = candidates.begin();
                 c != candidates.end(); ++c)
            {
                if (!tried.empty())
                    tried += ", ";
                tried += *c;
                handle = openModule(*c);
                if (handle)
                {
                    resolved = *c;
                    break;
                }
                lastErr = lastOsError();
            }

            if (!handle)
            {
                result = "openbf2.addModDll: could not load '" + name + "' (tried " + tried
                         + "; last error: " + lastErr + ")";
            }
            else
            {
                InitDllFn initDll = reinterpret_cast<InitDllFn>(moduleSymbol(handle, "initDll"));
                if (initDll && !initDll(reinterpret_cast<void*>(classManager)))
                {
                    closeModule(handle);
                    result = "openbf2.addModDll: initDll refused '" + resolved + "'";
                }
                else
                {
                    LoadedModule entry;
                    entry.requested = name;
                    entry.resolved  = resolved;
                    entry.handle    = handle;
                    mods.push_back(entry);
                    result = std::string("openbf2.addModDll: loaded '") + resolved + "'"
                             + (initDll ? " (initDll ok)" : " (no initDll; DllMain only)");
                }
            }
        }
    }

    if (io::mainConsole)
        io::mainConsole->output(result);
    return result;
}

} // anonymous namespace

BF2_CONSOLE_OBJECT("openbf2", 1, 0)
BF2_CONSOLE_METHOD_1_RET_ACTIVE(addModDll, openBf2AddModDll, std::string, true,
                                std::string, kOpenBf2ConsoleTag)
// [TEMP] openbf2.sv.debug.log 0/1: one switch for every [TEMP] test log of the dedicated server
// (Core/TempLog.h); the client has its own openbf2.debug.log. [RETIRE-WHEN: vehicle-tickrate-verified]
BF2_CONSOLE_PROPERTY_FN_NAMED("sv.debug.log", bool, true, ::dice::hfe::templog::enabled(),
                              ::dice::hfe::templog::setEnabled, kOpenBf2ConsoleTag)

// Handed to the console registry by ModuleBF2::init (Core/ModuleRegistry.cpp).
ConsoleObjectList& openBf2ConsoleObjects()
{
    return consoleBindingList(kOpenBf2ConsoleTag);
}

} // namespace hfe
} // namespace dice

#endif // DEDICATED

#if !DEDICATED

#include "../GameClient/GameClient.h"   // the net-stat overlay mode
#include "../../Menu/HudChat.h"         // the openbf2.console.* chat-history filter
#include "../../IO/Console/Console.h"   // console.clear / dump / log
#include <cstdio>
#include <ctime>
#if defined(_WIN32)
#  include <direct.h>                    // _mkdir - console.dump / log
#else
#  include <sys/stat.h>                  // mkdir - console.dump / log
#endif

namespace dice {
namespace hfe {

namespace {

const char* const kOpenBf2ConsoleTag = "openbf2.console";

} // anonymous namespace

// Case-insensitive compare, defined in IO/Console/Console.cpp (console.help).
int compareIgnoreCase(const char* a, const char* b);
int compareIgnoreCase(const char* a, const char* b, int n);

// Client bindings. Access 1, so the typed in-game console can use them.
BF2_CONSOLE_OBJECT("openbf2", 1, 0)

// The on-screen statistics share the member prefix "stats.". The console splits a command at its
// first dot, so these are members "stats.net" etc. of the openbf2 object, the same shape as retail's
// dotted members (renderer "soldier.debugCharacterMode", object "geometry.subGeometryCount").

// openbf2.stats.net - [CHOSEN, not recovered] the net-stat overlay; 1 shows it, 0 hides it.
// Always available; the overlay itself waits for a connection.
BF2_CONSOLE_PROPERTY_FN_NAMED("stats.net", bool, true, ::dice::hfe::getNetGraphOverlayMode(),
                              ::dice::hfe::setNetGraphOverlayMode, kOpenBf2ConsoleTag)

// openbf2.stats.perf - [CHOSEN, not recovered] a short frame-time summary (fps and frame time, CPU
// and GPU split, likely limit, draw calls); 1 shows it, 0 hides it. Measures only while it is on.
BF2_CONSOLE_PROPERTY_FN_NAMED("stats.perf", bool, true, ::dice::hfe::getPerfGraphOverlayMode(),
                              ::dice::hfe::setPerfGraphOverlayMode, kOpenBf2ConsoleTag)

// openbf2.stats.frame - [CHOSEN, not recovered] the full per-stage timing breakdown on screen and
// in the frame-timing log (BF2_FRAME_TIMING_LOG); 1 shows it and logs, 0 stops both.
BF2_CONSOLE_PROPERTY_FN_NAMED("stats.frame", bool, true, ::dice::hfe::getFrameTimingOverlayMode(),
                              ::dice::hfe::setFrameTimingOverlayMode, kOpenBf2ConsoleTag)

// openbf2.stats.audio - [CHOSEN, not recovered] the Sound.showStats text (provider, voices, buffers,
// volumes, listener) in the stats column; 1 shows it, 0 hides it.
BF2_CONSOLE_PROPERTY_FN_NAMED("stats.audio", bool, true, ::dice::hfe::getAudioStatsOverlayMode(),
                              ::dice::hfe::setAudioStatsOverlayMode, kOpenBf2ConsoleTag)

// openbf2.stats.showall - [CHOSEN, not recovered] 1 turns every stats block on, 0 turns all off.
BF2_CONSOLE_PROPERTY_FN_NAMED("stats.showall", bool, true, ::dice::hfe::getAllStatsOverlayMode(),
                              ::dice::hfe::setAllStatsOverlayMode, kOpenBf2ConsoleTag)

// openbf2.console.* - [CHOSEN, not recovered] which HUD chat lines are also copied into the console
// when they leave the HUD (Menu/HudChat.cpp). 1 copies them (retail), 0 keeps them out of the
// console; the HUD shows them either way. console.mute 1 silences every category at once and 0
// restores the per-category settings. The filter only hides lines (also ones already printed), so
// mute 0 brings them back; console.clear empties the whole history; console.dump saves the lines
// shown to a text file in ConsoleLogs; console.log [name] writes every new line to a file there
// until it is stopped.
namespace {
using ::dice::hfe::hud::ConsoleChatCategory;
int  consoleCategory(ConsoleChatCategory c)            { return ::dice::hfe::hud::getConsoleChatCategory(c) ? 1 : 0; }
void setConsoleCategory(ConsoleChatCategory c, int on) { ::dice::hfe::hud::setConsoleChatCategory(c, on != 0); }
int  consoleChat()           { return consoleCategory(::dice::hfe::hud::ConsoleChatChat); }
void setConsoleChat(int on)  { setConsoleCategory(::dice::hfe::hud::ConsoleChatChat, on); }
int  consoleKills()          { return consoleCategory(::dice::hfe::hud::ConsoleChatKills); }
void setConsoleKills(int on) { setConsoleCategory(::dice::hfe::hud::ConsoleChatKills, on); }
int  consoleEvents()          { return consoleCategory(::dice::hfe::hud::ConsoleChatEvents); }
void setConsoleEvents(int on) { setConsoleCategory(::dice::hfe::hud::ConsoleChatEvents, on); }
int  consoleRadio()          { return consoleCategory(::dice::hfe::hud::ConsoleChatRadio); }
void setConsoleRadio(int on) { setConsoleCategory(::dice::hfe::hud::ConsoleChatRadio, on); }
int  consolePersonal()          { return consoleCategory(::dice::hfe::hud::ConsoleChatPersonal); }
void setConsolePersonal(int on) { setConsoleCategory(::dice::hfe::hud::ConsoleChatPersonal, on); }
int  consoleMute()          { return ::dice::hfe::hud::getConsoleChatMute() ? 1 : 0; }
void setConsoleMute(int on) { ::dice::hfe::hud::setConsoleChatMute(on != 0); }
// console.clear: empties the console history, as GameLogic::clearConsole does on a level load
// (history limit to zero and back), without its thirty blank lines.
void consoleClear()
{
    ::dice::hfe::io::OldConsole* const console = ::dice::hfe::io::mainConsole;
    if (!console)
        return;
    const int maxHistory = console->getMaxHistorySize();
    console->setMaxHistorySize(0);
    console->setMaxHistorySize(maxHistory);
}
// The local date and time as YYYYMMDD-HHMMSS, for file names.
std::string timeStamp()
{
    const std::time_t now = std::time(0);
    char stamp[32] = "unknown-time";
    if (const std::tm* local = std::localtime(&now))
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
    return stamp;
}

// ConsoleLogs\<fileName> under the game folder (the working directory); creates the folder.
std::string consoleLogsPath(const std::string& fileName)
{
#if defined(_WIN32)
    _mkdir("ConsoleLogs");
    return "ConsoleLogs\\" + fileName;
#else
    mkdir("ConsoleLogs", 0755);
    return "ConsoleLogs/" + fileName;
#endif
}

// console.dump: writes the console lines the filter shows to ConsoleLogs\console-<date>-<time>.txt
// under the game folder (the working directory) and reports the path in the console.
void consoleDump()
{
    ::dice::hfe::io::OldConsole* const console = ::dice::hfe::io::mainConsole;
    if (!console)
        return;
    std::vector<std::string> lines;
    console->copyVisibleLines(lines);

    const std::string path = consoleLogsPath("console-" + timeStamp() + ".txt");
    std::FILE* file = std::fopen(path.c_str(), "w");
    if (!file)
    {
        console->output("openbf2.console.dump: could not write " + path + "\n");
        return;
    }
    for (std::size_t i = 0; i < lines.size(); ++i)
        std::fprintf(file, "%s\n", lines[i].c_str());
    std::fclose(file);
    char count[32];
    std::sprintf(count, "%u", static_cast<unsigned int>(lines.size()));
    console->output("openbf2.console.dump: " + std::string(count) + " lines written to " + path + "\n");
}

// console.log: writes every new console line to a file as it arrives, whatever the filters hide.
std::FILE*  g_consoleLogFile = 0;
std::string g_consoleLogPath;

void consoleLogfileStop()
{
    if (!g_consoleLogFile)
        return;
    ::dice::hfe::io::setOutputLogFile(0);
    std::fclose(g_consoleLogFile);
    g_consoleLogFile = 0;
    if (::dice::hfe::io::mainConsole)
        ::dice::hfe::io::mainConsole->output("openbf2.console.log: stopped, saved to "
                                             + g_consoleLogPath + "\n");
}

// Starts logging to ConsoleLogs\<fileName>, appending when the file exists.
void consoleLogfileStart(const std::string& fileName)
{
    consoleLogfileStop();
    const std::string path = consoleLogsPath(fileName);
    std::FILE* file = std::fopen(path.c_str(), "a");
    if (!file)
    {
        if (::dice::hfe::io::mainConsole)
            ::dice::hfe::io::mainConsole->output("openbf2.console.log: could not open " + path + "\n");
        return;
    }
    std::fprintf(file, "# BF2 console log, started %s\n", timeStamp().c_str());
    g_consoleLogFile = file;
    g_consoleLogPath = path;
    ::dice::hfe::io::setOutputLogFile(file);
    if (::dice::hfe::io::mainConsole)
        ::dice::hfe::io::mainConsole->output("openbf2.console.log: logging to " + path + "\n");
}

// openbf2.console.log with no name: starts a new console-log-<date>-<time>.txt, or stops the
// running log.
void consoleLogfileToggle()
{
    if (g_consoleLogFile)
        consoleLogfileStop();
    else
        consoleLogfileStart("console-log-" + timeStamp() + ".txt");
}

// openbf2.console.log <name>: "0" stops; any other name logs to ConsoleLogs\<name>.txt. Path
// separators and ':' become '_', so the file always lands in ConsoleLogs.
void consoleLogfileNamed(const std::string& name)
{
    if (name == "0")
    {
        consoleLogfileStop();
        return;
    }
    std::string fileName = name;
    for (std::size_t i = 0; i < fileName.size(); ++i)
        if (fileName[i] == '\\' || fileName[i] == '/' || fileName[i] == ':')
            fileName[i] = '_';
    if (fileName.empty())
        fileName = "console-log-" + timeStamp();
    const std::string::size_type dot = fileName.rfind('.');
    if (dot == std::string::npos || dot == 0)
        fileName += ".txt";
    consoleLogfileStart(fileName);
}
} // anonymous namespace

// A zero-argument command whose console spelling contains a dot. Same generated shape as
// BF2_CONSOLE_METHOD_0_STATIC_ACTIVE, with the quoted name of BF2_CC_HEAD_NAMED. Local to this file.
#define OPENBF2_CONSOLE_METHOD_0_NAMED(fnNameLiteral, target, listTag)                             \
namespace {                                                                                        \
class BF2_CC_NAME : public ::dice::hfe::ConsoleObjectBaseImpl                                      \
{                                                                                                  \
    BF2_CC_HEAD_NAMED(fnNameLiteral, 0, 0, 0, "void", false)                                       \
    BF2_CC_TAIL(0, listTag)                                                                        \
    virtual bool isObjectActive() const { return true; }                                           \
    virtual void executeObjectMethod()                                                             \
    {                                                                                              \
        if (m_argCount != 0) return;                                                               \
        m_hasReturnValue = false;                                                                  \
        target();                                                                                  \
    }                                                                                              \
    virtual void setArgFromString(int, const std::string&) { }                                     \
    BF2_CC_NO_RETURN_AS_STRING                                                                     \
    BF2_CC_COMMON_HOOKS                                                                            \
};                                                                                                 \
static BF2_CC_NAME BF2_CC_INSTANCE;                                                                \
}

BF2_CONSOLE_PROPERTY_FN_NAMED("console.chat", bool, true, consoleChat(), setConsoleChat, kOpenBf2ConsoleTag)
BF2_CONSOLE_PROPERTY_FN_NAMED("console.kills", bool, true, consoleKills(), setConsoleKills, kOpenBf2ConsoleTag)
BF2_CONSOLE_PROPERTY_FN_NAMED("console.events", bool, true, consoleEvents(), setConsoleEvents, kOpenBf2ConsoleTag)
BF2_CONSOLE_PROPERTY_FN_NAMED("console.radio", bool, true, consoleRadio(), setConsoleRadio, kOpenBf2ConsoleTag)
BF2_CONSOLE_PROPERTY_FN_NAMED("console.personal", bool, true, consolePersonal(), setConsolePersonal,
                              kOpenBf2ConsoleTag)
BF2_CONSOLE_PROPERTY_FN_NAMED("console.mute", bool, true, consoleMute(), setConsoleMute, kOpenBf2ConsoleTag)
OPENBF2_CONSOLE_METHOD_0_NAMED("console.clear", consoleClear, kOpenBf2ConsoleTag)
OPENBF2_CONSOLE_METHOD_0_NAMED("console.dump", consoleDump, kOpenBf2ConsoleTag)

// A command taking zero arguments or one string. Same shape as the macro above, with
// BF2_CONSOLE_METHOD_1_RET_ACTIVE's raw string argument. Local to this file.
#define OPENBF2_CONSOLE_METHOD_0_1_STRING_NAMED(fnNameLiteral, target0, target1, listTag)           \
namespace {                                                                                        \
class BF2_CC_NAME : public ::dice::hfe::ConsoleObjectBaseImpl                                      \
{                                                                                                  \
    BF2_CC_HEAD_NAMED(fnNameLiteral, 0, 0, 1, "void", false)                                       \
            m_argTypes[0] = "std::string";                                                         \
    BF2_CC_TAIL(1, listTag)                                                                        \
    virtual bool isObjectActive() const { return true; }                                           \
    virtual void executeObjectMethod()                                                             \
    {                                                                                              \
        m_hasReturnValue = false;                                                                  \
        if (m_argCount == 0)                                                                       \
            target0();                                                                             \
        else if (m_argCount == 1)                                                                  \
            target1(BF2_CC_ARG(std::string, 0));                                                   \
    }                                                                                              \
    virtual void setArgFromString(int index, const std::string& value)                             \
    {                                                                                              \
        BF2_CC_CONVERT(std::string, 1)                                                             \
    }                                                                                              \
    BF2_CC_NO_RETURN_AS_STRING                                                                     \
    BF2_CC_COMMON_HOOKS                                                                            \
};                                                                                                 \
static BF2_CC_NAME BF2_CC_INSTANCE;                                                                \
}

OPENBF2_CONSOLE_METHOD_0_1_STRING_NAMED("console.log", consoleLogfileToggle, consoleLogfileNamed,
                                        kOpenBf2ConsoleTag)

// console.list: prints every registered openbf2.* client command with a one-line description.
// The names come from the binding list itself, so a command without an entry below still shows.
namespace {
const char* commandDescription(const std::string& name)
{
    static const char* const table[][2] = {
        { "stats.net",        "1/0  ping, tick, interp, traffic and loss" },
        { "stats.perf",       "1/0  fps, frame time, CPU/GPU split, draw calls, graphics API" },
        { "stats.frame",      "1/0  time per main-loop and render stage, plus the timing log" },
        { "stats.audio",      "1/0  sound engine statistics (as Sound.showStats)" },
        { "stats.showall",    "1/0  every stats block on or off" },
        { "console.chat",     "1/0  chat lines in the console" },
        { "console.kills",    "1/0  kill lines in the console" },
        { "console.events",   "1/0  game and server messages in the console" },
        { "console.radio",    "1/0  radio commands in the console" },
        { "console.personal", "1/0  commander chat, notices, votes, requests in the console" },
        { "console.mute",     "1/0  hide or show all chat history in the console" },
        { "console.clear",    "     empty the console (also Ctrl+L)" },
        { "console.dump",     "     save the shown console lines to ConsoleLogs" },
        { "console.log",      "[name|0]  log every new line to ConsoleLogs; again or 0 stops" },
        { "console.list",     "     this list" },
        { "console.help",     "<command>  explain one command and show its value" },
        { "console.alias",    "[name [\"command\"]]  list, show or set a short alias" },
        { "console.unalias",  "<name>  remove an alias" } };
    for (std::size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (name == table[i][0])
            return table[i][1];
    return "";
}

void consoleList()
{
    ::dice::hfe::io::OldConsole* const console = ::dice::hfe::io::mainConsole;
    if (!console)
        return;
    const ConsoleObjectList& bindings = consoleBindingList(kOpenBf2ConsoleTag);
    std::string text;
    for (ConsoleObjectList::const_iterator it = bindings.begin(); it != bindings.end(); ++it)
    {
        const std::string name = (*it)->getFunctionName();
        std::string padded = "openbf2." + name;
        if (padded.size() < 26)
            padded.append(26 - padded.size(), ' ');
        text += padded + " " + commandDescription(name) + "\n";
    }
    console->output(text);
}
// console.help <command>: the longer text for one openbf2.* command, and its current value when it
// is a property. Accepts the name with or without "openbf2.".
const char* commandHelp(const std::string& name)
{
    static const char* const table[][2] = {
        { "stats.net", "Shows ping, tick rate, interpolation and extrapolation time, packets and\n"
                       "kB/s in and out, and packet loss of the connection to the server, in light\n"
                       "green at the left edge. Needs a server." },
        { "stats.perf", "Shows fps with average and worst frame time, how the frame splits between\n"
                        "game logic, rendering and the game.lockFps wait, Present and GPU wait with a\n"
                        "guess at the limit (CPU, GPU or CAP), draw calls, and the graphics API\n"
                        "(native Direct3D 9, DXVK or another d3d9.dll). Averaged over about 2 s." },
        { "stats.frame", "Shows the time of every main-loop stage and every render stage in ms per\n"
                         "frame, and writes one line per ~2 s to the frame-timing log." },
        { "stats.audio", "Shows the sound engine statistics: provider, quality, playing and waiting\n"
                         "samples, voices, buffer memory, volumes, Doppler and listener position." },
        { "stats.showall", "1 turns every stats block on, 0 turns them all off. Blocks that do not\n"
                           "fit in one column continue in a second column." },
        { "console.chat", "1 shows all, team and squad chat in the console, 0 hides it. Hidden\n"
                          "lines come back when it is set to 1 again. The HUD is not affected." },
        { "console.kills", "1 shows kill lines in the console, 0 hides them." },
        { "console.events", "1 shows game and server messages (overrun, flags, round info) in the\n"
                            "console, 0 hides them." },
        { "console.radio", "1 shows radio commands in the console, 0 hides them." },
        { "console.personal", "1 shows commander chat, notices such as \"you were killed by\", votes\n"
                              "and requests in the console, 0 hides them." },
        { "console.mute", "1 hides all chat history in the console, 0 shows it again with the\n"
                          "per-category settings. Aliases: stfu = mute 1, speak = mute 0." },
        { "console.clear", "Empties the console history. Ctrl+L does the same." },
        { "console.dump", "Saves the lines the console shows now to\n"
                          "ConsoleLogs\\console-<date>-<time>.txt in the game folder." },
        { "console.log", "No name: starts logging every new console line to\n"
                         "ConsoleLogs\\console-log-<date>-<time>.txt; run it again to stop.\n"
                         "With a name: logs to ConsoleLogs\\<name>.txt (appending). 0 stops." },
        { "console.list", "Lists every openbf2.* command with a one-line description." },
        { "console.help", "openbf2.console.help <command> explains one command." },
        { "console.alias", "No arguments: lists every alias. A name: shows it. A name and a command\n"
                           "(in quotes when it has spaces): sets it, e.g.\n"
                           "openbf2.console.alias perf \"openbf2.stats.perf\" then type: perf 1\n"
                           "Saved in OpenBF2Aliases.con in the Battlefield 2 documents folder." },
        { "console.unalias", "openbf2.console.unalias <name> removes an alias." } };
    for (std::size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (name == table[i][0])
            return table[i][1];
    return 0;
}

void consoleHelpNamed(const std::string& rawName)
{
    ::dice::hfe::io::OldConsole* const console = ::dice::hfe::io::mainConsole;
    if (!console)
        return;
    std::string name = rawName;
    if (name.size() > 8 && compareIgnoreCase(name.c_str(), "openbf2.", 8) == 0)
        name.erase(0, 8);
    const ConsoleObjectList& bindings = consoleBindingList(kOpenBf2ConsoleTag);
    for (ConsoleObjectList::const_iterator it = bindings.begin(); it != bindings.end(); ++it)
    {
        const std::string binding = (*it)->getFunctionName();
        if (compareIgnoreCase(binding.c_str(), name.c_str()) != 0)
            continue;
        std::string text = "openbf2." + binding + "  " + commandDescription(binding) + "\n";
        if (const char* help = commandHelp(binding))
            text += std::string(help) + "\n";
        if ((*it)->isSetGet())
        {
            std::string args[1];
            text += "Current value: " + (*it)->execute(args, 0) + "\n";
        }
        console->output(text);
        return;
    }
    console->output("openbf2.console.help: no openbf2 command named '" + rawName
                    + "'. openbf2.console.list shows them all.\n");
}

void consoleHelp()
{
    if (::dice::hfe::io::mainConsole)
        ::dice::hfe::io::mainConsole->output(
            "openbf2.console.help <command>, for example openbf2.console.help stats.perf.\n"
            "openbf2.console.list shows every openbf2 command.\n");
}
} // anonymous namespace
OPENBF2_CONSOLE_METHOD_0_NAMED("console.list", consoleList, kOpenBf2ConsoleTag)
OPENBF2_CONSOLE_METHOD_0_1_STRING_NAMED("console.help", consoleHelp, consoleHelpNamed, kOpenBf2ConsoleTag)

// console.alias / console.unalias - [CHOSEN, not recovered] the cut BF2 console alias feature brought
// back (IO/Console/Console.cpp): with no arguments alias lists every alias, with a name it shows that
// one, with a name and a command it sets it ("openbf2.console.alias perf \"openbf2.stats.perf 1\"").
namespace {
void consoleOut(const std::string& text)
{
    if (::dice::hfe::io::mainConsole)
        ::dice::hfe::io::mainConsole->output(text);
}

void consoleAliasList() { consoleOut(::dice::hfe::io::listConsoleAliases()); }

void consoleAliasShow(const std::string& name)
{
    const std::string* command = ::dice::hfe::io::findConsoleAlias(name);
    consoleOut(command ? name + " = " + *command + "\n" : "No alias named '" + name + "'.\n");
}

void consoleAliasSet(const std::string& name, const std::string& command)
{
    std::string error;
    if (::dice::hfe::io::setConsoleAlias(name, command, error))
        consoleOut("alias " + name + " = " + command + "\n");
    else
        consoleOut(error + "\n");
}

void consoleUnalias(const std::string& name)
{
    consoleOut(::dice::hfe::io::removeConsoleAlias(name) ? "Removed alias '" + name + "'.\n"
                                                          : "No alias named '" + name + "'.\n");
}
} // anonymous namespace

// Zero, one or two string arguments. Same shape as OPENBF2_CONSOLE_METHOD_0_1_STRING_NAMED.
#define OPENBF2_CONSOLE_METHOD_0_2_STRING_NAMED(fnNameLiteral, target0, target1, target2, listTag)  \
namespace {                                                                                        \
class BF2_CC_NAME : public ::dice::hfe::ConsoleObjectBaseImpl                                      \
{                                                                                                  \
    BF2_CC_HEAD_NAMED(fnNameLiteral, 0, 0, 2, "void", false)                                       \
            m_argTypes[0] = "std::string";                                                         \
            m_argTypes[1] = "std::string";                                                         \
    BF2_CC_TAIL(2, listTag)                                                                        \
    virtual bool isObjectActive() const { return true; }                                           \
    virtual void executeObjectMethod()                                                             \
    {                                                                                              \
        m_hasReturnValue = false;                                                                  \
        if (m_argCount == 0)                                                                       \
            target0();                                                                             \
        else if (m_argCount == 1)                                                                  \
            target1(BF2_CC_ARG(std::string, 0));                                                   \
        else if (m_argCount == 2)                                                                  \
            target2(BF2_CC_ARG(std::string, 0), BF2_CC_ARG(std::string, 1));                       \
    }                                                                                              \
    virtual void setArgFromString(int index, const std::string& value)                             \
    {                                                                                              \
        BF2_CC_CONVERT(std::string, 1)                                                             \
        BF2_CC_CONVERT(std::string, 2)                                                             \
    }                                                                                              \
    BF2_CC_NO_RETURN_AS_STRING                                                                     \
    BF2_CC_COMMON_HOOKS                                                                            \
};                                                                                                 \
static BF2_CC_NAME BF2_CC_INSTANCE;                                                                \
}

OPENBF2_CONSOLE_METHOD_0_2_STRING_NAMED("console.alias", consoleAliasList, consoleAliasShow, consoleAliasSet,
                                        kOpenBf2ConsoleTag)
OPENBF2_CONSOLE_METHOD_0_1_STRING_NAMED("console.unalias", consoleAliasList, consoleUnalias, kOpenBf2ConsoleTag)

// [TEMP] openbf2.debug.log 0/1: one switch for every [TEMP] test log (Core/TempLog.h).
// [RETIRE-WHEN: vehicle-tickrate-verified]
BF2_CONSOLE_PROPERTY_FN_NAMED("debug.log", bool, true, ::dice::hfe::templog::enabled(),
                              ::dice::hfe::templog::setEnabled, kOpenBf2ConsoleTag)

// Handed to the console registry by ModuleBF2::init (Core/ModuleRegistry.cpp).
ConsoleObjectList& openBf2ConsoleObjects()
{
    return consoleBindingList(kOpenBf2ConsoleTag);
}

} // namespace hfe
} // namespace dice

#endif // !DEDICATED
