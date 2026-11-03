/*  ROUTE — Energy-Routed Multimode Filter
    ---------------------------------------------------------------------------
    One conjugate pole pair per channel (TPT state-variable form) is tapped as
    LP / BP / HP, and those three taps are recombined through a UNIT-NORM routing
    vector built from two spherical angles:

        c = ( cos(theta)cos(phi), sin(theta)cos(phi), sin(phi) ),   ||c|| = 1

    Ordinary LP->BP->HP morphing crossfades the responses, which changes total
    gain as you sweep (worst around the middle, where two correlated responses
    both contribute). Because ||c|| = 1 here for every theta/phi, the routing
    itself contributes no gain change; only the filter does.

    The routing vector's orthogonal complement d (exactly perpendicular, also
    unit norm) is a "free" second signal derived from the same pole pair. It is
    injected anti-phase across the channels, so Width produces genuine
    decorrelation instead of a delay-based pseudo-stereo effect.

    Colour saturates the SVF's integrator states rather than the output, so
    distortion happens inside the resonance and interacts with Q the way an
    analogue filter's clipping does. tanh is bounded, so the loop stays stable.
*/
#pragma once
#include "../ToposCore.h"

namespace topos {

class EnergyRoute final : public FilterEngine
{
public:
    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = {
            "route", "ROUTE", "Unit-norm LP/BP/HP routing from one pole pair, with orthogonal width", 8,
            {
                { "cutoff", "Cutoff",     20.0f, 18000.0f, 800.0f, 0.3f, "Hz" },
                { "res",    "Resonance",   0.5f,    40.0f,   3.0f, 0.4f, ""   },
                { "theta",  "Route Theta", 0.0f,     1.0f,   0.0f, 1.0f, ""   },
                { "phi",    "Route Phi",   0.0f,     1.0f,   0.0f, 1.0f, ""   },
                { "width",  "Ortho Width", 0.0f,     1.0f,  0.35f, 1.0f, ""   },
                { "color",  "Colour",      0.0f,     1.0f,   0.0f, 1.0f, ""   },
                { "skew",   "Stereo Skew",-12.0f,   12.0f,   0.0f, 1.0f, "st" },
                { "poles",  "Poles",       1.0f,     2.0f,   1.0f, 1.0f, "x2", true },
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
        for (int s = 0; s < 2; ++s) { svfL[s].reset(); svfR[s].reset(); }
        dcL.reset(); dcR.reset();
    }

    void setParams (const float* p) override
    {
        cutoff = clampf (p[0], 20.0f, 18000.0f);
        res    = clampf (p[1], 0.5f, 40.0f);
        color  = clampf (p[5], 0.0f, 1.0f);
        width  = clampf (p[4], 0.0f, 1.0f);
        stages = (p[7] >= 1.5f) ? 2 : 1;

        const float th = clampf (p[2], 0.0f, 1.0f) * kPi * 0.5f;
        const float ph = clampf (p[3], 0.0f, 1.0f) * kPi * 0.5f;
        c1 = std::cos (th) * std::cos (ph);
        c2 = std::sin (th) * std::cos (ph);
        c3 = std::sin (ph);
        d1 = -std::sin (th);      // exactly orthogonal to c, unit norm
        d2 =  std::cos (th);
        d3 =  0.0f;

        const float skew = clampf (p[6], -12.0f, 12.0f);
        const float fL = clampf (cutoff * std::pow (2.0f, -skew / 24.0f), 20.0f, (float) (sr * 0.48));
        const float fR = clampf (cutoff * std::pow (2.0f,  skew / 24.0f), 20.0f, (float) (sr * 0.48));
        for (int s = 0; s < 2; ++s)
        {
            svfL[s].set (fL, res, sr);
            svfR[s].set (fR, res, sr);
        }
        satDrive = 1.0f + 3.0f * color;
        satComp  = 1.0f / satDrive;
    }

    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            float mainL, orthL, mainR, orthR;
            runChannel (svfL, L[s], mainL, orthL);
            runChannel (svfR, R[s], mainR, orthR);

            // orthogonal component injected anti-phase -> decorrelated width
            L[s] = dcL.process (mainL + width * orthL);
            R[s] = dcR.process (mainR - width * orthR);
        }
    }

private:
    inline void runChannel (SVF* svf, float x, float& main, float& orth)
    {
        float m = 0.0f, o = 0.0f;
        float sig = x;
        for (int st = 0; st < stages; ++st)
        {
            svf[st].process (sig);

            if (color > 0.0f)     // saturate the integrator states, inside the resonance
            {
                svf[st].ic1 = (1.0f - color) * svf[st].ic1 + color * fastTanh (svf[st].ic1 * satDrive) * satComp;
                svf[st].ic2 = (1.0f - color) * svf[st].ic2 + color * fastTanh (svf[st].ic2 * satDrive) * satComp;
            }

            m = c1 * svf[st].lp + c2 * svf[st].bp + c3 * svf[st].hp;
            o = d1 * svf[st].lp + d2 * svf[st].bp + d3 * svf[st].hp;
            sig = m;              // cascade the routed output for the 4-pole option
        }
        main = sanitise (m);
        orth = sanitise (o);
    }

    SVF     svfL[2], svfR[2];
    DCBlock dcL, dcR;
    float   c1 = 1.0f, c2 = 0.0f, c3 = 0.0f, d1 = 0.0f, d2 = 1.0f, d3 = 0.0f;
    float   cutoff = 800.0f, res = 3.0f, color = 0.0f, width = 0.35f;
    float   satDrive = 1.0f, satComp = 1.0f;
    int     stages = 1;
};

} // namespace topos
