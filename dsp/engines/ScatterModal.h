/*  SCATTER — Coupled Modal Scattering Network
    ---------------------------------------------------------------------------
    N digital-waveguide branches (delay + damping + loss) all terminating at one
    shared N-port scattering junction, written in NORMALISED (power) wave
    variables. The junction of a parallel connection with branch admittances Y_i
    becomes, after normalising each wave by sqrt(Y_i), a Householder reflection:

        u_i   = sqrt(Y_i / sum_j Y_j)          so ||u|| = 1
        j     = sum_i u_i * in_i               (normalised junction variable)
        out_i = 2*loss*u_i*H(j) - in_i         (scattering)

    With loss = 1 and H = 1 the scattering matrix is exactly S = 2 u u^T - I, an
    orthogonal (Householder) matrix, so ||S|| = 1 for ANY admittance
    distribution. That is the whole stability argument, and it holds
    instantaneously, which is what makes the topology safe to modulate at audio
    rate: for loss <= 1 the singular values are |2*loss - 1| <= 1 and 1, and
    every branch additionally has loop gain < 1.

    (The unnormalised Kelly-Lochbaum form p = sum alpha_i in_i, out_i = p - in_i
    is only lossless in the admittance-weighted norm. In plain L2 — the norm the
    surrounding delay loops actually live in — its gain exceeds 1 for every
    non-uniform coupling setting: 1.30x at coupling 0.9, 2.98x at 0.5, 5.47x in
    the star limit for N = 8. See tools/junction_passivity_proof.py. A resonant
    network built on that form runs away, and the offline suite caught it doing
    exactly that. Normalised waves remove the trap entirely.)

    The u VECTOR is the topology control, and this is what separates the engine
    from a morphing resonator bank:

        Coupling = 0   all weight on branch 0 -> STAR / uncoupled. S becomes
                       diag(+1, -1, ..., -1): eight independent resonant loops.
                       Modes stay separate and clean.
        Coupling = 1   uniform weights        -> MESH. Every branch loads every
                       other through the junction, mode frequencies pull on each
                       other, and the response becomes plate-like and diffuse.

    So the same eight resonators are re-wired, not re-tuned. H is a first-order
    lowpass in the junction itself. A one-pole lowpass with unity DC gain is
    positive real (its Nyquist locus is the circle through 0 and 1 in the right
    half plane), so |2H - 1| <= 1 and the reflection stays non-expansive while
    becoming frequency dependent: high partials couple less than low ones, as at
    a real loaded junction.
*/
#pragma once
#include "../ToposCore.h"

