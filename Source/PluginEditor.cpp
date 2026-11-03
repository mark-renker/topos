#include "PluginEditor.h"

using namespace topos;
namespace P = topos::params;

//============================================================================
namespace
{
    void styleName (juce::Label& l, const juce::String& text, float height = 13.0f,
                    juce::Justification j = juce::Justification::centredLeft)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (j);
        l.setFont (juce::Font (juce::FontOptions (height)));
        l.setInterceptsMouseClicks (false, false);
    }

    void styleLinear (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    }

    void styleRotary (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
    }

    juce::String formatSpecValue (const ParamSpec& spec, float value)
    {
        const int decimals = spec.stepped ? 0 : (std::abs (spec.max - spec.min) > 200.0f ? 1 : 3);
        juce::String t (value, decimals);
        const juce::String unit = juce::String::fromUTF8 (spec.unit);
        if (unit.isNotEmpty())
            t += " " + unit;
        return t;
    }
}

//============================================================================
// SlotPanel
//============================================================================
SlotPanel::SlotPanel (ToposAudioProcessor& p, int slotIndex)
    : proc (p), index (slotIndex)
{
    styleName (nameLabel, "SLOT " + juce::String (index + 1), 14.0f);
    addAndMakeVisible (nameLabel);

    styleName (blurbLabel, {}, 11.5f);
    blurbLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (blurbLabel);

    engineBox.addItemList (P::engineNames(), 1);
    addAndMakeVisible (engineBox);
    engineAtt = std::make_unique<ComboBoxAtt> (proc.apvts, P::slotId (index, "engine"), engineBox);

    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, P::slotId (index, "bypass"), bypassButton);

    laneBox.addItemList (P::laneNames(), 1);
    addAndMakeVisible (laneBox);
    laneAtt = std::make_unique<ComboBoxAtt> (proc.apvts, P::slotId (index, "lane"), laneBox);

    struct Row { juce::Slider& s; juce::Label& l; const char* id; const char* text; bool rotary; };
    const Row rows[] =
    {
        { posSlider,   posLabel,   "pos",      "Pos",   false },
        { driveSlider, driveLabel, "drive_db", "Drive", false },
        { mixSlider,   mixLabel,   "mix",      "Mix",   false },
        { levelSlider, levelLabel, "level_db", "Level", false },
        { panSlider,   panLabel,   "pan",      "Pan",   false },
    };

    for (const auto& r : rows)
    {
        styleLinear (r.s);
        addAndMakeVisible (r.s);
        styleName (r.l, r.text, 12.0f, juce::Justification::centredRight);
        addAndMakeVisible (r.l);
    }

    posAtt   = std::make_unique<SliderAtt> (proc.apvts, P::slotId (index, "pos"),      posSlider);
    driveAtt = std::make_unique<SliderAtt> (proc.apvts, P::slotId (index, "drive_db"), driveSlider);
    mixAtt   = std::make_unique<SliderAtt> (proc.apvts, P::slotId (index, "mix"),      mixSlider);
    levelAtt = std::make_unique<SliderAtt> (proc.apvts, P::slotId (index, "level_db"), levelSlider);
    panAtt   = std::make_unique<SliderAtt> (proc.apvts, P::slotId (index, "pan"),      panSlider);

    for (int i = 0; i < kMaxEngineParams; ++i)
    {
        styleRotary (paramSliders[i]);
        addAndMakeVisible (paramSliders[i]);
        styleName (paramLabels[i], {}, 11.5f, juce::Justification::centred);
        addAndMakeVisible (paramLabels[i]);

        paramAtt[i] = std::make_unique<SliderAtt> (proc.apvts, P::slotParamId (index, i),
                                                   paramSliders[i]);
    }

    refreshFromEngine (true);
}

void SlotPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (2);
    g.setColour (juce::Colour (0xff2a2f36));
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    g.setColour (juce::Colour (0xff454d58));
    g.drawRoundedRectangle (r.toFloat(), 5.0f, 1.0f);
}

