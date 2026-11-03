/*  TOPOS — Modular Filter Matrix
    dsp/ToposCore.h : framework-free DSP core API.

    Nothing in dsp/ may include JUCE. The plugin wrapper (Source/) owns all
    host/format code; the rack below owns all audio behaviour, so the same core
    compiles into the plugin, into offline test harnesses, and later into
    embedded targets (Daisy) without change.

    ---------------------------------------------------------------------------
    EXPANSION CONTRACT  (read before adding a filter)
    ---------------------------------------------------------------------------
    1. Add dsp/engines/MyThing.h defining `class MyThing final : public FilterEngine`.
    2. Give it a static `descriptor()` with a STABLE `id` string and <= kMaxEngineParams
       ParamSpec entries. `id` is what gets written into host sessions — never change
       it, never reorder existing params, only append.
    3. Register it in dsp/EngineRegistry.h (append to the table; never reorder).
    That is the whole procedure: no plugin parameters are created or renamed, no
    editor code changes, no host-session breakage. Slot parameters are generic
    (slotN_pM) and are re-labelled from the descriptor at runtime.
*/
#pragma once

#include <cmath>
#include <cstring>
#include <memory>
#include <algorithm>

namespace topos {

//============================================================================
// limits
//============================================================================
static constexpr int   kMaxEngineParams = 8;
static constexpr int   kNumSlots        = 6;   // raise freely: params are generated
static constexpr int   kNumLanes        = 3;
static constexpr float kPi              = 3.14159265358979323846f;

//============================================================================
// parameter description
//============================================================================
struct ParamSpec
{
    const char* id      = "";      // stable, lowercase, no spaces
    const char* label   = "";      // shown in the editor
    float       min     = 0.0f;
    float       max     = 1.0f;
    float       def     = 0.0f;
    float       skew    = 1.0f;    // 1 = linear; <1 biases toward min (use 0.3 for Hz)
    const char* unit    = "";
    bool        stepped = false;   // integer-valued control
};

struct EngineDescriptor
{
    const char* id    = "none";    // stable session key
    const char* name  = "None";
    const char* blurb = "";
    int         numParams = 0;
    ParamSpec   params[kMaxEngineParams] {};
};

//============================================================================
// engine base class
//============================================================================
class FilterEngine
{
public:
    virtual ~FilterEngine() = default;

    virtual const EngineDescriptor& desc() const = 0;

    /** Called before playback and on any sample-rate change. May allocate. */
    virtual void prepare (double sampleRate, int maxBlock) = 0;

    /** Clear all state. Must be allocation free and realtime safe. */
    virtual void reset() = 0;

    /** Real-world parameter values, kMaxEngineParams entries, already smoothed
        by the host wrapper at block rate. Realtime safe. */
    virtual void setParams (const float* p) = 0;

    /** In-place stereo processing. Realtime safe, no allocation. */
    virtual void process (float* left, float* right, int numSamples) = 0;

    /** Extra tail beyond the host's own reported latency, in seconds. */
    virtual float tailSeconds() const { return 0.0f; }

protected:
    double sr        = 48000.0;
    float  invSr     = 1.0f / 48000.0f;
};

//============================================================================
// small shared helpers
//============================================================================
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float sanitise (float v)
{
    if (! std::isfinite (v)) return 0.0f;
    return clampf (v, -64.0f, 64.0f);
}

/** cheap bounded tanh (max error ~2e-3, monotonic, exact odd symmetry) */
inline float fastTanh (float x)
{
    x = clampf (x, -8.0f, 8.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** one-pole smoother in "coefficient" form */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void  setTimeMs (float ms, double sampleRate)
    {
        const float n = std::max (1.0f, (float) (ms * 0.001 * sampleRate));
        a = std::exp (-1.0f / n);
    }
    inline float process (float x) { z = x + (z - x) * a; return z; }
    void reset (float v = 0.0f) { z = v; }
};

/** direction-dependent (asymmetric) smoother — the hysteresis primitive */
struct AsymSmooth
{
    float up = 0.0f, dn = 0.0f, z = 0.0f;
    void setTimesMs (float upMs, float dnMs, double sampleRate)
    {
        up = std::exp (-1.0f / std::max (1.0f, (float) (upMs * 0.001 * sampleRate)));
        dn = std::exp (-1.0f / std::max (1.0f, (float) (dnMs * 0.001 * sampleRate)));
    }
    inline float process (float x)
    {
        const float a = (x > z) ? up : dn;
        z = x + (z - x) * a;
        return z;
    }
    void reset (float v = 0.0f) { z = v; }
};

/** attack/release amplitude follower */
struct EnvFollower
{
    float ca = 0.0f, cr = 0.0f, z = 0.0f;
    void setTimesMs (float atkMs, float relMs, double sampleRate)
    {
        ca = std::exp (-1.0f / std::max (1.0f, (float) (atkMs * 0.001 * sampleRate)));
        cr = std::exp (-1.0f / std::max (1.0f, (float) (relMs * 0.001 * sampleRate)));
    }
    inline float process (float x)
    {
        const float r = std::fabs (x);
        z = (r > z) ? (r + (z - r) * ca) : (r + (z - r) * cr);
        return z;
    }
    void reset() { z = 0.0f; }
};

/** DC blocker (zero at DC, pole just inside it) */
struct DCBlock
{
    float x1 = 0.0f, y1 = 0.0f, R = 0.9995f;
    void setSampleRate (double sampleRate) { R = 1.0f - (float) (2.0 * kPi * 8.0 / sampleRate); }
    inline float process (float x) { const float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0.0f; }
};

/** first-order lowpass used for loop damping */
struct LP1
{
    float a = 0.5f, z = 0.0f;
    void setCutoff (float hz, double sampleRate)
    {
        const float w = clampf (hz, 10.0f, (float) (sampleRate * 0.49));
        a = 1.0f - std::exp (-2.0f * kPi * w / (float) sampleRate);
    }
    inline float process (float x) { z += a * (x - z); return z; }
    void reset() { z = 0.0f; }
};

/** TPT state-variable filter with simultaneous LP/BP/HP taps */
struct SVF
{
    float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void set (float hz, float q, double sampleRate)
    {
        const float f = clampf (hz, 10.0f, (float) (sampleRate * 0.48));
        g  = std::tan (kPi * f / (float) sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1; lp = v2; hp = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0.0f; lp = bp = hp = 0.0f; }
};

/** interpolating delay line (power-of-two buffer, linear interpolation) */
struct FracDelay
{
    std::unique_ptr<float[]> buf;
    int   size = 0, mask = 0, w = 0;

    void allocate (int maxSamples)
    {
        int n = 8;
        while (n < maxSamples + 4) n <<= 1;
        size = n; mask = n - 1;
        buf.reset (new float[(size_t) n]);
        clear();
    }
    void clear() { if (buf) std::fill (buf.get(), buf.get() + size, 0.0f); w = 0; }

    inline void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    inline float read (float delaySamples) const
    {
        const float d  = clampf (delaySamples, 1.0f, (float) (size - 3));
        const int   i  = (int) d;
        const float fr = d - (float) i;
        const int   r0 = (w - i - 1) & mask;
        const int   r1 = (r0 - 1) & mask;
        return buf[(size_t) r0] + fr * (buf[(size_t) r1] - buf[(size_t) r0]);
    }
};

} // namespace topos
