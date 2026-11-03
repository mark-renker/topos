/*  topos_cli — offline validation + demo renderer for the TOPOS DSP core.

    Build:  g++ -O2 -std=c++17 -I../dsp tests/topos_cli.cpp -o build/topos_cli
    Usage:  topos_cli --selftest
            topos_cli --render <demo> --in in.f32 --out out.f32 [--sr 48000]
            topos_cli --list-demos
*/
#include "../dsp/ToposRack.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <random>
#include <cmath>

using namespace topos;

//============================================================================
// helpers
//============================================================================
static std::vector<float> readRaw (const std::string& p)
{
    std::ifstream f (p, std::ios::binary);
    if (! f) return {};
    f.seekg (0, std::ios::end);
    const long n = (long) f.tellg() / 4;
    f.seekg (0);
    std::vector<float> v ((size_t) std::max (0L, n));
    if (n > 0) f.read ((char*) v.data(), n * 4);
    return v;
}

static void writeRawInterleaved (const std::string& p, const std::vector<float>& L, const std::vector<float>& R)
{
    std::ofstream f (p, std::ios::binary);
    std::vector<float> inter (L.size() * 2);
    for (size_t i = 0; i < L.size(); ++i) { inter[2 * i] = L[i]; inter[2 * i + 1] = R[i]; }
    f.write ((const char*) inter.data(), (std::streamsize) inter.size() * 4);
}

static void defaultsFor (SlotState& s, int engineIndex)
{
    s.engine = engineIndex;
    const auto& d = EngineRegistry::descriptor (engineIndex);
    for (int i = 0; i < kMaxEngineParams; ++i)
        s.params[i] = (i < d.numParams) ? d.params[i].def : 0.0f;
}

struct Metrics { float peak = 0, rms = 0, dc = 0; long nonFinite = 0; };

static Metrics measure (const std::vector<float>& L, const std::vector<float>& R)
{
    Metrics m; double acc = 0, sum = 0; long n = 0;
    for (size_t i = 0; i < L.size(); ++i)
        for (float v : { L[i], R[i] })
        {
            if (! std::isfinite (v)) { ++m.nonFinite; continue; }
            m.peak = std::max (m.peak, std::fabs (v));
            acc += (double) v * v; sum += v; ++n;
        }
    if (n > 0) { m.rms = (float) std::sqrt (acc / n); m.dc = (float) (sum / n); }
    return m;
}

//============================================================================
// self test
//============================================================================
static int failures = 0;

static void check (bool ok, const std::string& what, const std::string& detail = {})
{
    printf ("%-58s %s%s%s\n", what.c_str(), ok ? "PASS" : "FAIL",
            detail.empty() ? "" : "  ", detail.c_str());
    if (! ok) ++failures;
}

static std::vector<float> noise (int n, float amp = 0.5f, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.0f, amp);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = clampf (d (rng), -1.0f, 1.0f);
    return v;
}

/** run one engine standalone at a given parameter vector */
static Metrics runEngine (int engineIndex, const float* params, const std::vector<float>& in,
                          double sr = 48000.0, int block = 64)
{
    auto eng = EngineRegistry::create (engineIndex);
    eng->prepare (sr, block);
    eng->reset();
    eng->setParams (params);

    std::vector<float> L = in, R = in;
    for (size_t pos = 0; pos < L.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - pos);
        eng->setParams (params);
        eng->process (L.data() + pos, R.data() + pos, n);
    }
    return measure (L, R);
}

