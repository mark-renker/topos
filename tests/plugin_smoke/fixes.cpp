/*  fixes.cpp — regression checks for the knob-mapping and gain-staging fixes.

    1. skew follows the JUCE convention (skew < 1 -> more travel at the low end)
    2. selecting an engine loads that engine's ParamSpec defaults into p0..p7
    3. restoring a session does NOT overwrite restored knob values
    4. host parameter text shows real engine values ("800.0 Hz", not "0.50")
    5. centre pan is unity gain
    6. Auto Gain: drive is compensated on the wet path, parallel lanes are averaged

    Exit code 0 = all checks passed.
*/
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

static int failures = 0;

static void check (bool ok, const char* what, double got, double want)
{
    std::printf ("[%s] %-58s got %.4f  want %.4f\n", ok ? " ok " : "FAIL", what, got, want);
    if (! ok) ++failures;
}

static void setParam (ToposAudioProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

static float getPlain (ToposAudioProcessor& p, const juce::String& id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

static int engineIndex (const char* name)
{
    for (int i = 0; i < topos::EngineRegistry::count(); ++i)
        if (juce::String (topos::EngineRegistry::descriptor (i).name) == name)
            return i;
    return -1;
}

static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (350); }

/** RMS of the second half of a 1 kHz sine (amplitude 0.25) run through the plugin */
static double sineRms (ToposAudioProcessor& p)
{
    const double sr = 48000.0;
    const int block = 256, blocks = 160;
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double acc = 0.0; long count = 0; long t = 0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i, ++t)
        {
            const float x = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) t / sr);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        p.processBlock (buf, midi);
        if (b >= blocks / 2)
            for (int i = 0; i < block; ++i, ++count)
                acc += 0.5 * (buf.getSample (0, i) * buf.getSample (0, i) + buf.getSample (1, i) * buf.getSample (1, i));
    }
    return std::sqrt (acc / (double) count);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double inRms = 0.25 / std::sqrt (2.0);

    // ---- 1. skew --------------------------------------------------------
    {
        const topos::ParamSpec cutoff { "cutoff", "Cutoff", 20.0f, 18000.0f, 900.0f, 0.3f, "Hz" };
        const float mid = topos::params::denormalise (cutoff, 0.5f);
        const float want = 20.0f + 17980.0f * std::pow (0.5f, 1.0f / 0.3f);
        check (std::abs (mid - want) < 1.0f, "cutoff at knob 50% (~1.8 kHz, was ~14.6 kHz)", mid, want);
        const float rt = topos::params::denormalise (cutoff, topos::params::normalise (cutoff, 900.0f));
        check (std::abs (rt - 900.0f) < 0.5f, "normalise/denormalise round trip at 900 Hz", rt, 900.0);
    }

    const int route = engineIndex ("ROUTE"), disperse = engineIndex ("DISPERSE");
    if (route < 0 || disperse < 0) { std::printf ("engine names not found\n"); return 2; }

    // ---- 2 + 4. engine defaults + host text -----------------------------
    juce::MemoryBlock saved;
    {
        ToposAudioProcessor p;
        setParam (p, "slot0_engine", (float) route);
        pump();
        const auto& desc = topos::EngineRegistry::descriptor (route);
        for (int k = 0; k < desc.numParams; ++k)
        {
            const float v = topos::params::denormalise (desc.params[k],
                                                        getPlain (p, topos::params::slotParamId (0, k)));
            const juce::String what = juce::String ("ROUTE default: ") + desc.params[k].label;
            check (std::abs (v - desc.params[k].def) <= 0.01f * std::max (1.0f, std::abs (desc.params[k].def)),
                   what.toRawUTF8(), v, desc.params[k].def);
        }
        const auto text = p.apvts.getParameter ("slot0_p0")->getCurrentValueAsText();
        std::printf ("       host text for slot0_p0: \"%s\"\n", text.toRawUTF8());
        check (text.contains ("Hz"), "host shows real units for engine proxy", text.contains ("Hz"), 1);

        // tweak a knob, save the session
        setParam (p, "slot0_p1", 0.9f);
        p.getStateInformation (saved);
    }

    // ---- 3. session restore keeps knob values ----------------------------
    {
        ToposAudioProcessor p;
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        pump();
        check (std::abs (getPlain (p, "slot0_p1") - 0.9f) < 1e-4f, "restored knob survives engine-default sync",
               getPlain (p, "slot0_p1"), 0.9);
    }

    // ---- 5. centre pan unity (DISPERSE is an allpass chain) --------------
    {
        ToposAudioProcessor p;
        setParam (p, "slot0_engine", (float) disperse);
        setParam (p, "auto_gain", 0.0f);
        pump();
        const double r = sineRms (p);
        check (std::abs (juce::Decibels::gainToDecibels (r / inRms)) < 0.5, "centre pan level, dB (was -3 dB)",
               juce::Decibels::gainToDecibels (r / inRms), 0.0);
    }

    // ---- 6a. drive compensation ----------------------------------------
    for (int ag = 0; ag < 2; ++ag)
    {
        ToposAudioProcessor p;
        setParam (p, "slot0_engine", (float) disperse);
        setParam (p, "slot0_drive_db", 12.0f);
        setParam (p, "auto_gain", (float) ag);
        pump();
        const double db = juce::Decibels::gainToDecibels (sineRms (p) / inRms);
        check (ag ? std::abs (db) < 0.5 : std::abs (db - 12.0) < 0.5,
               ag ? "+12 dB drive, Auto Gain on  -> level change, dB"
                  : "+12 dB drive, Auto Gain off -> level change, dB", db, ag ? 0.0 : 12.0);
    }

    // ---- 6b. parallel lane averaging -----------------------------------
    for (int ag = 0; ag < 2; ++ag)
    {
        ToposAudioProcessor p;
        for (int s = 0; s < 3; ++s)
        {
            setParam (p, topos::params::slotId (s, "engine"), (float) disperse);
            setParam (p, topos::params::slotId (s, "lane"), (float) s);
            setParam (p, topos::params::slotId (s, "mix"), 0.0f);   // each lane passes its input
        }
        setParam (p, "auto_gain", (float) ag);
        pump();
        const double db = juce::Decibels::gainToDecibels (sineRms (p) / inRms);
        const double want = ag ? 0.0 : juce::Decibels::gainToDecibels (3.0);
        check (std::abs (db - want) < 0.5,
               ag ? "3 active lanes, Auto Gain on  -> level change, dB"
                  : "3 active lanes, Auto Gain off -> level change, dB", db, want);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
