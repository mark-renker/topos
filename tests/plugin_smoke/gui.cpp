#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    ToposAudioProcessor proc;

    // vary engines so the panels show different parameter counts
    const char* ids[] = { "slot0_engine", "slot1_engine", "slot2_engine",
                          "slot3_engine", "slot4_engine", "slot5_engine" };
    for (int i = 0; i < 6; ++i)
        if (auto* p = proc.apvts.getParameter (ids[i]))
            p->setValueNotifyingHost ((float) i / 5.0f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    const int sizes[][2] = { { 1080, 800 }, { 760, 480 }, { 1600, 1000 } };
    juce::PNGImageFormat png;
    for (auto& sz : sizes)
    {
        ed->setSize (sz[0], sz[1]);
        ed->resized();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out ("/home/user/workspace/plugin_smoke/editor_" + juce::String (sz[0]) + "x" + juce::String (sz[1]) + ".png");
        out.deleteFile();
        if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
            png.writeImageToStream (img, *stream);
        std::printf ("snapshot %dx%d -> %s\n", img.getWidth(), img.getHeight(), out.getFullPathName().toRawUTF8());
    }
    return 0;
}
