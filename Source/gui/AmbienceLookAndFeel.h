#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spxui
{

/** Panel palette. Dark brushed-metal front panel with an amber readout,
    after the look of the 1U rack units this algorithm came from. */
namespace Palette
{
    const juce::Colour background   { 0xff14161a };
    const juce::Colour panel        { 0xff1c2027 };
    const juce::Colour panelEdge    { 0xff0b0d10 };
    const juce::Colour ridge        { 0xff2a303a };
    const juce::Colour text         { 0xffc8cdd6 };
    const juce::Colour textDim      { 0xff717a88 };
    const juce::Colour accentWarm   { 0xffe08a2e };   // reverb section
    const juce::Colour accentCool   { 0xff3fb6a8 };   // level section
    const juce::Colour lcdBack      { 0xff0a0c0a };
    const juce::Colour lcdText      { 0xffffb454 };
}

//==============================================================================
/** Draws the rotary controls. Each slider's accent colour is taken from its
    `rotarySliderFillColourId`, which is how the two sections are told apart. */
class AmbienceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbienceLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbienceLookAndFeel)
};

} // namespace spxui
