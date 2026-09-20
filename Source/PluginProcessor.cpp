#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using Engine = spx::AmbienceEngine;

namespace
{
    constexpr int kNumPrograms = static_cast<int> (spx::kNumPresets);

    juce::String formatSeconds (float seconds)
    {
        return seconds < 1.0f ? juce::String (seconds * 1000.0f, 0) + " ms"
                              : juce::String (seconds, 2) + " s";
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SPXAmbienceAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto decibels = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    auto percent  = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto millis   = [] (float v, int) { return juce::String (v, 1) + " ms"; };

    Range levelRange { Engine::kMinLevelDb, Engine::kMaxLevelDb, 0.1f };

    Range preDelayRange { Engine::kMinPreDelayMs, Engine::kMaxPreDelayMs, 0.1f };
    preDelayRange.setSkewForCentre (40.0f);

    // Skewed so the short ambience settings occupy most of the travel, with
    // the long tails available at the top of the sweep.
    Range timeRange { Engine::kMinReverbTimeS, Engine::kMaxReverbTimeS, 0.01f };
    timeRange.setSkewForCentre (1.5f);

    Range percentRange { 0.0f, 100.0f, 0.1f };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::input, 1 }, "Input", levelRange, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (decibels).withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::preDelay, 1 }, "Pre-Delay", preDelayRange, 12.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (millis).withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::time, 1 }, "Reverb Time", timeRange, 1.20f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return formatSeconds (v); }).withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::decay, 1 }, "Decay", percentRange, 45.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (percent).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::size, 1 }, "Size", percentRange, 40.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (percent).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::mix, 1 }, "Mix", percentRange, 30.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (percent).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::output, 1 }, "Output", levelRange, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (decibels).withLabel ("dB")));

    // --- wet-path tone controls -------------------------------------------
    auto hertz = [] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    };

    auto gainDb = [] (float v, int)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    };

    // Named per band, so a host's automation lane says which way the filter
    // goes rather than just "Pass".
    auto lowModeText  = [] (bool v, int) { return juce::String (v ? "HPF" : "Shelf"); };
    auto highModeText = [] (bool v, int) { return juce::String (v ? "LPF" : "Shelf"); };

    // Logarithmic, so the knobs feel even across their range.
    Range lowFreqRange { Engine::kMinLowEqHz, Engine::kMaxLowEqHz, 1.0f };
    lowFreqRange.setSkewForCentre (180.0f);

    Range highFreqRange { Engine::kMinHighEqHz, Engine::kMaxHighEqHz, 1.0f };
    highFreqRange.setSkewForCentre (5500.0f);

    Range eqGainRange { -Engine::kMaxEqGainDb, Engine::kMaxEqGainDb, 0.1f };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::lowEqFreq, 1 }, "Low Freq", lowFreqRange, 120.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (hertz).withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::lowEqGain, 1 }, "Low Gain", eqGainRange, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (gainDb).withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::lowEqMode, 1 }, "Low Mode", false,
        juce::AudioParameterBoolAttributes{}.withStringFromValueFunction (lowModeText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::highEqFreq, 1 }, "High Freq", highFreqRange, 8000.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (hertz).withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::highEqGain, 1 }, "High Gain", eqGainRange, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (gainDb).withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::highEqMode, 1 }, "High Mode", false,
        juce::AudioParameterBoolAttributes{}.withStringFromValueFunction (highModeText)));

    return layout;
}

//==============================================================================
SPXAmbienceAudioProcessor::SPXAmbienceAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputParam    = apvts.getRawParameterValue (ParamID::input);
    preDelayParam = apvts.getRawParameterValue (ParamID::preDelay);
    timeParam     = apvts.getRawParameterValue (ParamID::time);
    decayParam    = apvts.getRawParameterValue (ParamID::decay);
    sizeParam     = apvts.getRawParameterValue (ParamID::size);
    mixParam      = apvts.getRawParameterValue (ParamID::mix);
    outputParam   = apvts.getRawParameterValue (ParamID::output);

    lowEqFreqParam  = apvts.getRawParameterValue (ParamID::lowEqFreq);
    lowEqGainParam  = apvts.getRawParameterValue (ParamID::lowEqGain);
    lowEqModeParam  = apvts.getRawParameterValue (ParamID::lowEqMode);
    highEqFreqParam = apvts.getRawParameterValue (ParamID::highEqFreq);
    highEqGainParam = apvts.getRawParameterValue (ParamID::highEqGain);
    highEqModeParam = apvts.getRawParameterValue (ParamID::highEqMode);
}

