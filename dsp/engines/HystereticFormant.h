/*  VOWEL — Hysteretic Formant Filter
    ---------------------------------------------------------------------------
    A four-formant resonator bank in which every formant frequency and bandwidth
    reaches its target through a DIRECTION-DEPENDENT slew: opening gestures use
    one time constant, closing gestures another. The bank therefore has
    articulatory memory — arriving at a vowel position from below sounds
    different from arriving at it from above, and fast gestures overshoot in one
    direction only, the way a real articulator with asymmetric muscle dynamics
    does.

    On top of that, input level drags the formants (Growl): loud input pulls
    centre frequencies down and bandwidths up, and because that drag passes
    through the same asymmetric smoother, it does not track level symmetrically.
    Sustained loud input settles into a wide, growly configuration and takes the
    slow path back.

    A nasal anti-formant (notch) is available; real nasalised vowels have zeros,
    not just poles, and their absence is a common giveaway in formant filters.
*/
#pragma once
#include "../ToposCore.h"

namespace topos {

class HystereticFormant final : public FilterEngine
{
public:
    static constexpr int NF = 4;

    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = {
            "vowel", "VOWEL", "Formant bank with asymmetric, level-dragged formant trajectories", 8,
            {
                { "vowel",  "Vowel",       0.0f,   4.0f,  0.0f, 1.0f, ""   },
                { "tract",  "Tract Scale", 0.5f,   2.0f,  1.0f, 1.0f, "x"  },
                { "open",   "Open Slew",   0.5f, 500.0f, 12.0f, 0.3f, "ms" },
                { "close",  "Close Slew",  0.5f, 500.0f, 90.0f, 0.3f, "ms" },
                { "growl",  "Growl",       0.0f,   1.0f, 0.35f, 1.0f, ""   },
                { "bw",     "Bandwidth",   0.25f,  4.0f,  1.0f, 1.0f, "x"  },
                { "nasal",  "Nasal Notch", 0.0f,   1.0f,  0.0f, 1.0f, ""   },
                { "tiltp",  "Formant Tilt",-1.0f,  1.0f,  0.0f, 1.0f, ""   },
            }
        };
        return d;
    }
    const EngineDescriptor& desc() const override { return descriptor(); }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; invSr = 1.0f / (float) sampleRate;
        dcL.setSampleRate (sampleRate); dcR.setSampleRate (sampleRate);
        envL.setTimesMs (5.0f, 150.0f, sampleRate);
        envR.setTimesMs (5.0f, 150.0f, sampleRate);
        for (int i = 0; i < NF; ++i)
        {
            fSm[i].reset (700.0f); bSm[i].reset (90.0f);
        }
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < NF; ++i) { resL[i].reset(); resR[i].reset(); }
        notchL.reset(); notchR.reset(); envL.reset(); envR.reset();
        dcL.reset(); dcR.reset();
    }

    void setParams (const float* p) override
    {
        vowel  = clampf (p[0], 0.0f, 4.0f);
        tract  = clampf (p[1], 0.5f, 2.0f);
        growl  = clampf (p[4], 0.0f, 1.0f);
        bwScl  = clampf (p[5], 0.25f, 4.0f);
        nasal  = clampf (p[6], 0.0f, 1.0f);
        tiltP  = clampf (p[7], -1.0f, 1.0f);
        for (int i = 0; i < NF; ++i)
        {
            fSm[i].setTimesMs (clampf (p[2], 0.5f, 500.0f), clampf (p[3], 0.5f, 500.0f), sr);
            bSm[i].setTimesMs (clampf (p[2], 0.5f, 500.0f), clampf (p[3], 0.5f, 500.0f), sr);
        }
        computeTargets();
    }

    void process (float* L, float* R, int n) override
    {
        // level drag is evaluated per block from the previous block's envelope,
        // then the asymmetric smoothers run per sample so the trajectory (not
        // just the endpoint) is shaped.
        for (int s = 0; s < n; ++s)
        {
            const float e = 0.5f * (envL.process (L[s]) + envR.process (R[s]));
            const float drag = clampf (e, 0.0f, 1.0f);

            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < NF; ++i)
            {
                const float fT = fTarget[i] * (1.0f - 0.25f * growl * drag);
                const float bT = bTarget[i] * (1.0f + 3.0f * growl * drag);
                const float f  = clampf (fSm[i].process (fT), 40.0f, (float) (sr * 0.45));
                const float b  = clampf (bSm[i].process (bT), 10.0f, 2000.0f);
                const float q  = clampf (f / b, 0.5f, 60.0f);

                resL[i].set (f, q, sr); resR[i].set (f, q, sr);
                resL[i].process (L[s]); resR[i].process (R[s]);
                outL += resL[i].bp * gain[i];
                outR += resR[i].bp * gain[i];
            }

            if (nasal > 0.0f)
            {
                const float nf = 1000.0f * tract;
                notchL.set (nf, clampf (nf / (180.0f * bwScl), 0.5f, 30.0f), sr);
                notchR.set (nf, clampf (nf / (180.0f * bwScl), 0.5f, 30.0f), sr);
                notchL.process (outL); notchR.process (outR);
                const float nL = notchL.lp + notchL.hp;   // notch = LP + HP
                const float nR = notchR.lp + notchR.hp;
                outL = (1.0f - nasal) * outL + nasal * nL;
                outR = (1.0f - nasal) * outR + nasal * nR;
            }

            L[s] = sanitise (dcL.process (outL));
            R[s] = sanitise (dcR.process (outR));
        }
    }

private:
    void computeTargets()
    {
        // F1..F4 for A E I O U, and nominal bandwidths
        static const float tab[5][NF] = {
            { 730.f, 1090.f, 2440.f, 3400.f },   // A
            { 530.f, 1840.f, 2480.f, 3500.f },   // E
            { 270.f, 2290.f, 3010.f, 3700.f },   // I
            { 570.f,  840.f, 2410.f, 3300.f },   // O
            { 300.f,  870.f, 2240.f, 3200.f },   // U
        };
        static const float bwt[NF] = { 60.f, 90.f, 120.f, 160.f };

        const int   i0 = (int) clampf (std::floor (vowel), 0.0f, 3.0f);
        const int   i1 = i0 + 1;
        const float fr = clampf (vowel - (float) i0, 0.0f, 1.0f);

        for (int i = 0; i < NF; ++i)
        {
            fTarget[i] = ((1.0f - fr) * tab[i0][i] + fr * tab[i1][i]) * tract;
            bTarget[i] = bwt[i] * bwScl;
            static const float g0[NF] = { 1.0f, 0.65f, 0.4f, 0.22f };
            // Formant Tilt rebalances low vs high formants
            const float t = (float) i / (float) (NF - 1) * 2.0f - 1.0f;
            gain[i] = g0[i] * std::pow (2.0f, tiltP * t * 1.5f);
        }
    }

    SVF        resL[NF], resR[NF], notchL, notchR;
    AsymSmooth fSm[NF], bSm[NF];
    EnvFollower envL, envR;
    DCBlock    dcL, dcR;
    float fTarget[NF] {}, bTarget[NF] {}, gain[NF] {};
    float vowel = 0.0f, tract = 1.0f, growl = 0.35f, bwScl = 1.0f, nasal = 0.0f, tiltP = 0.0f;
};

} // namespace topos
