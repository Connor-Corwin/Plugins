#pragma once

#include "dsp/AmbienceEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/** SPX Ambience — a small ambience reverb voiced after the Yamaha SPX900
    Ambience algorithm and the Waves H-Reverb Ambience preset. */
class SPXAmbienceAudioProcessor : public juce::AudioProcessor
{
public:
    SPXAmbienceAudioProcessor();
    ~SPXAmbienceAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // The engine is single precision; this keeps the base class's double
    // overload visible rather than hidden behind the float one.
    using juce::AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Parameter IDs, shared with the editor. */
    struct ParamID
    {
        static constexpr const char* input    = "input";
        static constexpr const char* preDelay = "predelay";
        static constexpr const char* time     = "time";
        static constexpr const char* decay    = "decay";
        static constexpr const char* size     = "size";
        static constexpr const char* mix      = "mix";
        static constexpr const char* output   = "output";

        static constexpr const char* lowEqFreq  = "eqlowfreq";
        static constexpr const char* lowEqGain  = "eqlowgain";
        static constexpr const char* lowEqMode  = "eqlowmode";
        static constexpr const char* highEqFreq = "eqhighfreq";
        static constexpr const char* highEqGain = "eqhighgain";
        static constexpr const char* highEqMode = "eqhighmode";
    };

private:
    spx::AmbienceEngine::Parameters gatherParameters() const;

    spx::AmbienceEngine engine;
    juce::AudioBuffer<float> monoScratch;

    std::atomic<float>* inputParam    = nullptr;
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* timeParam     = nullptr;
    std::atomic<float>* decayParam    = nullptr;
    std::atomic<float>* sizeParam     = nullptr;
    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* outputParam   = nullptr;

    std::atomic<float>* lowEqFreqParam  = nullptr;
    std::atomic<float>* lowEqGainParam  = nullptr;
    std::atomic<float>* lowEqModeParam  = nullptr;
    std::atomic<float>* highEqFreqParam = nullptr;
    std::atomic<float>* highEqGainParam = nullptr;
    std::atomic<float>* highEqModeParam = nullptr;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPXAmbienceAudioProcessor)
};