void SlotPanel::refreshFromEngine (bool force)
{
    auto* param = proc.apvts.getRawParameterValue (P::slotId (index, "engine"));
    const int engineIndex = param == nullptr
                              ? 0
                              : juce::jlimit (0, EngineRegistry::count() - 1,
                                              (int) std::lround (param->load()));

    if (! force && engineIndex == shownEngine)
        return;

    shownEngine = engineIndex;

    const EngineDescriptor& desc = EngineRegistry::descriptor (engineIndex);
    const juce::String blurb = juce::String::fromUTF8 (desc.blurb);

    blurbLabel.setText (blurb, juce::dontSendNotification);
    engineBox.setTooltip (juce::String::fromUTF8 (desc.name) + juce::String::fromUTF8 (" \xe2\x80\x94 ") + blurb);

    const int np = juce::jlimit (0, kMaxEngineParams, desc.numParams);

    for (int i = 0; i < kMaxEngineParams; ++i)
    {
        const bool used = (i < np);

        paramSliders[i].setVisible (used);
        paramLabels[i].setVisible (used);

        if (! used)
        {
            paramLabels[i].setText ({}, juce::dontSendNotification);
            paramSliders[i].setTooltip ({});
            continue;
        }

        const ParamSpec spec = desc.params[i];   // static storage: safe to copy

        paramLabels[i].setText (juce::String::fromUTF8 (spec.label), juce::dontSendNotification);

        // the parameter itself stays a plain normalised 0..1 proxy; only the
        // displayed text is mapped through the engine's ParamSpec
        paramSliders[i].textFromValueFunction = [spec] (double norm)
        {
            return formatSpecValue (spec, P::denormalise (spec, (float) norm));
        };
        paramSliders[i].valueFromTextFunction = [spec] (const juce::String& text)
        {
            return (double) P::normalise (spec, text.getFloatValue());
        };
        paramSliders[i].updateText();

        paramSliders[i].setTooltip (juce::String::fromUTF8 (spec.label) + ": "
                                    + formatSpecValue (spec, spec.min) + " .. "
                                    + formatSpecValue (spec, spec.max)
                                    + (spec.stepped ? " (stepped)" : juce::String()));
    }

    resized();
}

void SlotPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    const int W = area.getWidth();

    // ---- row 1: engine / bypass / lane / position / blurb ------------------
    auto row1 = area.removeFromTop (24);
    nameLabel.setBounds (row1.removeFromLeft (56));
    engineBox.setBounds (row1.removeFromLeft (juce::jmin (190, row1.getWidth() / 3)).reduced (0, 1));
    row1.removeFromLeft (6);
    bypassButton.setBounds (row1.removeFromLeft (78));
    row1.removeFromLeft (4);
    laneBox.setBounds (row1.removeFromLeft (80).reduced (0, 1));
    row1.removeFromLeft (6);
    posLabel.setBounds (row1.removeFromLeft (30));
    posSlider.setBounds (row1.removeFromLeft (juce::jmin (120, juce::jmax (80, row1.getWidth() / 3))));
    row1.removeFromLeft (8);
    blurbLabel.setBounds (row1);

    area.removeFromTop (4);

    // ---- row 2: drive / mix / level / pan ---------------------------------
    auto row2 = area.removeFromTop (24);
    const int cell2 = juce::jmax (140, W / 4);
    juce::Slider* mixerSliders[] = { &driveSlider, &mixSlider, &levelSlider, &panSlider };
    juce::Label*  mixerLabels[]  = { &driveLabel,  &mixLabel,  &levelLabel,  &panLabel  };

    for (int i = 0; i < 4; ++i)
    {
        auto cell = row2.removeFromLeft (juce::jmin (cell2, row2.getWidth()));
        mixerLabels[i]->setBounds (cell.removeFromLeft (44));
        cell.removeFromLeft (2);
        mixerSliders[i]->setBounds (cell.reduced (2, 0));
    }

    area.removeFromTop (4);

    // ---- row 3: the eight generic engine parameters -----------------------
    auto row3 = area;
    const int visible = juce::jmax (1, [this]
    {
        int n = 0;
        for (int i = 0; i < kMaxEngineParams; ++i) if (paramSliders[i].isVisible()) ++n;
        return n;
    }());

    const int cols  = juce::jmax (1, juce::jmin (kMaxEngineParams, visible));
    const int cellW = juce::jmax (72, row3.getWidth() / cols);

    for (int i = 0; i < kMaxEngineParams; ++i)
    {
        if (! paramSliders[i].isVisible())
            continue;

        auto cell = row3.removeFromLeft (juce::jmin (cellW, juce::jmax (72, row3.getWidth())));
        paramLabels[i].setBounds (cell.removeFromTop (14));
        paramSliders[i].setBounds (cell.reduced (3, 0));
    }
}

//============================================================================
// SlotsHolder
//============================================================================
ToposAudioProcessorEditor::SlotsHolder::SlotsHolder (ToposAudioProcessor& p)
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        panels[(size_t) i] = std::make_unique<SlotPanel> (p, i);
        addAndMakeVisible (*panels[(size_t) i]);
    }
}

void ToposAudioProcessorEditor::SlotsHolder::resized()
{
    auto area = getLocalBounds().reduced (2, 4);
    for (auto& panel : panels)
        panel->setBounds (area.removeFromTop (SlotPanel::kPanelHeight));
}

void ToposAudioProcessorEditor::SlotsHolder::refreshAll()
{
    for (auto& panel : panels)
        panel->refreshFromEngine (false);
}

