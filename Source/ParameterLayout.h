/*  ParameterLayout.h — programmatic APVTS layout for TOPOS.

    EXPANSION CONTRACT (see dsp/ToposCore.h):
    Parameter IDs here are GENERIC and permanently stable. Slot engine
    parameters are eight normalised proxies (slotN_p0 .. slotN_p7) that the
    processor maps onto the CURRENT engine's ParamSpec range at block rate, and
    that the editor re-labels from the engine's EngineDescriptor at runtime.
    Adding an engine to EngineRegistry therefore creates NO new parameter IDs
    and cannot break a saved session.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/EngineRegistry.h"
#include "dsp/ToposCore.h"

namespace topos::params
{

//============================================================================
// stable id strings
//============================================================================
inline constexpr const char* kInDb      = "in_db";
inline constexpr const char* kOutDb     = "out_db";
inline constexpr const char* kDryWet    = "dry_wet";
inline constexpr const char* kOversample = "oversample";
inline constexpr const char* kAutoGain   = "auto_gain";

inline juce::String laneId (int lane, const char* suffix)
{
    return "lane" + juce::String (lane) + "_" + suffix;
}

inline juce::String slotId (int slot, const char* suffix)
{
    return "slot" + juce::String (slot) + "_" + suffix;
}

/** the eight generic, engine-agnostic normalised proxies */
inline juce::String slotParamId (int slot, int p)
{
    return "slot" + juce::String (slot) + "_p" + juce::String (p);
}

inline constexpr int kVersionHint = 1;

inline juce::ParameterID pid (const juce::String& id) { return { id, kVersionHint }; }

//============================================================================
// helpers
//============================================================================
inline juce::StringArray engineNames()
{
    juce::StringArray names;
    for (int i = 0; i < topos::EngineRegistry::count(); ++i)
        names.add (juce::String::fromUTF8 (topos::EngineRegistry::descriptor (i).name));
    return names;
}

inline juce::StringArray laneNames()
{
    juce::StringArray names;
    for (int l = 0; l < topos::kNumLanes; ++l)
        names.add ("Lane " + juce::String (l + 1));
    return names;
}

inline juce::StringArray oversampleNames() { return { "1x", "2x", "4x" }; }

/** normalised (0..1) proxy -> real engine value, honouring skew + stepping.
    Skew follows the JUCE NormalisableRange convention the engines were written
    for: skew < 1 gives MORE knob travel to the low end (Hz, ms, s), i.e.
    proportion = norm^(1/skew). (The original wrapper used norm^skew, which
    crammed e.g. 20 Hz..9 kHz into the bottom 10 % of the knob.) */
inline float denormalise (const topos::ParamSpec& spec, float norm)
{
    norm = topos::clampf (norm, 0.0f, 1.0f);
    const float skew = (spec.skew > 0.0f ? spec.skew : 1.0f);
    float v = spec.min + (spec.max - spec.min) * std::pow (norm, 1.0f / skew);
    if (spec.stepped)
        v = std::round (v);
    return topos::clampf (v, std::min (spec.min, spec.max), std::max (spec.min, spec.max));
}

/** real engine value -> normalised proxy (inverse of denormalise) */
inline float normalise (const topos::ParamSpec& spec, float value)
{
    const float span = spec.max - spec.min;
    if (std::abs (span) < 1.0e-12f) return 0.0f;
    const float lin  = topos::clampf ((value - spec.min) / span, 0.0f, 1.0f);
    const float skew = (spec.skew > 0.0f ? spec.skew : 1.0f);
    return topos::clampf (std::pow (lin, skew), 0.0f, 1.0f);
}

//============================================================================
// layout
//============================================================================
inline juce::String formatSpecValue (const topos::ParamSpec& spec, float value)
{
    const int decimals = spec.stepped ? 0 : (std::abs (spec.max - spec.min) > 200.0f ? 1 : 3);
    juce::String t (value, decimals);
    const juce::String unit = juce::String::fromUTF8 (spec.unit);
    if (unit.isNotEmpty())
        t += " " + unit;
    return t;
}

/** engineForSlot: per-slot engine index kept current by the processor, so the
    host's parameter display can show real engine values for the generic
    proxies (e.g. "1800.0 Hz" instead of "0.50"). May be null. */
