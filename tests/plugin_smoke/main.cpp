#include <juce_events/juce_events.h>
#include "PluginProcessor.h"
#include <cstdio>

static void setP (ToposAudioProcessor& p, const juce::String& id, float norm)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (norm);
    else std::printf ("MISSING PARAM %s\n", id.toRawUTF8());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    ToposAudioProcessor proc;
    std::printf ("params=%d\n", proc.getParameters().size());

    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    juce::Random rnd (7);
    juce::MidiBuffer midi;

    for (int engine = 0; engine < topos::EngineRegistry::count(); ++engine)
    {
        // put this engine in every slot, spread over lanes
        for (int s = 0; s < topos::kNumSlots; ++s)
        {
            setP (proc, topos::params::slotId (s, "engine"),
                  (float) engine / (float) juce::jmax (1, topos::EngineRegistry::count() - 1));
            setP (proc, topos::params::slotId (s, "lane"), (float) (s % 3) / 2.0f);
            for (int q = 0; q < topos::kMaxEngineParams; ++q)
                setP (proc, topos::params::slotParamId (s, q), rnd.nextFloat());
        }

        juce::AudioBuffer<float> buf (2, 256);
        double peak = 0.0; bool bad = false;
        for (int b = 0; b < 40; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    buf.setSample (c, i, 0.3f * (rnd.nextFloat() * 2.0f - 1.0f));
            proc.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) bad = true;
                    peak = juce::jmax (peak, (double) std::abs (v));
                }
        }
        std::printf ("engine %d (%s): peak=%.4f finite=%s tail=%.3f\n", engine,
                     topos::EngineRegistry::descriptor (engine).name, peak,
                     bad ? "NO" : "yes", proc.getTailLengthSeconds());
    }

    // oversample change (message-thread re-prepare) + mono layout
    setP (proc, topos::params::kOversample, 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    juce::AudioBuffer<float> b2 (2, 300);
    b2.clear(); for (int i = 0; i < 300; ++i) b2.setSample (0, i, 0.2f), b2.setSample (1, i, 0.2f);
    proc.processBlock (b2, midi);
    std::printf ("after 4x os: sample=%.4f finite=%d\n", b2.getSample (0, 10),
                 (int) std::isfinite (b2.getSample (0, 10)));

    proc.setPlayConfigDetails (1, 1, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> mono (1, 256);
    for (int i = 0; i < 256; ++i) mono.setSample (0, i, 0.25f * std::sin (0.05f * (float) i));
    proc.processBlock (mono, midi);
    std::printf ("mono ok sample=%.4f\n", mono.getSample (0, 30));

    juce::MemoryBlock mb; proc.getStateInformation (mb);
    const float before = proc.apvts.getRawParameterValue (topos::params::slotParamId (0, 0))->load();
    setP (proc, topos::params::slotParamId (0, 0), 0.123f);
    proc.setStateInformation (mb.getData(), (int) mb.getSize());
    const float after = proc.apvts.getRawParameterValue (topos::params::slotParamId (0, 0))->load();
    std::printf ("state roundtrip: %.4f -> %.4f (%s), bytes=%d\n", before, after,
                 std::abs (before - after) < 1.0e-4f ? "OK" : "FAIL", (int) mb.getSize());
    std::printf ("SMOKE DONE\n");
    return 0;
}
