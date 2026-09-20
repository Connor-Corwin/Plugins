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

    /** Greys the control out without hiding it, used for a Gain knob whose
        band is in Pass mode. */
    void setKnobEnabled (bool shouldBeEnabled);

private:
    juce::Colour accentColour;
    float knobFraction;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobPanel)
};

//==============================================================================
/** Two-segment Shelf / Pass switch. Toggle state false is Shelf, true is Pass,
    matching the bool parameter it attaches to. */
class ModeSwitch : public juce::Button
{
public:
    explicit ModeSwitch (juce::Colour accent);

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    void setUiScale (float s) { uiScale = s; }

private:
    juce::Colour accentColour;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSwitch)
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
    static constexpr int kBaseWidth  = 720;
    static constexpr int kBaseHeight = 620;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void highlight (KnobPanel&);
    float scale() const { return static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth); }

    SPXAmbienceAudioProcessor& processor;
    spxui::AmbienceLookAndFeel lookAndFeel;

    KnobPanel preDelayKnob { "PRE-DELAY", spxui::Palette::accentWarm, 0.62f };
    KnobPanel timeKnob     { "REV TIME",  spxui::Palette::accentWarm, 0.62f };
    KnobPanel decayKnob    { "DECAY",     spxui::Palette::accentWarm, 0.62f };
    KnobPanel sizeKnob     { "SIZE",      spxui::Palette::accentWarm, 0.62f };

    KnobPanel lowFreqKnob  { "LOW FREQ",  spxui::Palette::accentEq, 0.58f };
    KnobPanel lowGainKnob  { "LOW GAIN",  spxui::Palette::accentEq, 0.58f };
    KnobPanel highFreqKnob { "HIGH FREQ", spxui::Palette::accentEq, 0.58f };
    KnobPanel highGainKnob { "HIGH GAIN", spxui::Palette::accentEq, 0.58f };

    ModeSwitch lowModeSwitch  { spxui::Palette::accentEq };
    ModeSwitch highModeSwitch { spxui::Palette::accentEq };

    KnobPanel inputKnob    { "INPUT",  spxui::Palette::accentCool, 0.58f };
    KnobPanel mixKnob      { "MIX",    spxui::Palette::accentCool, 0.58f };
    KnobPanel outputKnob   { "OUTPUT", spxui::Palette::accentCool, 0.58f };

    std::vector<std::unique_ptr<Attachment>> attachments;
    std::unique_ptr<ButtonAttachment> lowModeAttachment, highModeAttachment;

    // Watch the mode parameters so the Gain knobs grey out in Pass mode.
    std::unique_ptr<juce::ParameterAttachment> lowModeWatcher, highModeWatcher;

    juce::String readoutText { "AMBIENCE PROCESSOR" };
    juce::Rectangle<float> lcdBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPXAmbienceAudioProcessorEditor)
};
