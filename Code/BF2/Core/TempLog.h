// [TEMP] [CHOSEN, not recovered] One switch for every [TEMP] test log in this tree: console
// openbf2.debug.log 0/1 (client and dedicated server, default 1). A log still needs its own
// environment variable (BF2_LOG_VEHICLE / BF2_LOG_INTERP / BF2_NET_TRACE, set by the test bats);
// this switch can silence all of them at run time, e.g. to see whether logging costs frame rate.
// [RETIRE-WHEN: vehicle-tickrate-verified] test tree only, never committed.

#ifndef CORE_TEMPLOG_H
#define CORE_TEMPLOG_H

namespace dice {
namespace hfe {
namespace templog {

inline bool& enabledFlag()
{
    static bool enabled = true;
    return enabled;
}
inline bool enabled()             { return enabledFlag(); }
inline void setEnabled(bool on)   { enabledFlag() = on; }

} // namespace templog
} // namespace hfe
} // namespace dice

#endif // CORE_TEMPLOG_H
