/*  ToposRack — the mix/route matrix.
    ---------------------------------------------------------------------------
    Six slots. Each slot hosts any registered engine and declares:
        lane      which of three parallel lanes it belongs to
        position  its order WITHIN that lane (series order, freely reorderable)
        drive     pre-engine gain (into the engine's nonlinearity)
        mix       per-slot parallel blend of that engine against its own input
        level/pan post-engine placement into the lane
    Lanes run in parallel and are summed with their own level/mute. So the rack
    covers series chains, parallel banks, and any mixture of the two, with
    arbitrary ordering — and adding engines never changes this layer.

    Engine instances for every registered engine are pre-allocated per slot at
    prepare() time, so changing a slot's engine while audio is running never
    allocates and never blocks.

    Oversampling (1x/2x/4x) wraps the entire rack, because the nonlinear engines
    (THERMAL, ROUTE's colour, DISPERSE's envelope-driven coefficients) generate
    harmonics above Nyquist when pushed.
*/
#pragma once

#include "EngineRegistry.h"
#include <vector>

namespace topos {

//============================================================================
// halfband oversampler (windowed-sinc FIR, cascadable to 4x)
//============================================================================
class Halfband
{
public:
    static constexpr int kTaps = 31;

    void prepare (int maxUpBlock)
    {
        buildCoefficients();
        upState.assign (kTaps, 0.0f);
        dnState.assign (kTaps, 0.0f);
        scratch.assign ((size_t) std::max (8, maxUpBlock) + kTaps, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill (upState.begin(), upState.end(), 0.0f);
        std::fill (dnState.begin(), dnState.end(), 0.0f);
    }

    /** n input samples -> 2n output samples */
    void upsample (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            push (upState, in[i]);
            out[2 * i]     = 2.0f * dot (upState);     // zero-stuffed, gain compensated
            push (upState, 0.0f);
            out[2 * i + 1] = 2.0f * dot (upState);
        }
    }

    /** 2n input samples -> n output samples */
    void downsample (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            push (dnState, in[2 * i]);
            const float y = dot (dnState);
            push (dnState, in[2 * i + 1]);
            out[i] = y;
        }
    }

private:
    void buildCoefficients()
    {
        if (built) return;
        const int   M  = kTaps - 1;
        const float fc = 0.25f;                 // halfband
        float sum = 0.0f;
        for (int i = 0; i <= M; ++i)
        {
            const float m = (float) i - (float) M * 0.5f;
            const float s = (std::fabs (m) < 1.0e-6f) ? 2.0f * fc
                                                      : std::sin (2.0f * kPi * fc * m) / (kPi * m);
            const float w = 0.42f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) M)
                                  + 0.08f * std::cos (4.0f * kPi * (float) i / (float) M);
            h[i] = s * w;
            sum += h[i];
        }
        for (int i = 0; i <= M; ++i) h[i] /= sum;   // unity DC gain
        built = true;
    }
    static inline void push (std::vector<float>& z, float x)
    {
        for (int i = (int) z.size() - 1; i > 0; --i) z[(size_t) i] = z[(size_t) i - 1];
        z[0] = x;
    }
    inline float dot (const std::vector<float>& z) const
    {
        float acc = 0.0f;
        for (int i = 0; i < kTaps; ++i) acc += h[i] * z[(size_t) i];
        return acc;
    }

    float h[kTaps] {};
    bool  built = false;
    std::vector<float> upState, dnState, scratch;
};

//============================================================================
// state structs (plain data; the plugin wrapper fills these from parameters)
//============================================================================
struct SlotState
{
    int   engine   = 0;          // index into EngineRegistry
    bool  bypass   = false;
    int   lane     = 0;          // 0..kNumLanes-1
    int   position = 0;          // series order within the lane
    float driveDb  = 0.0f;
    float mix      = 1.0f;       // 0 = slot input, 1 = engine output
    float levelDb  = 0.0f;
    float pan      = 0.0f;       // -1..+1
    float params[kMaxEngineParams] {};
};

struct RackState
{
    SlotState slots[kNumSlots];
    float laneLevelDb[kNumLanes] { 0.0f, 0.0f, 0.0f };
    bool  laneMute[kNumLanes]    { false, false, false };
    int   oversample = 1;        // 1, 2 or 4
    float inputDb    = 0.0f;
    float outputDb   = 0.0f;
    float dryWet     = 1.0f;
    // Auto gain: compensates slot drive on the wet path (1/drive) and scales the
    // parallel-lane sum by 1/(active lanes) so routing changes don't jump level.
    // Off by default to keep the offline CLI renders identical; the plugin
    // exposes it as a parameter (default on).
    bool  autoGain   = false;
};

