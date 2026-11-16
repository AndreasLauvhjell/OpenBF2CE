// Quad drawing interface with scalar arguments. No virtual destructor or STL values cross this boundary.
// Method names are [INFERRED].
#ifndef GEOM_MENUREND_IQUADDRAWER_H
#define GEOM_MENUREND_IQUADDRAWER_H
#include "../../Core/BuildConfig.h"
#if !DEDICATED
#include "../../Core/IBase.h"
namespace dice { namespace hfe { namespace geom {
class IQuadDrawer : public IBase
{
public:
    virtual void flush() = 0;
    virtual void beginFrame() = 0;
    virtual void resetState() = 0;
    virtual void endFrame() = 0;    // +1C 1008AF10

    // [CHOSEN, not recovered] Geometry recording for unchanged HUD elements (mod/hud-perf). Retail
    // has no such slots; they are appended after the retail table so its positions stay. A
    // recording holds every quad submitted between beginRecording and endRecording (they are still
    // drawn) together with the batch state, and replayRecording appends the same vertices again in
    // the same order, without the per-quad work of building them.
    virtual int  createRecording() = 0;                 // a new empty recording id
    virtual void releaseRecording(int id) = 0;
    virtual void beginRecording(int id) = 0;           // clears the recording, starts capturing
    virtual void endRecording() = 0;
    virtual bool replayRecording(int id) = 0;          // false when the id holds nothing usable
};
} } }
#endif
#endif
