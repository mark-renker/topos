/*  PluginEditor.h — TOPOS editor.

    The slot panels are engine agnostic: the eight generic parameter sliders are
    re-labelled and re-scaled at runtime from the selected engine's
    EngineDescriptor, and sliders beyond desc.numParams are hidden. Adding an
    engine to the registry therefore needs no editor change at all.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

//============================================================================
class SlotPanel final : public juce::Component
{
public:
    SlotPanel (ToposAudioProcessor& p, int slotIndex);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** re-reads the slot's engine choice and rebuilds labels/ranges if needed */
    void refreshFromEngine (bool force = false);

    static constexpr int kPanelHeight = 196;

private:
    using SliderAtt   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    ToposAudioProcessor& proc;
    const int index;

    juce::Label     nameLabel, blurbLabel;
    juce::ComboBox  engineBox, laneBox;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::Slider    posSlider, driveSlider, mixSlider, levelSlider, panSlider;
    juce::Label     posLabel, driveLabel, mixLabel, levelLabel, panLabel;

    juce::Slider paramSliders[topos::kMaxEngineParams];
    juce::Label  paramLabels[topos::kMaxEngineParams];

    std::unique_ptr<ComboBoxAtt> engineAtt, laneAtt;
    std::unique_ptr<ButtonAtt>   bypassAtt;
    std::unique_ptr<SliderAtt>   posAtt, driveAtt, mixAtt, levelAtt, panAtt;
    std::unique_ptr<SliderAtt>   paramAtt[topos::kMaxEngineParams];

    int shownEngine = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotPanel)
};

//============================================================================
class ToposAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit ToposAudioProcessorEditor (ToposAudioProcessor&);
    ~ToposAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** scrolled container holding the six slot panels */
    class SlotsHolder final : public juce::Component
    {
    public:
        SlotsHolder (ToposAudioProcessor& p);
        void resized() override;
        void refreshAll();
        int  preferredHeight() const { return topos::kNumSlots * SlotPanel::kPanelHeight + 8; }
    private:
        std::array<std::unique_ptr<SlotPanel>, (size_t) topos::kNumSlots> panels;
    };

    ToposAudioProcessor& proc;

    juce::TooltipWindow tooltips { this, 500 };

    juce::Label  titleLabel, vendorLabel;
    juce::Slider inSlider, outSlider, dryWetSlider;
    juce::Label  inLabel, outLabel, dryWetLabel, osLabel;
    juce::ComboBox osBox;

    juce::Slider       laneSliders[topos::kNumLanes];
    juce::Label        laneLabels[topos::kNumLanes];
    juce::ToggleButton laneMutes[topos::kNumLanes];

    std::unique_ptr<SliderAtt>   inAtt, outAtt, dryWetAtt;
    std::unique_ptr<ComboBoxAtt> osAtt;
    std::unique_ptr<SliderAtt>   laneAtt[topos::kNumLanes];
    std::unique_ptr<ButtonAtt>   laneMuteAtt[topos::kNumLanes];

    juce::Viewport viewport;
    SlotsHolder    holder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToposAudioProcessorEditor)
};