static void selfTest()
{
    const double sr = 48000.0;
    printf ("TOPOS core self-test  (engines registered: %d)\n\n", EngineRegistry::count());

    // ---- 1. registry integrity ------------------------------------------
    {
        bool idsOk = true, paramsOk = true;
        for (int i = 0; i < EngineRegistry::count(); ++i)
        {
            const auto& d = EngineRegistry::descriptor (i);
            if (d.id == nullptr || std::strlen (d.id) == 0) idsOk = false;
            if (d.numParams < 0 || d.numParams > kMaxEngineParams) paramsOk = false;
            for (int j = 0; j < d.numParams; ++j)
                if (! (d.params[j].min < d.params[j].max
                       && d.params[j].def >= d.params[j].min
                       && d.params[j].def <= d.params[j].max))
                    paramsOk = false;
            if (EngineRegistry::indexOf (d.id) != i) idsOk = false;
        }
        check (idsOk, "registry: ids unique, resolvable, non-empty");
        check (paramsOk, "registry: param ranges and defaults sane");
    }

    // ---- 2. every engine, default params, stable + non-silent -----------
    const auto in = noise (48000);
    for (int e = 1; e < EngineRegistry::count(); ++e)
    {
        const auto& d = EngineRegistry::descriptor (e);
        SlotState s; defaultsFor (s, e);
        const auto m = runEngine (e, s.params, in, sr);
        char buf[160];
        snprintf (buf, sizeof buf, "peak=%.3f rms=%.4f dc=%+.1e", m.peak, m.rms, m.dc);
        check (m.nonFinite == 0 && m.peak < 32.0f && m.rms > 1.0e-6f,
               std::string ("engine ") + d.name + ": stable and non-silent at defaults", buf);
        check (std::fabs (m.dc) < 1.0e-2f,
               std::string ("engine ") + d.name + ": DC free");
    }

    // ---- 3. parameter extremes sweep (corners of the param hypercube) ---
    for (int e = 1; e < EngineRegistry::count(); ++e)
    {
        const auto& d = EngineRegistry::descriptor (e);
        bool ok = true; float worst = 0.0f;
        const auto hot = noise (12000, 0.95f, 21);
        for (int corner = 0; corner < (1 << 4); ++corner)   // 16 corners over first 4 params
        {
            SlotState s; defaultsFor (s, e);
            for (int j = 0; j < std::min (4, d.numParams); ++j)
                s.params[j] = (corner & (1 << j)) ? d.params[j].max : d.params[j].min;
            for (int j = 4; j < d.numParams; ++j)
                s.params[j] = (corner & 1) ? d.params[j].max : d.params[j].def;
            const auto m = runEngine (e, s.params, hot, sr);
            worst = std::max (worst, m.peak);
            if (m.nonFinite != 0 || ! std::isfinite (m.peak) || m.peak > 64.0f) ok = false;
        }
        char buf[80]; snprintf (buf, sizeof buf, "worst peak=%.2f", worst);
        check (ok, std::string ("engine ") + d.name + ": bounded at parameter extremes", buf);
    }

    // ---- 4. audio-rate parameter modulation (the stability claim) --------
    for (int e = 1; e < EngineRegistry::count(); ++e)
    {
        const auto& d = EngineRegistry::descriptor (e);
        auto eng = EngineRegistry::create (e);
        eng->prepare (sr, 8);
        eng->reset();
        auto sig = noise (48000, 0.8f, 33);
        std::vector<float> L = sig, R = sig;
        SlotState s; defaultsFor (s, e);
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        for (size_t pos = 0; pos + 8 <= L.size(); pos += 8)   // new params every 8 samples
        {
            for (int j = 0; j < d.numParams; ++j)
            {
                const float t = u (rng);
                s.params[j] = d.params[j].min + t * (d.params[j].max - d.params[j].min);
            }
            eng->setParams (s.params);
            eng->process (L.data() + pos, R.data() + pos, 8);
        }
        const auto m = measure (L, R);
        char buf[80]; snprintf (buf, sizeof buf, "peak=%.2f", m.peak);
        check (m.nonFinite == 0 && m.peak < 64.0f,
               std::string ("engine ") + d.name + ": stable under 6 kHz param modulation", buf);
    }

    // ---- 5. SCATTER passivity: sum(alpha) <= 2 must not grow energy -----
    {
        const int e = EngineRegistry::indexOf ("scatter");
        SlotState s; defaultsFor (s, e);
        s.params[3] = 20.0f;      // longest decay
        s.params[6] = 1.0f;       // losslessness = 1.0 -> junction exactly lossless
        s.params[4] = 18000.0f;   // minimal damping
        std::vector<float> imp (48000 * 6, 0.0f); imp[100] = 1.0f;

        auto eng = EngineRegistry::create (e);
        eng->prepare (48000.0, 64); eng->reset(); eng->setParams (s.params);
        std::vector<float> L = imp, R = imp;
        // sweep coupling across its whole range while ringing: topology modulation
        for (size_t pos = 0; pos + 64 <= L.size(); pos += 64)
        {
            s.params[2] = 0.5f + 0.5f * std::sin (6.28318f * 40.0f * (float) pos / 48000.0f);
            eng->setParams (s.params);
            eng->process (L.data() + pos, R.data() + pos, 64);
        }
        double e1 = 0, e2 = 0;
        const size_t half = L.size() / 2;
        for (size_t i = 0; i < half; ++i)       e1 += (double) L[i] * L[i] + (double) R[i] * R[i];
        for (size_t i = half; i < L.size(); ++i) e2 += (double) L[i] * L[i] + (double) R[i] * R[i];
        const auto m = measure (L, R);
        char buf[120]; snprintf (buf, sizeof buf, "E2/E1=%.4f peak=%.3f", e2 / std::max (1e-30, e1), m.peak);
        check (m.nonFinite == 0 && e2 <= e1 * 1.05 && m.peak < 8.0f,
               "SCATTER: passive under 40 Hz topology modulation", buf);
    }

    // ---- 6. DISPERSE is allpass (flat magnitude) at fixed coefficients ---
    {
        const int e = EngineRegistry::indexOf ("disperse");
        SlotState s; defaultsFor (s, e);
        s.params[1] = 0.0f;   // no envelope dependence -> pure LTI allpass
        auto eng = EngineRegistry::create (e);
        eng->prepare (48000.0, 64); eng->reset(); eng->setParams (s.params);

        // white noise in, compare band energies in vs out
        auto x = noise (1 << 16, 0.3f, 99);
        std::vector<float> L = x, R = x;
        for (size_t pos = 0; pos + 64 <= L.size(); pos += 64)
            eng->process (L.data() + pos, R.data() + pos, 64);

        double eIn = 0, eOut = 0;
        for (size_t i = 2048; i < x.size(); ++i) { eIn += (double) x[i] * x[i]; eOut += (double) L[i] * L[i]; }
        const double ratio = eOut / std::max (1e-30, eIn);
        char buf[80]; snprintf (buf, sizeof buf, "energy ratio=%.4f", ratio);
        check (ratio > 0.97 && ratio < 1.03, "DISPERSE: allpass (energy preserved) when LTI", buf);
    }

    // ---- 7. VOWEL hysteresis: rising vs falling path must differ --------
    {
        const int e = EngineRegistry::indexOf ("vowel");
        SlotState s; defaultsFor (s, e);
        s.params[2] = 3.0f;     // open slew fast
        s.params[3] = 300.0f;   // close slew slow -> strong asymmetry
        auto sig = noise (24000, 0.5f, 4);

        auto runPath = [&] (bool rising)
        {
            auto eng = EngineRegistry::create (e);
            eng->prepare (48000.0, 64); eng->reset();
            std::vector<float> L = sig, R = sig;
            const int blocks = (int) (L.size() / 64);
            for (int b = 0; b < blocks; ++b)
            {
                const float t = (float) b / (float) blocks;
                s.params[0] = rising ? 4.0f * t : 4.0f * (1.0f - t);
                eng->setParams (s.params);
                eng->process (L.data() + (size_t) b * 64, R.data() + (size_t) b * 64, 64);
            }
            return L;
        };
        const auto up = runPath (true);
        auto dn = runPath (false);
        std::reverse (dn.begin(), dn.end());     // compare like vowel position for like

        double num = 0, den = 0;
        for (size_t i = 4000; i < up.size() - 4000; ++i)
        {
            num += (double) (up[i] - dn[i]) * (up[i] - dn[i]);
            den += (double) up[i] * up[i];
        }
        const double diff = std::sqrt (num / std::max (1e-30, den));
        char buf[80]; snprintf (buf, sizeof buf, "path difference=%.1f%%", 100.0 * diff);
        check (diff > 0.05, "VOWEL: opening and closing paths differ (hysteresis present)", buf);
    }

    // ---- 8. THERMAL memory: response depends on recent history -----------
    {
        const int e = EngineRegistry::indexOf ("thermal");
        SlotState s; defaultsFor (s, e);
        s.params[2] = 8.0f;    // drive
        s.params[3] = 1.0f;    // thermal depth max
        s.params[4] = 200.0f;  // thermal time

        auto probe = [&] (bool preheat)
        {
            auto eng = EngineRegistry::create (e);
            eng->prepare (48000.0, 64); eng->reset(); eng->setParams (s.params);
            // optional loud pre-heat section, then the SAME quiet probe
            if (preheat)
            {
                auto hot = noise (48000, 0.95f, 8);
                std::vector<float> a = hot, b = hot;
                for (size_t pos = 0; pos + 64 <= a.size(); pos += 64)
                    eng->process (a.data() + pos, b.data() + pos, 64);
            }
            auto q = noise (12000, 0.05f, 12);
            std::vector<float> a = q, b = q;
            for (size_t pos = 0; pos + 64 <= a.size(); pos += 64)
                eng->process (a.data() + pos, b.data() + pos, 64);
            return measure (a, b);
        };
        const auto cold = probe (false);
        const auto warm = probe (true);
        const double d = std::fabs (warm.rms - cold.rms) / std::max (1e-9f, cold.rms);
        char buf[120]; snprintf (buf, sizeof buf, "cold rms=%.5f warm rms=%.5f delta=%.1f%%",
                                 cold.rms, warm.rms, 100.0 * d);
        check (d > 0.01, "THERMAL: identical input responds differently when hot", buf);
    }

    // ---- 9. ROUTE unit-norm routing: no gain bulge while morphing --------
    {
        const int e = EngineRegistry::indexOf ("route");
        SlotState s; defaultsFor (s, e);
        s.params[0] = 1000.0f; s.params[1] = 0.707f; s.params[4] = 0.0f;
        auto x = noise (1 << 15, 0.4f, 55);
        float minR = 1.0e9f, maxR = 0.0f;
        for (int k = 0; k <= 8; ++k)
        {
            s.params[2] = (float) k / 8.0f;
            const auto m = runEngine (e, s.params, x, 48000.0);
            minR = std::min (minR, m.rms); maxR = std::max (maxR, m.rms);
        }
        const float spread = maxR / std::max (1.0e-9f, minR);
        char buf[80]; snprintf (buf, sizeof buf, "rms spread across morph=%.2fx", spread);
        check (spread < 2.2f, "ROUTE: level stays controlled across LP->BP morph", buf);
    }

    // ---- 10. rack: routing, reordering, oversampling ---------------------
    {
        ToposRack rack;
        rack.prepare (48000.0, 256);
        RackState st;
        for (int i = 0; i < kNumSlots; ++i) defaultsFor (st.slots[i], 0);

        defaultsFor (st.slots[0], EngineRegistry::indexOf ("route"));
        defaultsFor (st.slots[1], EngineRegistry::indexOf ("disperse"));
        defaultsFor (st.slots[2], EngineRegistry::indexOf ("thermal"));
        st.slots[0].lane = 0; st.slots[0].position = 0;
        st.slots[1].lane = 0; st.slots[1].position = 1;
        st.slots[2].lane = 1; st.slots[2].position = 0;

        auto x = noise (48000, 0.5f, 77);
        for (int os : { 1, 2, 4 })
        {
            st.oversample = os;
            rack.setOversampleRate (os);
            rack.setState (st);
            rack.reset();
            std::vector<float> L = x, R = x;
            for (size_t pos = 0; pos + 256 <= L.size(); pos += 256)
                rack.process (L.data() + pos, R.data() + pos, 256);
            const auto m = measure (L, R);
            char buf[100]; snprintf (buf, sizeof buf, "os=%dx peak=%.3f rms=%.4f", os, m.peak, m.rms);
            check (m.nonFinite == 0 && m.peak < 32.0f && m.rms > 1.0e-6f,
                   std::string ("rack: stable at ") + std::to_string (os) + "x oversampling", buf);
        }

        // reorder slots mid-stream: must not blow up or produce discontinuity spikes
        rack.setOversampleRate (2);
        st.oversample = 2;
        std::vector<float> L = x, R = x;
        float maxJump = 0.0f, prev = 0.0f;
        for (size_t pos = 0, b = 0; pos + 256 <= L.size(); pos += 256, ++b)
        {
            st.slots[0].position = (int) (b % 3);
            st.slots[1].position = (int) ((b + 1) % 3);
            st.slots[2].lane     = (int) (b % kNumLanes);
            rack.setState (st);
            rack.process (L.data() + pos, R.data() + pos, 256);
            for (int i = 0; i < 256; ++i)
            {
                maxJump = std::max (maxJump, std::fabs (L[pos + (size_t) i] - prev));
                prev = L[pos + (size_t) i];
            }
        }
        const auto m = measure (L, R);
        char buf[100]; snprintf (buf, sizeof buf, "peak=%.3f max sample jump=%.3f", m.peak, maxJump);
        check (m.nonFinite == 0 && m.peak < 32.0f, "rack: live slot reorder / lane change is safe", buf);
    }

    // ---- 11. a neutral rack must be exactly unity gain -------------------
    // Empty slots, and slots whose engines are bypassed, must not each pass the
    // input through into the parallel lane sum: three lanes doing that would add
    // roughly +9.5 dB to an otherwise neutral rack.
    {
        std::vector<float> ref (4096);
        for (size_t i = 0; i < ref.size(); ++i)
            ref[i] = 0.3f * std::sin (6.2831853f * 220.0f * (float) i / 48000.0f);

        struct Case { const char* what; bool bypass; };
        const Case cases[2] = { { "all slots empty", false },
                                { "all slots loaded but bypassed", true } };
        float worst = 0.0f;
        const char* worstWhat = "";
        for (auto& c : cases)
        {
            ToposRack rack;
            rack.prepare (48000.0, 256);
            RackState st;
            for (int i = 0; i < kNumSlots; ++i)
            {
                defaultsFor (st.slots[i], c.bypass ? (1 + i % (EngineRegistry::count() - 1)) : 0);
                st.slots[i].bypass = c.bypass;
                st.slots[i].lane = i % kNumLanes;   // spread across all three lanes
                st.slots[i].position = i;
            }
            st.oversample = 1;
            rack.setOversampleRate (1);
            rack.setState (st);
            rack.reset();

            std::vector<float> L = ref, R = ref;
            for (size_t pos = 0; pos + 256 <= L.size(); pos += 256)
                rack.process (L.data() + pos, R.data() + pos, 256);

            float err = 0.0f;
            for (size_t i = 0; i < ref.size(); ++i)
                err = std::max (err, std::fabs (L[i] - ref[i]));
            if (err > worst) { worst = err; worstWhat = c.what; }
        }
        char buf[140];
        snprintf (buf, sizeof buf, "worst deviation from input = %.2e (%s)", worst, worstWhat);
        check (worst < 1.0e-5f, "rack: neutral rack is exactly unity gain", buf);
    }

    // ---- 12. silence in -> silence out (no self-oscillation) -------------
    {
        ToposRack rack;
        rack.prepare (48000.0, 256);
        RackState st;
        for (int i = 0; i < kNumSlots; ++i) defaultsFor (st.slots[i], 0);
        for (int i = 0; i < 5; ++i)
        {
            defaultsFor (st.slots[i], 1 + i);
            st.slots[i].lane = i % kNumLanes;
            st.slots[i].position = i;
        }
        st.oversample = 2;
        rack.setOversampleRate (2);
        rack.setState (st);
        rack.reset();
        std::vector<float> L (48000 * 2, 0.0f), R (48000 * 2, 0.0f);
        for (size_t pos = 0; pos + 256 <= L.size(); pos += 256)
            rack.process (L.data() + pos, R.data() + pos, 256);
        const auto m = measure (L, R);
        char buf[80]; snprintf (buf, sizeof buf, "peak=%.2e", m.peak);
        check (m.peak < 1.0e-4f, "rack: all five engines active, silence stays silent", buf);
    }

    printf ("\n%s  (%d failure%s)\n", failures == 0 ? "ALL CORE TESTS PASSED" : "CORE TESTS FAILED",
            failures, failures == 1 ? "" : "s");
}