//==============================================================================
Engine::Parameters SPXAmbienceAudioProcessor::gatherParameters() const
{
    Engine::Parameters p;
    p.inputDb     = inputParam->load();
    p.outputDb    = outputParam->load();
    p.mix         = mixParam->load() * 0.01f;
    p.preDelayMs  = preDelayParam->load();
    p.reverbTimeS = timeParam->load();
    p.decay       = decayParam->load() * 0.01f;
    p.size        = sizeParam->load() * 0.01f;

    using Mode = Engine::BandMode;
    p.lowEqHz      = lowEqFreqParam->load();
    p.lowEqGainDb  = lowEqGainParam->load();
    p.lowEqMode    = lowEqModeParam->load() > 0.5f ? Mode::pass : Mode::shelf;
    p.highEqHz     = highEqFreqParam->load();
    p.highEqGainDb = highEqGainParam->load();
    p.highEqMode   = highEqModeParam->load() > 0.5f ? Mode::pass : Mode::shelf;
    return p;
}

void SPXAmbienceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setParameters (gatherParameters(), true);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock));
    monoScratch.clear();
}

double SPXAmbienceAudioProcessor::getTailLengthSeconds() const
{
    // Reverb time plus the longest pre-delay, so hosts render enough tail.
    return static_cast<double> (timeParam->load()) + Engine::kMaxPreDelayMs * 0.001;
}

bool SPXAmbienceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void SPXAmbienceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || numChannels == 0)
        return;

    engine.setParameters (gatherParameters());

    if (numChannels >= 2)
    {
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    }
    else
    {
        // Mono host: run the stereo engine and fold the returns back together,
        // so the reflection pattern stays the same as in stereo. Chunked
        // against the scratch buffer rather than resized, since the audio
        // thread must not allocate if the host hands us an oversized block.
        float* left = buffer.getWritePointer (0);
        float* scratch = monoScratch.getWritePointer (0);
        const int chunkSize = monoScratch.getNumSamples();

        for (int offset = 0; offset < numSamples; offset += chunkSize)
        {
            const int count = juce::jmin (chunkSize, numSamples - offset);
            float* block = left + offset;

            juce::FloatVectorOperations::copy (scratch, block, count);
            engine.process (block, scratch, count);
            juce::FloatVectorOperations::add (block, scratch, count);
            juce::FloatVectorOperations::multiply (block, 0.5f, count);
        }
    }
}

//==============================================================================
int SPXAmbienceAudioProcessor::getNumPrograms() { return kNumPrograms; }

const juce::String SPXAmbienceAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, kNumPrograms)
               ? juce::String (spx::kPresets[static_cast<std::size_t> (index)].name)
               : juce::String();
}

void SPXAmbienceAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumPrograms))
        return;

    currentProgram = index;
    const auto& program = spx::kPresets[static_cast<std::size_t> (index)];

    auto apply = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    apply (ParamID::input,    program.inputDb);
    apply (ParamID::preDelay, program.preDelayMs);
    apply (ParamID::time,     program.reverbTimeS);
    apply (ParamID::decay,    program.decayPercent);
    apply (ParamID::size,     program.sizePercent);
    apply (ParamID::mix,      program.mixPercent);
    apply (ParamID::output,   program.outputDb);
    apply (ParamID::lowEqFreq,  program.lowEqHz);
    apply (ParamID::lowEqGain,  program.lowEqGainDb);
    apply (ParamID::lowEqMode,  program.lowEqPass ? 1.0f : 0.0f);
    apply (ParamID::highEqFreq, program.highEqHz);
    apply (ParamID::highEqGain, program.highEqGainDb);
    apply (ParamID::highEqMode, program.highEqPass ? 1.0f : 0.0f);
}

//==============================================================================
void SPXAmbienceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SPXAmbienceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);

        if (state.isValid() && state.hasType (apvts.state.getType()))
        {
            currentProgram = juce::jlimit (0, kNumPrograms - 1,
                                           static_cast<int> (state.getProperty ("program", 0)));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* SPXAmbienceAudioProcessor::createEditor()
{
    return new SPXAmbienceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPXAmbienceAudioProcessor();
}