inline juce::AudioProcessorValueTreeState::ParameterLayout
    createLayout (const std::atomic<int>* engineForSlot = nullptr)
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using APInt = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const juce::NormalisableRange<float> gainRange  { -24.0f, 24.0f, 0.01f };
    const juce::NormalisableRange<float> levelRange { -60.0f, 12.0f, 0.01f };
    const juce::NormalisableRange<float> unitRange  { 0.0f, 1.0f, 0.0001f };
    const juce::NormalisableRange<float> panRange   { -1.0f, 1.0f, 0.001f };
    // engine proxies are continuous: a 0.0001 step on a skewed range turned
    // defaults like 800 Hz into 799.9 Hz after the round trip
    const juce::NormalisableRange<float> proxyRange { 0.0f, 1.0f };

    // explicit formatting: the 0.01 step snaps 0 dB to -1e-6, which JUCE's
    // default formatter shows as "-0.00"
    auto dbAttr = juce::AudioParameterFloatAttributes()
                      .withLabel ("dB")
                      .withStringFromValueFunction ([] (float v, int)
                                                    { return juce::String (std::abs (v) < 0.005f ? 0.0f : v, 2); });
    auto pcAttr = juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int)
                                                    { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    // ---- global -----------------------------------------------------------
    layout.add (std::make_unique<APF> (pid (kInDb),   "Input",   gainRange, 0.0f, dbAttr));
    layout.add (std::make_unique<APF> (pid (kOutDb),  "Output",  gainRange, 0.0f, dbAttr));
    layout.add (std::make_unique<APF> (pid (kDryWet), "Dry/Wet", unitRange, 1.0f, pcAttr));
    layout.add (std::make_unique<APC> (pid (kOversample), "Oversample", oversampleNames(), 0));
    layout.add (std::make_unique<APB> (pid (kAutoGain),   "Auto Gain",  true));

    // ---- lanes ------------------------------------------------------------
    for (int l = 0; l < topos::kNumLanes; ++l)
    {
        layout.add (std::make_unique<APF> (pid (laneId (l, "level_db")),
                                          "Lane " + juce::String (l + 1) + " Level",
                                          levelRange, 0.0f, dbAttr));
        layout.add (std::make_unique<APB> (pid (laneId (l, "mute")),
                                          "Lane " + juce::String (l + 1) + " Mute", false));
    }

    // ---- slots ------------------------------------------------------------
    const auto engines = engineNames();
    const auto lanes   = laneNames();

    for (int n = 0; n < topos::kNumSlots; ++n)
    {
        const juce::String pfx = "Slot " + juce::String (n + 1) + " ";

        layout.add (std::make_unique<APC>   (pid (slotId (n, "engine")), pfx + "Engine", engines, 0));
        layout.add (std::make_unique<APB>   (pid (slotId (n, "bypass")), pfx + "Bypass", false));
        layout.add (std::make_unique<APC>   (pid (slotId (n, "lane")),   pfx + "Lane",   lanes, 0));
        layout.add (std::make_unique<APInt> (pid (slotId (n, "pos")),    pfx + "Position",
                                             0, topos::kNumSlots - 1, n));
        layout.add (std::make_unique<APF>   (pid (slotId (n, "drive_db")), pfx + "Drive",
                                             gainRange, 0.0f, dbAttr));
        layout.add (std::make_unique<APF>   (pid (slotId (n, "mix")),      pfx + "Mix",
                                             unitRange, 1.0f, pcAttr));
        layout.add (std::make_unique<APF>   (pid (slotId (n, "level_db")), pfx + "Level",
                                             levelRange, 0.0f, dbAttr));
        layout.add (std::make_unique<APF>   (pid (slotId (n, "pan")),      pfx + "Pan",
                                             panRange, 0.0f));

        // generic normalised engine-parameter proxies — never renamed, never reordered
        for (int p = 0; p < topos::kMaxEngineParams; ++p)
        {
            auto attr = juce::AudioParameterFloatAttributes();

            if (engineForSlot != nullptr)
            {
                const std::atomic<int>* eng = engineForSlot + n;

                attr = attr.withStringFromValueFunction ([eng, p] (float norm, int)
                {
                    const auto& desc = topos::EngineRegistry::descriptor (
                        juce::jlimit (0, topos::EngineRegistry::count() - 1, eng->load()));
                    if (p >= desc.numParams) return juce::String ("-");
                    return formatSpecValue (desc.params[p], denormalise (desc.params[p], norm));
                })
                .withValueFromStringFunction ([eng, p] (const juce::String& text)
                {
                    const auto& desc = topos::EngineRegistry::descriptor (
                        juce::jlimit (0, topos::EngineRegistry::count() - 1, eng->load()));
                    if (p >= desc.numParams) return 0.5f;
                    return normalise (desc.params[p], text.getFloatValue());
                });
            }

            layout.add (std::make_unique<APF> (pid (slotParamId (n, p)),
                                              pfx + "P" + juce::String (p + 1),
                                              proxyRange, 0.5f, attr));
        }
    }

    return layout;
}

} // namespace topos::params