//============================================================================
// the rack
//============================================================================
class ToposRack
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        baseSr = sampleRate;
        maxBlockSize = std::max (16, maxBlock);
        const int osMax = maxBlockSize * 4;

        for (int s = 0; s < kNumSlots; ++s)
        {
            slotEngines[s].clear();
            for (int e = 0; e < EngineRegistry::count(); ++e)
            {
                auto eng = EngineRegistry::create (e);
                // Prepared at 1x, matching RackState::oversample's default of 1.
                // setOversampleRate() re-prepares when the factor changes. Buffers
                // below are still sized for the worst case (4x) so that switching
                // never allocates on the audio thread.
                eng->prepare (sampleRate, osMax);
                slotEngines[s].push_back (std::move (eng));
            }
        }
        preparedOsRate = 1;

        for (int c = 0; c < 2; ++c)
        {
            hb1[c].prepare (osMax);
            hb2[c].prepare (osMax);
            osBuf[c].assign ((size_t) osMax + 8, 0.0f);
            os2Buf[c].assign ((size_t) osMax + 8, 0.0f);
            dryBuf[c].assign ((size_t) maxBlockSize + 8, 0.0f);
            for (int l = 0; l < kNumLanes; ++l)
            {
                laneBuf[l][c].assign ((size_t) osMax + 8, 0.0f);
                slotBuf[l][c].assign ((size_t) osMax + 8, 0.0f);
            }
        }
        reset();
    }

    void reset()
    {
        for (int s = 0; s < kNumSlots; ++s)
            for (auto& e : slotEngines[s]) e->reset();
        for (int c = 0; c < 2; ++c) { hb1[c].reset(); hb2[c].reset(); }
        laneNormPrev = -1.0f;   // snap (no ramp) on the next block
    }

    /** Re-prepare engines when the effective (oversampled) rate changes. Called
        from prepare-time or from the message thread — not realtime safe. */
    void setOversampleRate (int factor)
    {
        if (factor == preparedOsRate) return;
        preparedOsRate = factor;
        for (int s = 0; s < kNumSlots; ++s)
            for (auto& e : slotEngines[s]) { e->prepare (baseSr * factor, maxBlockSize * factor); e->reset(); }
    }

    void setState (const RackState& s) { state = s; }
    RackState& stateRef() { return state; }

    float tailSeconds() const
    {
        float t = 0.0f;
        for (int s = 0; s < kNumSlots; ++s)
        {
            const int e = clampIndex (state.slots[s].engine);
            if (e > 0 && ! state.slots[s].bypass)
                t = std::max (t, slotEngines[s][(size_t) e]->tailSeconds());
        }
        return t;
    }

    void process (float* L, float* R, int n)
    {
        if (n <= 0) return;
        n = std::min (n, maxBlockSize);

        const float inG  = dbToGain (state.inputDb);
        const float outG = dbToGain (state.outputDb);

        for (int i = 0; i < n; ++i)
        {
            L[i] = sanitise (L[i]) * inG;
            R[i] = sanitise (R[i]) * inG;
            dryBuf[0][(size_t) i] = L[i];
            dryBuf[1][(size_t) i] = R[i];
        }

        const int os = (state.oversample == 4 ? 4 : (state.oversample == 2 ? 2 : 1));
        const int nOs = n * os;

        float* wL = L;
        float* wR = R;
        if (os >= 2)
        {
            hb1[0].upsample (L, osBuf[0].data(), n);
            hb1[1].upsample (R, osBuf[1].data(), n);
            wL = osBuf[0].data(); wR = osBuf[1].data();
            if (os == 4)
            {
                hb2[0].upsample (osBuf[0].data(), os2Buf[0].data(), n * 2);
                hb2[1].upsample (osBuf[1].data(), os2Buf[1].data(), n * 2);
                wL = os2Buf[0].data(); wR = os2Buf[1].data();
            }
        }

        runLanes (wL, wR, nOs);

        if (os == 4)
        {
            hb2[0].downsample (os2Buf[0].data(), osBuf[0].data(), n * 2);
            hb2[1].downsample (os2Buf[1].data(), osBuf[1].data(), n * 2);
            hb1[0].downsample (osBuf[0].data(), L, n);
            hb1[1].downsample (osBuf[1].data(), R, n);
        }
        else if (os == 2)
        {
            hb1[0].downsample (osBuf[0].data(), L, n);
            hb1[1].downsample (osBuf[1].data(), R, n);
        }

        const float w = clampf (state.dryWet, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            L[i] = sanitise ((1.0f - w) * dryBuf[0][(size_t) i] + w * L[i]) * outG;
            R[i] = sanitise ((1.0f - w) * dryBuf[1][(size_t) i] + w * R[i]) * outG;
        }
    }