namespace topos {

class ScatterModal final : public FilterEngine
{
public:
    static constexpr int N = 8;

    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = {
            "scatter", "SCATTER", "Coupled modal scattering network: star <-> mesh topology", 8,
            {
                { "f0",     "Fundamental", 20.0f,  2000.0f, 110.0f,  0.3f, "Hz" },
                { "morph",  "Ratio Morph",  0.0f,     2.0f,   0.0f,  1.0f, ""   },
                { "coup",   "Coupling",     0.0f,     1.0f,   0.5f,  1.0f, ""   },
                { "decay",  "Decay",        0.05f,   20.0f,   2.0f,  0.3f, "s"  },
                { "bright", "Brightness", 200.0f, 18000.0f, 6000.0f, 0.3f, "Hz" },
                { "jlp",    "Junction LP",500.0f, 20000.0f, 9000.0f, 0.3f, "Hz" },
                { "loss",   "Losslessness", 0.90f,    1.0f, 0.999f,  1.0f, ""   },
                { "spread", "Spread",       0.0f,     1.0f,  0.55f,  1.0f, ""   },
            }
        };
        return d;
    }
    const EngineDescriptor& desc() const override { return descriptor(); }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; invSr = 1.0f / (float) sampleRate;
        const int maxDelay = (int) (sampleRate / 18.0) + 8;   // down to ~18 Hz
        for (int i = 0; i < N; ++i) delay[i].allocate (maxDelay);
        dcL.setSampleRate (sampleRate); dcR.setSampleRate (sampleRate);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < N; ++i) { delay[i].clear(); damp[i].reset(); inWave[i] = 0.0f; }
        jlp.reset(); dcL.reset(); dcR.reset();
    }

    void setParams (const float* p) override
    {
        f0     = clampf (p[0], 20.0f, 2000.0f);
        morph  = clampf (p[1], 0.0f, 2.0f);
        coup   = clampf (p[2], 0.0f, 1.0f);
        decayS = clampf (p[3], 0.05f, 20.0f);
        bright = clampf (p[4], 200.0f, 18000.0f);
        jlpHz  = clampf (p[5], 500.0f, 20000.0f);
        loss   = clampf (p[6], 0.90f, 1.0f);
        spread = clampf (p[7], 0.0f, 1.0f);
        updateCoefficients();
    }

    void process (float* L, float* R, int n) override
    {
        const float inGain = 1.0f / std::sqrt ((float) N);

        for (int s = 0; s < n; ++s)
        {
            const float x = 0.5f * (L[s] + R[s]);   // one resonant object, mono excited

            // ---- N-port junction: normalised wave variable, then reflection --
            float j = 0.0f;
            for (int i = 0; i < N; ++i) j += u[i] * (inWave[i] + x * inGain);
            j = jlp.process (j);                    // positive-real junction load

            float sumL = 0.0f, sumR = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float incoming = inWave[i] + x * inGain;
                const float outgoing = twoLoss * u[i] * j - incoming;   // Householder

                // ---- branch waveguide: delay -> damping -> loss ------------
                delay[i].write (sanitise (outgoing));
                float b = delay[i].read (dl[i]);
                b = damp[i].process (b) * lg[i];
                inWave[i] = sanitise (b);

                sumL += b * wL[i];
                sumR += b * wR[i];
            }

            L[s] = dcL.process (sumL * 0.5f);
            R[s] = dcR.process (sumR * 0.5f);
        }
    }

    float tailSeconds() const override { return decayS; }

private:
    void updateCoefficients()
    {
        // --- modal ratio templates: harmonic -> circular plate -> inharmonic
        static const float plate[N] = { 1.0f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f, 3.155f, 3.500f };

        for (int i = 0; i < N; ++i)
        {
            const float rh = (float) (i + 1);
            const float rp = plate[i];
            const float rg = std::pow (1.6180339887f, (float) i * 0.8f);
            const float r  = (morph < 1.0f) ? (1.0f - morph) * rh + morph * rp
                                            : (2.0f - morph) * rp + (morph - 1.0f) * rg;

            const float hz = clampf (f0 * r, 18.0f, (float) (sr * 0.45));
            dl[i] = clampf ((float) sr / hz - 1.0f, 2.0f, (float) (delay[i].size - 3));

            // per-branch loop gain giving the requested T60 for that loop length
            lg[i] = std::pow (0.001f, dl[i] / std::max (1.0f, decayS * (float) sr));
            lg[i] = clampf (lg[i], 0.0f, 0.99999f);

            damp[i].setCutoff (bright, sr);

            // --- topology vector: star (all weight on branch 0) <-> mesh (uniform)
            // w sums to exactly 1 for any coupling, so ||u|| = 1 exactly.
            const float w = coup / (float) N + (1.0f - coup) * (i == 0 ? 1.0f : 0.0f);
            u[i] = std::sqrt (std::max (0.0f, w));

            // stereo pickups: even/odd branch weighting, widened by Spread
            const float even = (i % 2 == 0) ? 1.0f : 0.0f;
            wL[i] = 1.0f - spread * (1.0f - even);
            wR[i] = 1.0f - spread * even;
        }
        jlp.setCutoff (jlpHz, sr);
        twoLoss = 2.0f * loss;
    }

    FracDelay delay[N];
    LP1       damp[N], jlp;
    DCBlock   dcL, dcR;
    float     inWave[N] {}, u[N] {}, dl[N] {}, lg[N] {}, wL[N] {}, wR[N] {};
    float     twoLoss = 2.0f;
    float     f0 = 110.0f, morph = 0.0f, coup = 0.5f, decayS = 2.0f;
    float     bright = 6000.0f, jlpHz = 9000.0f, loss = 0.999f, spread = 0.55f;
};

} // namespace topos
