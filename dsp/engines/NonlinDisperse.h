/*  DISPERSE — Envelope-Dependent Nonlinear Dispersion
    ---------------------------------------------------------------------------
    A cascade of first-order allpass sections whose coefficients are driven by the
    input envelope, so loud and quiet material propagate with different group
    delay. Magnitude response stays flat; what changes is transient shape and the
    implied material (metal, wood, gut, something impossible).

    Each section is implemented as a 2x2 ROTATION, not as a direct-form allpass:

        out   = -s*x + c*state
        state =  c*x + s*state          with c = sqrt(1 - s^2)

    Its transfer function is exactly the first-order allpass
    (z^-1 - s)/(1 - s*z^-1), but the state update is an orthogonal matrix, whose
    singular values are 1 for every s. A product of orthogonal matrices is
    orthogonal, so the cascade is norm-preserving at every instant even while all
    coefficients are being modulated at audio rate. That is precisely what a
    naively modulated direct-form allpass chain fails to guarantee: there, fast
    coefficient modulation can pump energy into the state (parametric
    amplification). Here it structurally cannot.

    The envelope detector is bandpassed (Env Focus) so dispersion can respond to
    transient content rather than overall level.
*/
#pragma once
#include "../ToposCore.h"

namespace topos {

class NonlinDisperse final : public FilterEngine
{
public:
    static constexpr int kMaxStages = 12;

    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = {
            "disperse", "DISPERSE", "Allpass rotation lattice with envelope-dependent group delay", 8,
            {
                { "disp",   "Dispersion",  -0.95f,   0.95f,  0.55f, 1.0f, ""   },
                { "depth",  "Env Depth",   -1.0f,    1.0f,   0.60f, 1.0f, ""   },
                { "tilt",   "Coeff Tilt",  -1.0f,    1.0f,   0.30f, 1.0f, ""   },
                { "atk",    "Env Attack",   0.1f,  200.0f,   3.0f,  0.3f, "ms" },
                { "rel",    "Env Release",  1.0f, 2000.0f, 120.0f,  0.3f, "ms" },
                { "stages", "Stages",       1.0f,   12.0f,   8.0f,  1.0f, "",  true },
                { "spread", "Stereo Spread",0.0f,    1.0f,   0.50f, 1.0f, ""   },
                { "focus",  "Env Focus",   20.0f, 8000.0f,  60.0f,  0.3f, "Hz" },
            }
        };
        return d;
    }
    const EngineDescriptor& desc() const override { return descriptor(); }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; invSr = 1.0f / (float) sampleRate;
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kMaxStages; ++i) { zL[i] = 0.0f; zR[i] = 0.0f; }
        envL.reset(); envR.reset(); hpL.reset(); hpR.reset();
    }

    void setParams (const float* p) override
    {
        base   = clampf (p[0], -0.95f, 0.95f);
        depth  = clampf (p[1], -1.0f, 1.0f);
        tilt   = clampf (p[2], -1.0f, 1.0f);
        stages = (int) clampf (std::round (p[5]), 1.0f, (float) kMaxStages);
        spread = clampf (p[6], 0.0f, 1.0f);
        envL.setTimesMs (clampf (p[3], 0.1f, 200.0f), clampf (p[4], 1.0f, 2000.0f), sr);
        envR.setTimesMs (clampf (p[3], 0.1f, 200.0f), clampf (p[4], 1.0f, 2000.0f), sr);
        hpL.setCutoff (clampf (p[7], 20.0f, 8000.0f), sr);
        hpR.setCutoff (clampf (p[7], 20.0f, 8000.0f), sr);
        polR = 1.0f - 2.0f * spread;      // +1 = mono behaviour, -1 = mirrored
    }

    void process (float* L, float* R, int n) override
    {
        for (int s = 0; s < n; ++s)
        {
            // envelope of the *high* part of the signal: transient sensitive
            const float eL = clampf (envL.process (L[s] - hpL.process (L[s])), 0.0f, 1.0f);
            const float eR = clampf (envR.process (R[s] - hpR.process (R[s])), 0.0f, 1.0f);

            L[s] = sanitise (runChain (zL, L[s], eL, 1.0f));
            R[s] = sanitise (runChain (zR, R[s], eR, polR));
        }
    }

private:
    inline float coefFor (int i, float e, float pol) const
    {
        const float grad = (stages > 1) ? (2.0f * (float) i / (float) (stages - 1) - 1.0f) : 0.0f;
        return clampf (pol * (base * (1.0f + tilt * grad) + depth * e), -0.98f, 0.98f);
    }

    inline float runChain (float* z, float x, float e, float pol)
    {
        float v = x;
        for (int i = 0; i < stages; ++i)
        {
            const float s = coefFor (i, e, pol);
            const float c = std::sqrt (std::max (0.0f, 1.0f - s * s));
            const float out   = -s * v + c * z[i];
            const float state =  c * v + s * z[i];      // orthogonal 2x2 rotation
            z[i] = state;
            v = out;
        }
        return v;
    }

    float         zL[kMaxStages] {}, zR[kMaxStages] {};
    EnvFollower   envL, envR;
    LP1           hpL, hpR;
    float base = 0.55f, depth = 0.6f, tilt = 0.3f, spread = 0.5f, polR = 0.0f;
    int   stages = 8;
};

} // namespace topos
