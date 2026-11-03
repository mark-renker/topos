/*  THERMAL — Electrothermal Ladder Filter
    ---------------------------------------------------------------------------
    A four-stage nonlinear transistor-ladder low-pass wrapped in slow PHYSICAL
    state, which is the part ordinary virtual-analogue filters leave out. Most VA
    models are memoryless with respect to their own history: the same knob
    position always gives the same response. Real ladders do not behave that way.

      * Junction temperature. The exponential converter that sets ladder cutoff
        has scale factor proportional to kT/q. Signal power dissipated in the
        ladder heats the die, and the tuning drifts while you play. Modelled as a
        one-pole integration of a power proxy (tens of ms to seconds), which
        pulls cutoff and effective Q down as it rises.
      * Rail sag. Drive the filter hard and the supply droops, so available
        headroom shrinks with a slow recovery. Modelled as a one-pole on |x|
        scaling the pre-ladder swing, which makes the distortion character
        level-history dependent, not just level dependent.
      * Device mismatch. Fixed per-stage asymmetry coefficients (this particular
        unit's transistor pairs) break the ideal odd symmetry of the ladder,
        adding weak even-order content and asymmetric clipping. The asymmetry is
        applied as off*y*|y| rather than as a DC offset, so it scales with signal
        level and vanishes exactly at silence — a DC offset would make the plugin
        emit a small thump-and-settle every time the host resumes processing.

    The ladder itself is the standard unit-delay-resolved nonlinear cascade: each
    stage is a saturating one-pole, and the global resonance path carries one
    sample of delay. Every stage is bounded by its own tanh, which is what keeps
    it stable at high resonance and high drive. Run the rack at 2x or 4x
    oversampling when driving this engine hard.
*/
#pragma once
#include "../ToposCore.h"

namespace topos {

class ElectrothermalLadder final : public FilterEngine
{
public:
    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = {
            "thermal", "THERMAL", "Nonlinear ladder with thermal drift, rail sag and device mismatch", 8,
            {
                { "cutoff", "Cutoff",       20.0f, 18000.0f, 900.0f, 0.3f, "Hz" },
                { "res",    "Resonance",     0.0f,    1.05f,   0.5f, 1.0f, ""   },
                { "drive",  "Ladder Drive",  0.1f,   20.0f,   1.0f,  0.4f, "x"  },
                { "tk",     "Thermal Depth", 0.0f,    1.0f,    0.5f, 1.0f, ""   },
                { "tt",     "Thermal Time",  5.0f, 5000.0f, 300.0f,  0.3f, "ms" },
                { "sag",    "Rail Sag",      0.0f,    1.0f,    0.4f, 1.0f, ""   },
                { "sagt",   "Sag Recovery",  1.0f, 2000.0f,  60.0f,  0.3f, "ms" },
                { "mis",    "Mismatch",      0.0f,    1.0f,    0.3f, 1.0f, ""   },
            }
        };
        return d;
    }
    const EngineDescriptor& desc() const override { return descriptor(); }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; invSr = 1.0f / (float) sampleRate;
        dcL.setSampleRate (sampleRate); dcR.setSampleRate (sampleRate);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < 4; ++i) { yL[i] = 0.0f; yR[i] = 0.0f; }
        thermL.reset(); thermR.reset(); sagL.reset(); sagR.reset();
        dcL.reset(); dcR.reset();
    }

    void setParams (const float* p) override
    {
        cutoff  = clampf (p[0], 20.0f, 18000.0f);
        res     = clampf (p[1], 0.0f, 1.05f);
        drive   = clampf (p[2], 0.1f, 20.0f);
        thermK  = clampf (p[3], 0.0f, 1.0f);
        sagAmt  = clampf (p[5], 0.0f, 1.0f);
        mismatch= clampf (p[7], 0.0f, 1.0f);
        thermL.setTimeMs (clampf (p[4], 5.0f, 5000.0f), sr);
        thermR.setTimeMs (clampf (p[4], 5.0f, 5000.0f), sr);
        sagL.setTimeMs (clampf (p[6], 1.0f, 2000.0f), sr);
        sagR.setTimeMs (clampf (p[6], 1.0f, 2000.0f), sr);

        static const float off[4] = { 0.013f, -0.021f, 0.008f, -0.006f };
        for (int i = 0; i < 4; ++i) offs[i] = off[i] * mismatch;
    }

    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            L[s] = sanitise (dcL.process (runLadder (yL, thermL, sagL, L[s])));
            R[s] = sanitise (dcR.process (runLadder (yR, thermR, sagR, R[s])));
        }
    }

private:
    inline float runLadder (float* y, OnePole& therm, OnePole& sagFilt, float x)
    {
        // ---- slow physical state --------------------------------------------
        const float temp = clampf (therm.process (x * x), 0.0f, 4.0f);
        const float sag  = clampf (sagFilt.process (std::fabs (x)), 0.0f, 1.0f);

        const float fc  = clampf (cutoff * (1.0f - 0.35f * thermK * temp), 20.0f, (float) (sr * 0.45));
        const float k   = 4.0f * res * (1.0f - 0.20f * thermK * temp);
        const float hr  = 1.0f - 0.7f * sagAmt * sag;
        const float g   = std::min (1.0f, 2.0f * kPi * fc * invSr);

        // ---- unit-delay-resolved nonlinear ladder ---------------------------
        const float u = fastTanh ((x * drive - k * y[3]) * hr);

        y[0] = bound (y[0] + g * (u          - satAsym (y[0], offs[0])));
        y[1] = bound (y[1] + g * (satAsym (y[0], 0.0f) - satAsym (y[1], offs[1])));
        y[2] = bound (y[2] + g * (satAsym (y[1], 0.0f) - satAsym (y[2], offs[2])));
        y[3] = bound (y[3] + g * (satAsym (y[2], 0.0f) - satAsym (y[3], offs[3])));

        // resonance costs level in a real ladder; compensate part of it back
        return y[3] * (1.0f + 0.6f * k);
    }

    static inline float bound (float v) { return clampf (v, -8.0f, 8.0f); }

    /** saturation with level-scaled asymmetry: zero in, zero out */
    static inline float satAsym (float y, float off)
    {
        return fastTanh (y + off * y * std::fabs (y));
    }

    float   yL[4] {}, yR[4] {}, offs[4] {};
    OnePole thermL, thermR, sagL, sagR;
    DCBlock dcL, dcR;
    float   cutoff = 900.0f, res = 0.5f, drive = 1.0f, thermK = 0.5f;
    float   sagAmt = 0.4f, mismatch = 0.3f;
};

} // namespace topos
