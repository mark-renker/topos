/*  PluginProcessor.h — TOPOS host wrapper.

    Owns the APVTS and one ToposRack. All host/format code lives here; the DSP
    core in dsp/ stays framework free.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "ParameterLayout.h"
#include "dsp/ToposRack.h"

class ToposAudioProcessor final : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    ToposAudioProcessor();
    ~ToposAudioProcessor() override;

    //==== lifecycle =========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;   // keep the double-precision overload visible

    //==== editor ============================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==== info ==============================================================
    const juce::String getName() const override { return "TOPOS"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    //==== state =============================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // Declared before apvts: the layout's display lambdas capture engineForSlot.
    // engineForSlot   — current engine per slot (any thread may read)
    // proxiesForEngine — engine the slot's p0..p7 values currently belong to;
    //                    while it differs from the selected engine the audio
    //                    thread uses the engine's ParamSpec defaults instead
    std::atomic<int> engineForSlot[topos::kNumSlots] {};
    std::atomic<int> proxiesForEngine[topos::kNumSlots] {};

public:
    juce::AudioProcessorValueTreeState apvts;

private:
    void syncEngineDefaults();               // message thread
    static BusesProperties defaultBuses();

    //==== parameter pointer cache (no lookups in processBlock) ==============
    struct SlotPtrs
    {
        std::atomic<float>* engine   = nullptr;
        std::atomic<float>* bypass   = nullptr;
        std::atomic<float>* lane     = nullptr;
        std::atomic<float>* pos      = nullptr;
        std::atomic<float>* driveDb  = nullptr;
        std::atomic<float>* mix      = nullptr;
        std::atomic<float>* levelDb  = nullptr;
        std::atomic<float>* pan      = nullptr;
        std::atomic<float>* p[topos::kMaxEngineParams] {};
    };

    void cacheParameterPointers();
    void updateRackState();                 // realtime safe, no allocation
    void timerCallback() override;           // message thread: oversample re-prepare

    static int osFactorFromChoice (int choice) { return choice >= 2 ? 4 : (choice == 1 ? 2 : 1); }

    topos::ToposRack rack;
    topos::RackState state;                  // member: filled in place each block

    SlotPtrs slots[topos::kNumSlots];
    std::atomic<float>* pInDb       = nullptr;
    std::atomic<float>* pOutDb      = nullptr;
    std::atomic<float>* pDryWet     = nullptr;
    std::atomic<float>* pOversample = nullptr;
    std::atomic<float>* pAutoGain   = nullptr;
    std::atomic<float>* pLaneLevel[topos::kNumLanes] {};
    std::atomic<float>* pLaneMute[topos::kNumLanes] {};

    juce::AudioBuffer<float> scratch;        // stereo work buffer (mono hosting)
    int  preparedBlock = 512;
    std::atomic<int> appliedOs { 1 };        // oversample factor the engines are prepared for
    std::atomic<float> tailSecondsCached { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToposAudioProcessor)
};