private:
    static inline int clampIndex (int e) { return (e < 0 || e >= EngineRegistry::count()) ? 0 : e; }
    static inline float dbToGain (float db) { return std::pow (10.0f, clampf (db, -60.0f, 24.0f) / 20.0f); }

    void runLanes (float* L, float* R, int n)
    {
        // build the per-lane ordered slot lists (cheap: kNumSlots is small)
        int  order[kNumLanes][kNumSlots];
        int  count[kNumLanes] = { 0 };
        bool active[kNumLanes] = { false };
        for (int l = 0; l < kNumLanes; ++l)
        {
            for (int s = 0; s < kNumSlots; ++s)
                if (clampLane (state.slots[s].lane) == l)
                {
                    order[l][count[l]++] = s;

                    // A lane only earns a place in the parallel sum if it actually
                    // does something. Without this, empty or fully bypassed lanes
                    // each pass the input through and the sum adds gain: three
                    // empty lanes would give +9.5 dB on an otherwise neutral rack.
                    if (state.slots[s].engine > 0 && ! state.slots[s].bypass)
                        active[l] = true;
                }

            // insertion sort by position, stable on slot index
            for (int i = 1; i < count[l]; ++i)
                for (int j = i; j > 0; --j)
                {
                    const int a = order[l][j - 1], b = order[l][j];
                    if (state.slots[b].position < state.slots[a].position)
                        std::swap (order[l][j - 1], order[l][j]);
                    else break;
                }
        }

        int numActive = 0;
        for (int l = 0; l < kNumLanes; ++l)
            if (! state.laneMute[l] && active[l]) ++numActive;

        // level-matched parallel sum, ramped across the block to avoid clicks
        const float normTarget = (state.autoGain && numActive > 1) ? 1.0f / (float) numActive : 1.0f;
        const float normStart  = (laneNormPrev < 0.0f) ? normTarget : laneNormPrev;
        laneNormPrev = normTarget;

        float accL, accR;
        bool anyLane = false;
        for (int l = 0; l < kNumLanes; ++l)
        {
            if (state.laneMute[l] || ! active[l]) continue;
            anyLane = true;

            std::copy (L, L + n, laneBuf[l][0].begin());
            std::copy (R, R + n, laneBuf[l][1].begin());

            for (int k = 0; k < count[l]; ++k)
                runSlot (order[l][k], laneBuf[l][0].data(), laneBuf[l][1].data(), n, l);
        }

        for (int i = 0; i < n; ++i)
        {
            accL = 0.0f; accR = 0.0f;
            for (int l = 0; l < kNumLanes; ++l)
            {
                if (state.laneMute[l] || ! active[l]) continue;
                const float g = dbToGain (state.laneLevelDb[l]);
                accL += laneBuf[l][0][(size_t) i] * g;
                accR += laneBuf[l][1][(size_t) i] * g;
            }
            if (! anyLane) { accL = L[i]; accR = R[i]; }   // nothing routed: pass through
            else
            {
                const float norm = normStart + (normTarget - normStart) * (float) (i + 1) / (float) n;
                accL *= norm; accR *= norm;
            }
            L[i] = accL; R[i] = accR;
        }
    }

    static inline int clampLane (int l) { return (l < 0 || l >= kNumLanes) ? 0 : l; }

    void runSlot (int s, float* L, float* R, int n, int lane)
    {
        const SlotState& st = state.slots[s];
        const int e = clampIndex (st.engine);
        if (e == 0 || st.bypass) return;

        FilterEngine* eng = slotEngines[s][(size_t) e].get();
        eng->setParams (st.params);

        const float dg = dbToGain (st.driveDb);
        float* tL = slotBuf[lane][0].data();
        float* tR = slotBuf[lane][1].data();

        for (int i = 0; i < n; ++i) { tL[i] = L[i] * dg; tR[i] = R[i] * dg; }

        eng->process (tL, tR, n);

        const float mix = clampf (st.mix, 0.0f, 1.0f);
        // auto gain: undo the drive on the wet path so Drive changes character,
        // not loudness, and the Mix knob blends two level-matched signals
        const float lv  = dbToGain (st.levelDb) * (state.autoGain ? 1.0f / dg : 1.0f);
        const float pan = clampf (st.pan, -1.0f, 1.0f);
        // stereo balance law: unity on both sides at centre, the far side fades
        // out as the slot is panned (the old sin/cos law cost 3 dB at centre)
        const float gl  = lv * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const float gr  = lv * (pan < 0.0f ? 1.0f + pan : 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const float wetL = sanitise (tL[i]) * gl;
            const float wetR = sanitise (tR[i]) * gr;
            L[i] = (1.0f - mix) * L[i] + mix * wetL;
            R[i] = (1.0f - mix) * R[i] + mix * wetR;
        }
    }

    RackState state;
    float laneNormPrev = -1.0f;
    std::vector<std::unique_ptr<FilterEngine>> slotEngines[kNumSlots];
    Halfband hb1[2], hb2[2];
    std::vector<float> osBuf[2], os2Buf[2], dryBuf[2];
    std::vector<float> laneBuf[kNumLanes][2], slotBuf[kNumLanes][2];
    double baseSr = 48000.0;
    int    maxBlockSize = 512;
    int    preparedOsRate = 1;
};

} // namespace topos
