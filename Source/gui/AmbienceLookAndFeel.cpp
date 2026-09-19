#include "AmbienceLookAndFeel.h"

namespace spxui
{

AmbienceLookAndFeel::AmbienceLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::Slider::rotarySliderFillColourId, Palette::accentWarm);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panel);
    setColour (juce::TooltipWindow::textColourId, Palette::text);
}

juce::Font AmbienceLookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::Font (juce::FontOptions (label.getHeight() * 0.7f));
}

void AmbienceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = diameter * 0.5f;

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    const float arcRadius = radius * 0.88f;
    const float arcThickness = radius * 0.13f;

    // --- background arc ---------------------------------------------------
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Palette::ridge);
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // --- value arc, with a soft halo so it reads as lit --------------------
    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);

        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (arcThickness * 2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // --- knob body --------------------------------------------------------
    const float bodyRadius = radius * 0.66f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353c47), body.getCentreX(), body.getY(),
                                             juce::Colour (0xff171a1f), body.getCentreX(), body.getBottom(),
                                             false));
    g.fillEllipse (body);

    g.setColour (Palette::panelEdge);
    g.drawEllipse (body, 1.2f);

    // Highlight across the top of the cap.
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillEllipse (body.reduced (bodyRadius * 0.18f).withHeight (bodyRadius * 0.7f));

    // --- pointer ----------------------------------------------------------
    juce::Path pointer;
    const float pointerThickness = radius * 0.085f;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -bodyRadius * 0.92f,
                                 pointerThickness, bodyRadius * 0.52f, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (Palette::text);
    g.fillPath (pointer);

    // A dot of accent at the centre ties the cap to its arc.
    g.setColour (accent.withAlpha (slider.isMouseOverOrDragging() ? 0.9f : 0.55f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.14f, radius * 0.14f).withCentre (centre));
}

} // namespace spxui