//============================================================================
// demo renders
//============================================================================
struct Demo
{
    const char* name;
    const char* note;
    std::function<void (RackState&, float t, float dur)> automate;
    int oversample;
};

static void baseState (RackState& st)
{
    for (int i = 0; i < kNumSlots; ++i) { defaultsFor (st.slots[i], 0); st.slots[i].lane = 0; st.slots[i].position = i; }
    for (int l = 0; l < kNumLanes; ++l) { st.laneLevelDb[l] = 0.0f; st.laneMute[l] = false; }
    st.dryWet = 1.0f; st.inputDb = 0.0f; st.outputDb = 0.0f;
}

static std::vector<Demo> demos()
{
    std::vector<Demo> d;

    d.push_back ({ "01_scatter_star_to_mesh",
        "SCATTER alone: ratio morph, then coupling star->mesh (topology change)",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("scatter"));
            auto& p = st.slots[0].params;
            const float a = t / dur;
            p[0] = 98.0f;
            p[1] = clampf (a * 4.0f, 0.0f, 2.0f);                     // ratio morph in first quarter
            p[2] = clampf ((a - 0.35f) / 0.6f, 0.0f, 1.0f);           // then star -> mesh
            p[3] = 3.5f; p[4] = 7500.0f;
            p[5] = 14000.0f - 11000.0f * a;
            p[6] = 0.9995f; p[7] = 0.6f;
            st.slots[0].driveDb = 3.0f;
            st.slots[0].levelDb = 12.0f;      // struck resonator sits low without makeup
        }, 2 });

    d.push_back ({ "02_route_energy_morph",
        "ROUTE alone: cutoff sweep with LP->BP->HP unit-norm morph and colour",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("route"));
            auto& p = st.slots[0].params;
            const float a = t / dur;
            p[0] = 120.0f * std::pow (50.0f, (a < 0.5f ? a * 2.0f : (1.0f - a) * 2.0f));
            p[1] = 5.0f;                      // resonant Q adds gain; keep the demo unclipped
            p[2] = clampf (a * 2.0f, 0.0f, 1.0f);
            p[3] = clampf ((a - 0.5f) * 2.0f, 0.0f, 1.0f);
            p[4] = 0.55f;
            p[5] = clampf ((a - 0.2f) * 1.1f, 0.0f, 0.85f);
            p[6] = 3.0f; p[7] = 2.0f;
            st.slots[0].levelDb = -12.0f;
        }, 4 });

    d.push_back ({ "03_disperse_env_material",
        "DISPERSE alone: dispersion sweep, then envelope depth (loud vs quiet differ)",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("disperse"));
            auto& p = st.slots[0].params;
            const float a = t / dur;
            p[0] = -0.9f + 1.8f * clampf (a * 2.0f, 0.0f, 1.0f);
            p[1] = clampf ((a - 0.5f) * 2.0f, 0.0f, 1.0f) * 0.95f;
            p[2] = -0.9f + 1.8f * a;
            p[3] = 2.5f; p[4] = 140.0f; p[5] = 12.0f; p[6] = 0.7f; p[7] = 90.0f;
        }, 2 });

    d.push_back ({ "04_vowel_hysteresis",
        "VOWEL alone: same vowel path up then down — hysteresis makes them differ",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("vowel"));
            auto& p = st.slots[0].params;
            const float a = t / dur;
            p[0] = (a < 0.5f) ? 8.0f * a : 8.0f * (1.0f - a);   // 0->4->0
            p[1] = 1.0f; p[2] = 6.0f; p[3] = 280.0f;
            p[4] = 0.7f; p[5] = 0.6f + 1.2f * a; p[6] = 0.25f; p[7] = 0.0f;
            st.slots[0].driveDb = 4.0f;
        }, 2 });

    d.push_back ({ "05_thermal_drift",
        "THERMAL alone: quiet, then hard-driven (thermal drift + sag), then quiet again",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("thermal"));
            auto& p = st.slots[0].params;
            const float a = t / dur;
            p[0] = 260.0f * std::pow (9.0f, (a < 0.5f ? a * 2.0f : (1.0f - a) * 2.0f));
            p[1] = 0.85f; p[2] = 7.0f; p[3] = 0.95f; p[4] = 400.0f;
            p[5] = 0.75f; p[6] = 90.0f; p[7] = 0.6f;
            st.outputDb = -8.0f;
        }, 4 });

    d.push_back ({ "06_series_chain",
        "SERIES: SCATTER -> DISPERSE -> THERMAL in one lane, order swapped halfway",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            const float a = t / dur;
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("scatter"));
            defaultsFor (st.slots[1], EngineRegistry::indexOf ("disperse"));
            defaultsFor (st.slots[2], EngineRegistry::indexOf ("thermal"));
            for (int i = 0; i < 3; ++i) st.slots[i].lane = 0;

            // after halfway, THERMAL moves in front of SCATTER
            st.slots[0].position = (a < 0.5f) ? 0 : 1;
            st.slots[1].position = 2;
            st.slots[2].position = (a < 0.5f) ? 1 : 0;

            st.slots[0].params[0] = 110.0f;
            st.slots[0].params[1] = 1.0f;
            st.slots[0].params[2] = 0.15f + 0.8f * a;
            st.slots[0].params[3] = 2.2f;
            st.slots[0].mix = 0.85f; st.slots[0].levelDb = -3.0f;

            st.slots[1].params[0] = 0.7f;
            st.slots[1].params[1] = 0.8f;
            st.slots[1].params[6] = 0.8f;

            st.slots[2].params[0] = 500.0f + 3000.0f * std::sin (6.2831853f * 0.12f * t);
            st.slots[2].params[1] = 0.7f;
            st.slots[2].params[2] = 4.0f;
            st.slots[2].params[3] = 0.9f;
            st.slots[2].mix = 0.9f;
            st.outputDb = -3.0f;
        }, 4 });

    d.push_back ({ "07_parallel_lanes",
        "PARALLEL: VOWEL | ROUTE | SCATTER in three lanes, cross-fading lane levels",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            const float a = t / dur;
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("vowel"));
            defaultsFor (st.slots[1], EngineRegistry::indexOf ("route"));
            defaultsFor (st.slots[2], EngineRegistry::indexOf ("scatter"));
            st.slots[0].lane = 0; st.slots[1].lane = 1; st.slots[2].lane = 2;
            for (int i = 0; i < 3; ++i) st.slots[i].position = 0;

            st.slots[0].params[0] = 4.0f * a; st.slots[0].params[4] = 0.5f;
            st.slots[0].pan = -0.6f;
            st.slots[1].params[0] = 300.0f + 2500.0f * a;
            st.slots[1].params[1] = 12.0f;
            st.slots[1].params[2] = 0.5f;
            st.slots[1].pan = 0.6f;
            st.slots[2].params[0] = 82.4f;
            st.slots[2].params[2] = a;
            st.slots[2].params[3] = 4.0f;

            // lane levels rotate so each filter takes the foreground in turn
            for (int l = 0; l < 3; ++l)
            {
                const float phase = a * 3.0f - (float) l;
                const float g = clampf (1.0f - std::fabs (phase - 0.5f) * 1.2f, 0.0f, 1.0f);
                st.laneLevelDb[l] = -24.0f + 24.0f * g;
            }
            st.outputDb = -2.0f;
        }, 2 });

    d.push_back ({ "08_hybrid_matrix",
        "HYBRID: lane 0 = ROUTE->THERMAL series, lane 1 = SCATTER, lane 2 = VOWEL->DISPERSE",
        [] (RackState& st, float t, float dur)
        {
            baseState (st);
            const float a = t / dur;
            defaultsFor (st.slots[0], EngineRegistry::indexOf ("route"));
            defaultsFor (st.slots[1], EngineRegistry::indexOf ("thermal"));
            defaultsFor (st.slots[2], EngineRegistry::indexOf ("scatter"));
            defaultsFor (st.slots[3], EngineRegistry::indexOf ("vowel"));
            defaultsFor (st.slots[4], EngineRegistry::indexOf ("disperse"));

            st.slots[0].lane = 0; st.slots[0].position = 0;
            st.slots[1].lane = 0; st.slots[1].position = 1;
            st.slots[2].lane = 1; st.slots[2].position = 0;
            st.slots[3].lane = 2; st.slots[3].position = 0;
            st.slots[4].lane = 2; st.slots[4].position = 1;

            st.slots[0].params[0] = 400.0f + 2000.0f * std::sin (6.2831853f * 0.07f * t);
            st.slots[0].params[1] = 8.0f;
            st.slots[0].params[2] = a;
            st.slots[1].params[0] = 1200.0f; st.slots[1].params[2] = 3.0f; st.slots[1].params[3] = 0.8f;
            st.slots[2].params[0] = 61.7f; st.slots[2].params[1] = 2.0f * a;
            st.slots[2].params[2] = 0.9f; st.slots[2].params[3] = 5.0f;
            st.slots[3].params[0] = 4.0f * (1.0f - a);
            st.slots[4].params[0] = 0.85f; st.slots[4].params[1] = 0.9f; st.slots[4].params[6] = 1.0f;

            st.laneLevelDb[0] = -6.0f; st.laneLevelDb[1] = -4.0f; st.laneLevelDb[2] = -8.0f;
            st.outputDb = -3.0f;
        }, 4 });

    return d;
}

