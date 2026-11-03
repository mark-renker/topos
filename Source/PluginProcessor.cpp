#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace topos;

//============================================================================
juce::AudioProcessor::BusesProperties ToposAudioProcessor::defaultBuses()
{
    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

ToposAudioProcessor::ToposAudioProcessor()
    : juce::AudioProcessor (defaultBuses()),
      apvts (*this, nullptr, "TOPOS", topos::params::createLayout (engineForSlot))
{
    cacheParameterPointers();
    startTimer (100);      // watches the oversample choice (re-prepare is not RT safe)
}

ToposAudioProcessor::~ToposAudioProcessor()
{
    stopTimer();
}

void ToposAudioProcessor::cacheParameterPointers()
{
    using namespace topos::params;

    pInDb       = apvts.getRawParameterValue (kInDb);
    pOutDb      = apvts.getRawParameterValue (kOutDb);
    pDryWet     = apvts.getRawParameterValue (kDryWet);
    pOversample = apvts.getRawParameterValue (kOversample);
    pAutoGain   = apvts.getRawParameterValue (kAutoGain);

    for (int l = 0; l < kNumLanes; ++l)
    {
        pLaneLevel[l] = apvts.getRawParameterValue (laneId (l, "level_db"));
        pLaneMute[l]  = apvts.getRawParameterValue (laneId (l, "mute"));
    }

    for (int n = 0; n < kNumSlots; ++n)
    {
        auto& s = slots[n];
        s.engine  = apvts.getRawParameterValue (slotId (n, "engine"));
        s.bypass  = apvts.getRawParameterValue (slotId (n, "bypass"));
        s.lane    = apvts.getRawParameterValue (slotId (n, "lane"));
        s.pos     = apvts.getRawParameterValue (slotId (n, "pos"));
        s.driveDb = apvts.getRawParameterValue (slotId (n, "drive_db"));
        s.mix     = apvts.getRawParameterValue (slotId (n, "mix"));
        s.levelDb = apvts.getRawParameterValue (slotId (n, "level_db"));
        s.pan     = apvts.getRawParameterValue (slotId (n, "pan"));

        for (int p = 0; p < kMaxEngineParams; ++p)
            s.p[p] = apvts.getRawParameterValue (slotParamId (n, p));
    }

    jassert (pInDb != nullptr && pOversample != nullptr);
}

//============================================================================
// lifecycle
//============================================================================
void ToposAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlock = juce::jmax (16, samplesPerBlock);

    rack.prepare (sampleRate, preparedBlock);

    const int os = osFactorFromChoice ((int) pOversample->load());
    rack.setOversampleRate (os);
    appliedOs.store (os);

    scratch.setSize (2, preparedBlock, false, true, true);
    scratch.clear();

    rack.reset();
    setLatencySamples (0);

    updateRackState();
    rack.setState (state);
    tailSecondsCached.store (rack.tailSeconds());
}

void ToposAudioProcessor::releaseResources()
{
}

bool ToposAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

double ToposAudioProcessor::getTailLengthSeconds() const
{
    return (double) tailSecondsCached.load();
}

//============================================================================
// parameter -> RackState (realtime safe: no allocation, no locking)
//============================================================================
void ToposAudioProcessor::updateRackState()
{
    state.inputDb   = pInDb->load();
    state.outputDb  = pOutDb->load();
    state.dryWet    = pDryWet->load();
    state.oversample = appliedOs.load();
    state.autoGain  = pAutoGain->load() > 0.5f;

    for (int l = 0; l < kNumLanes; ++l)
    {
        state.laneLevelDb[l] = pLaneLevel[l]->load();
        state.laneMute[l]    = pLaneMute[l]->load() > 0.5f;
    }

    for (int n = 0; n < kNumSlots; ++n)
    {
        const auto& src = slots[n];
        auto& dst = state.slots[n];

        const int engineIndex = juce::jlimit (0, EngineRegistry::count() - 1,
                                             (int) std::lround (src.engine->load()));

        dst.engine   = engineIndex;
        dst.bypass   = src.bypass->load() > 0.5f;
        dst.lane     = juce::jlimit (0, kNumLanes - 1, (int) std::lround (src.lane->load()));
        dst.position = juce::jlimit (0, kNumSlots - 1, (int) std::lround (src.pos->load()));
        dst.driveDb  = src.driveDb->load();
        dst.mix      = src.mix->load();
        dst.levelDb  = src.levelDb->load();
        dst.pan      = src.pan->load();

        engineForSlot[n].store (engineIndex, std::memory_order_relaxed);

        // normalised proxies -> real engine values via the current descriptor.
        // Right after an engine change (before the message thread has reset the
        // proxies) the old engine's knob positions are meaningless for the new
        // engine, so use its defaults until syncEngineDefaults() catches up.
        const EngineDescriptor& desc = EngineRegistry::descriptor (engineIndex);
        const int np = juce::jlimit (0, kMaxEngineParams, desc.numParams);
        const bool proxiesValid = proxiesForEngine[n].load (std::memory_order_acquire) == engineIndex;

        for (int p = 0; p < np; ++p)
            dst.params[p] = proxiesValid ? topos::params::denormalise (desc.params[p], src.p[p]->load())
                                         : desc.params[p].def;

        for (int p = np; p < kMaxEngineParams; ++p)
            dst.params[p] = 0.0f;
    }
}

