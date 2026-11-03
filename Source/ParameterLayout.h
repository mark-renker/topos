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

/** normalised (0..1) proxy -> real engine value, honouring skew + stepping */
inline float denormalise (const topos::ParamSpec& spec, float norm)
{
    norm = topos::clampf (norm, 0.0f, 1.0f);
    const float skew = (spec.skew > 0.0f ? spec.skew : 1.0f);
    float v = spec.min + (spec.max - spec.min) * std::pow (norm, skew);
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
    return topos::clampf (std::pow (lin, 1.0f / skew), 0.0f, 1.0f);
}

//============================================================================
// layout
//============================================================================
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
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

    auto dbAttr = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto pcAttr = juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int)
                                                    { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    // ---- global -----------------------------------------------------------
    layout.add (std::make_unique<APF> (pid (kInDb),   "Input",   gainRange, 0.0f, dbAttr));
    layout.add (std::make_unique<APF> (pid (kOutDb),  "Output",  gainRange, 0.0f, dbAttr));
    layout.add (std::make_unique<APF> (pid (kDryWet), "Dry/Wet", unitRange, 1.0f, pcAttr));
    layout.add (std::make_unique<APC> (pid (kOversample), "Oversample", oversampleNames(), 0));

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
            layout.add (std::make_unique<APF> (pid (slotParamId (n, p)),
                                              pfx + "P" + juce::String (p + 1),
                                              unitRange, 0.5f));
    }

    return layout;
}

} // namespace topos::params