//============================================================================
// editor
//============================================================================
ToposAudioProcessorEditor::ToposAudioProcessorEditor (ToposAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), holder (p)
{
    styleName (titleLabel, juce::String::fromUTF8 ("TOPOS \xe2\x80\x94 Modular Filter Matrix"), 19.0f);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    styleName (vendorLabel, "Renky Denk Audio", 12.0f, juce::Justification::centredRight);
    addAndMakeVisible (vendorLabel);

    struct G { juce::Slider& s; juce::Label& l; const char* id; const char* text; };
    const G globals[] =
    {
        { inSlider,     inLabel,     P::kInDb,   "In"  },
        { outSlider,    outLabel,    P::kOutDb,  "Out" },
        { dryWetSlider, dryWetLabel, P::kDryWet, "Dry/Wet" },
    };

    for (const auto& g : globals)
    {
        styleLinear (g.s);
        addAndMakeVisible (g.s);
        styleName (g.l, g.text, 12.0f, juce::Justification::centredRight);
        addAndMakeVisible (g.l);
    }

    inAtt     = std::make_unique<SliderAtt> (proc.apvts, P::kInDb,   inSlider);
    outAtt    = std::make_unique<SliderAtt> (proc.apvts, P::kOutDb,  outSlider);
    dryWetAtt = std::make_unique<SliderAtt> (proc.apvts, P::kDryWet, dryWetSlider);

    styleName (osLabel, "Oversample", 12.0f, juce::Justification::centredRight);
    addAndMakeVisible (osLabel);
    osBox.addItemList (P::oversampleNames(), 1);
    osBox.setTooltip ("Oversampling wraps the whole rack; engines are re-prepared "
                      "on the message thread when this changes.");
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<ComboBoxAtt> (proc.apvts, P::kOversample, osBox);

    for (int l = 0; l < kNumLanes; ++l)
    {
        styleLinear (laneSliders[l]);
        addAndMakeVisible (laneSliders[l]);
        styleName (laneLabels[l], "Lane " + juce::String (l + 1), 12.0f,
                   juce::Justification::centredRight);
        addAndMakeVisible (laneLabels[l]);

        laneMutes[l].setButtonText ("Mute");
        addAndMakeVisible (laneMutes[l]);

        laneAtt[l]     = std::make_unique<SliderAtt> (proc.apvts, P::laneId (l, "level_db"),
                                                      laneSliders[l]);
        laneMuteAtt[l] = std::make_unique<ButtonAtt> (proc.apvts, P::laneId (l, "mute"),
                                                      laneMutes[l]);
    }

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (760, 480, 2400, 1800);
    setSize (1080, 800);

    startTimer (150);   // picks up engine changes from host automation too
}

ToposAudioProcessorEditor::~ToposAudioProcessorEditor()
{
    stopTimer();
}

void ToposAudioProcessorEditor::timerCallback()
{
    holder.refreshAll();
}

void ToposAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c2026));

    g.setColour (juce::Colour (0xff232830));
    g.fillRect (0, 0, getWidth(), 104);
    g.setColour (juce::Colour (0xff3a424d));
    g.drawHorizontalLine (104, 0.0f, (float) getWidth());
}

void ToposAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // ---- title ------------------------------------------------------------
    auto titleRow = area.removeFromTop (28).reduced (10, 2);
    vendorLabel.setBounds (titleRow.removeFromRight (140));
    titleLabel.setBounds (titleRow);

    // ---- global row -------------------------------------------------------
    auto globalRow = area.removeFromTop (28).reduced (10, 2);
    const int gCell = juce::jmax (150, globalRow.getWidth() / 4);

    struct Pair { juce::Label* l; juce::Component* c; int labelW; };
    const Pair pairs[] =
    {
        { &inLabel,     &inSlider,     34 },
        { &outLabel,    &outSlider,    34 },
        { &dryWetLabel, &dryWetSlider, 58 },
    };

    for (const auto& pr : pairs)
    {
        auto cell = globalRow.removeFromLeft (juce::jmin (gCell, globalRow.getWidth()));
        pr.l->setBounds (cell.removeFromLeft (pr.labelW));
        cell.removeFromLeft (2);
        pr.c->setBounds (cell.reduced (3, 0));
    }

    {
        auto cell = globalRow.removeFromLeft (juce::jmin (gCell, globalRow.getWidth()));
        osLabel.setBounds (cell.removeFromLeft (70));
        cell.removeFromLeft (4);
        osBox.setBounds (cell.removeFromLeft (juce::jmin (80, cell.getWidth())).reduced (0, 2));
    }

    // ---- lane strip -------------------------------------------------------
    auto laneRow = area.removeFromTop (40).reduced (10, 6);
    const int lCell = juce::jmax (200, laneRow.getWidth() / kNumLanes);

    for (int l = 0; l < kNumLanes; ++l)
    {
        auto cell = laneRow.removeFromLeft (juce::jmin (lCell, laneRow.getWidth()));
        laneLabels[l].setBounds (cell.removeFromLeft (48));
        cell.removeFromLeft (2);
        laneMutes[l].setBounds (cell.removeFromRight (62));
        laneSliders[l].setBounds (cell.reduced (3, 0));
    }

    area.removeFromTop (10);

    // ---- scrolled slot panels --------------------------------------------
    viewport.setBounds (area.reduced (8, 0));

    const int barW = viewport.isVerticalScrollBarShown()
                       ? viewport.getScrollBarThickness() : 0;

    holder.setSize (juce::jmax (740, viewport.getWidth() - barW), holder.preferredHeight());
}