//============================================================================
// audio
//============================================================================
void ToposAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < numCh; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numCh <= 0 || numSamples <= 0)
        return;

    updateRackState();
    rack.setState (state);

    // Keep the reported tail current from the audio thread as well as the timer:
    // tailSeconds() is plain arithmetic over the slot states, so it is safe here,
    // and a host that queries the tail before the first timer tick still gets a
    // real value instead of zero.
    tailSecondsCached.store (rack.tailSeconds(), std::memory_order_relaxed);

    const bool stereo = (numCh >= 2);
    const int  chunk  = preparedBlock;

    if (stereo)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        for (int off = 0; off < numSamples; off += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - off);
            rack.process (L + off, R + off, n);
        }
    }
    else
    {
        // mono host: run the (inherently stereo) rack on a pre-allocated
        // scratch pair and fold the result back down.
        float* M  = buffer.getWritePointer (0);
        float* sL = scratch.getWritePointer (0);
        float* sR = scratch.getWritePointer (1);

        for (int off = 0; off < numSamples; off += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - off);

            for (int i = 0; i < n; ++i) { sL[i] = M[off + i]; sR[i] = M[off + i]; }

            rack.process (sL, sR, n);

            for (int i = 0; i < n; ++i) M[off + i] = 0.5f * (sL[i] + sR[i]);
        }
    }
}

//============================================================================
// message thread: oversample factor changes need a (non-RT-safe) re-prepare
//============================================================================
void ToposAudioProcessor::syncEngineDefaults()
{
    for (int n = 0; n < kNumSlots; ++n)
    {
        const int engineIndex = juce::jlimit (0, EngineRegistry::count() - 1,
                                             (int) std::lround (slots[n].engine->load()));
        if (proxiesForEngine[n].load (std::memory_order_acquire) == engineIndex)
            continue;

        // engine changed (UI, host automation or preset): load its defaults
        const EngineDescriptor& desc = EngineRegistry::descriptor (engineIndex);
        const int np = juce::jlimit (0, kMaxEngineParams, desc.numParams);
        engineForSlot[n].store (engineIndex, std::memory_order_relaxed);

        for (int p = 0; p < np; ++p)
            if (auto* param = apvts.getParameter (topos::params::slotParamId (n, p)))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (topos::params::normalise (desc.params[p], desc.params[p].def));
                param->endChangeGesture();
            }

        proxiesForEngine[n].store (engineIndex, std::memory_order_release);
    }
}

void ToposAudioProcessor::timerCallback()
{
    if (pOversample == nullptr)
        return;

    syncEngineDefaults();

    const int wanted = osFactorFromChoice ((int) pOversample->load());

    if (wanted != appliedOs.load())
    {
        suspendProcessing (true);
        rack.setOversampleRate (wanted);
        rack.reset();
        appliedOs.store (wanted);
        suspendProcessing (false);
    }

    tailSecondsCached.store (rack.tailSeconds());
}

//============================================================================
// state
//============================================================================
void ToposAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ToposAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // restored knob values already belong to the restored engines:
            // mark them valid so syncEngineDefaults() doesn't overwrite them
            for (int n = 0; n < kNumSlots; ++n)
            {
                const int e = juce::jlimit (0, EngineRegistry::count() - 1,
                                            (int) std::lround (slots[n].engine->load()));
                engineForSlot[n].store (e, std::memory_order_relaxed);
                proxiesForEngine[n].store (e, std::memory_order_release);
            }
        }
}

//============================================================================
juce::AudioProcessorEditor* ToposAudioProcessor::createEditor()
{
    return new ToposAudioProcessorEditor (*this);
}

//============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToposAudioProcessor();
}
