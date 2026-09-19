#pragma once

#include "PluginProcessor.h"
#include "gui/AmbienceLookAndFeel.h"

//==============================================================================
/** A labelled rotary control: name above, knob in the middle, value below. */
class KnobPanel : public juce::Component
{
public:
    KnobPanel (const juce::String& name, juce::Colour accent, float knobProportion);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

    juce::Slider slider;
    juce::String title;

    /** Called when the value changes or the mouse moves over the control, so
        the editor can drive the readout. */
    std::function<void (KnobPanel&)> onHighlight;

    void setUiScale (float s) { uiScale = s; }

private:
    juce::Colour accentColour;
    float knobFraction;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobPanel)
};

//==============================================================================
class SPXAmbienceAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit SPXAmbienceAudioProcessorEditor (SPXAmbienceAudioProcessor&);
    ~SPXAmbienceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Design size; the window scales from here. */
    static constexpr int kBaseWidth  = 640;
    static constexpr int kBaseHeight = 440;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void highlight (KnobPanel&);
    float scale() const { return static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth); }

    SPXAmbienceAudioProcessor& processor;
    spxui::AmbienceLookAndFeel lookAndFeel;

    KnobPanel preDelayKnob { "PRE-DELAY", spxui::Palette::accentWarm, 0.62f };
    KnobPanel timeKnob     { "REV TIME",  spxui::Palette::accentWarm, 0.62f };
    KnobPanel decayKnob    { "DECAY",     spxui::Palette::accentWarm, 0.62f };
    KnobPanel sizeKnob     { "SIZE",      spxui::Palette::accentWarm, 0.62f };

    KnobPanel inputKnob    { "INPUT",  spxui::Palette::accentCool, 0.58f };
    KnobPanel mixKnob      { "MIX",    spxui::Palette::accentCool, 0.58f };
    KnobPanel outputKnob   { "OUTPUT", spxui::Palette::accentCool, 0.58f };

    std::vector<std::unique_ptr<Attachment>> attachments;

    juce::String readoutText { "AMBIENCE PROCESSOR" };
    juce::Rectangle<float> lcdBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPXAmbienceAudioProcessorEditor)
};