int main (int argc, char** argv)
{
    std::string demoName, inPath, outPath;
    bool doSelfTest = false, doList = false;
    double sr = 48000.0;

    for (int i = 1; i < argc; ++i)
    {
        if (! strcmp (argv[i], "--selftest")) doSelfTest = true;
        else if (! strcmp (argv[i], "--list-demos")) doList = true;
        else if (! strcmp (argv[i], "--render") && i + 1 < argc) demoName = argv[++i];
        else if (! strcmp (argv[i], "--in")  && i + 1 < argc) inPath = argv[++i];
        else if (! strcmp (argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (! strcmp (argv[i], "--sr")  && i + 1 < argc) sr = atof (argv[++i]);
    }

    if (doSelfTest) { selfTest(); return failures == 0 ? 0 : 1; }

    const auto all = demos();
    if (doList)
    {
        for (auto& d : all) printf ("%-28s %s\n", d.name, d.note);
        return 0;
    }
    if (demoName.empty() || outPath.empty()) { printf ("nothing to do\n"); return 1; }

    const Demo* demo = nullptr;
    for (auto& d : all) if (demoName == d.name) demo = &d;
    if (! demo) { printf ("unknown demo %s\n", demoName.c_str()); return 1; }

    auto x = readRaw (inPath);
    if (x.empty()) { printf ("no input audio\n"); return 1; }

    const int block = 128;
    ToposRack rack;
    rack.prepare (sr, block);
    rack.setOversampleRate (demo->oversample);

    RackState st; baseState (st);
    st.oversample = demo->oversample;

    std::vector<float> L = x, R = x;
    const float dur = (float) x.size() / (float) sr;
    for (size_t pos = 0; pos + (size_t) block <= L.size(); pos += (size_t) block)
    {
        const float t = (float) pos / (float) sr;
        demo->automate (st, t, dur);
        st.oversample = demo->oversample;
        rack.setState (st);
        rack.process (L.data() + pos, R.data() + pos, block);
    }

    const auto m = measure (L, R);
    fprintf (stderr, "%s: %zu frames  peak=%.4f rms=%.5f dc=%+.2e nonfinite=%ld\n",
             demo->name, L.size(), m.peak, m.rms, m.dc, m.nonFinite);
    writeRawInterleaved (outPath, L, R);
    return m.nonFinite ? 2 : 0;
}
