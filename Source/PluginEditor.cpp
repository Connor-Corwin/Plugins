#include "PluginEditor.h"

using namespace spxui;

namespace
{
    juce::Font panelFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font lcdFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }

    /** Draws a section heading with a hairline running out to the right. */
    void drawSectionRule (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                          juce::Colour accent, float fontHeight)
    {
        g.setFont (panelFont (fontHeight, true));
        const float textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + fontHeight * 0.6f;

        g.setColour (accent.withAlpha (0.85f));
        g.drawText (label, area.removeFromLeft (textWidth), juce::Justification::centredLeft, false);

        g.setColour (Palette::ridge);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1.0f));
    }
}

//==============================================================================
KnobPanel::KnobPanel (const juce::String& name, juce::Colour accent, float knobProportion)
    : title (name), accentColour (accent), knobFraction (knobProportion)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.addMouseListener (this, false);

    slider.onValueChange = [this]
    {
        repaint();
        if (onHighlight != nullptr)
            onHighlight (*this);
    };

    addAndMakeVisible (slider);
}

void KnobPanel::mouseEnter (const juce::MouseEvent&)
{
    if (onHighlight != nullptr)
        onHighlight (*this);
}

void KnobPanel::mouseExit (const juce::MouseEvent&) {}

void KnobPanel::resized()
{
    auto area = getLocalBounds().toFloat();
    const float labelHeight = area.getHeight() * 0.15f;
    area.removeFromTop (labelHeight);
    area.removeFromBottom (labelHeight);

    const float diameter = juce::jmin (area.getWidth(), area.getHeight()) * knobFraction / 0.62f;
    slider.setBounds (juce::Rectangle<float> (diameter, diameter)
                          .withCentre (area.getCentre()).toNearestInt());
}

void KnobPanel::setKnobEnabled (bool shouldBeEnabled)
{
    if (slider.isEnabled() == shouldBeEnabled)
        return;

    slider.setEnabled (shouldBeEnabled);
    repaint();
}

void KnobPanel::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float labelHeight = area.getHeight() * 0.15f;
    const float textAlpha = slider.isEnabled() ? 1.0f : 0.4f;

    g.setColour (Palette::textDim.withMultipliedAlpha (textAlpha));
    g.setFont (panelFont (juce::jmax (8.0f, 9.5f * uiScale), true));
    g.drawText (title, area.removeFromTop (labelHeight), juce::Justification::centred, false);

    g.setColour (Palette::text.withMultipliedAlpha (textAlpha));
    g.setFont (lcdFont (juce::jmax (9.0f, 11.5f * uiScale)));
    g.drawText (slider.getTextFromValue (slider.getValue()),
                area.removeFromBottom (labelHeight), juce::Justification::centred, false);
}

//==============================================================================
ModeSwitch::ModeSwitch (juce::Colour accent, juce::String filterLabel)
    : juce::Button ("mode"), accentColour (accent), filterModeLabel (std::move (filterLabel))
{
    setClickingTogglesState (true);
}

void ModeSwitch::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = bounds.getHeight() * 0.5f;
    const bool isFilter = getToggleState();

    g.setColour (Palette::lcdBack);
    g.fillRoundedRectangle (bounds, radius);

    // The lit half is whichever mode is selected.
    auto selected = isFilter ? bounds.withTrimmedLeft (bounds.getWidth() * 0.5f)
                             : bounds.withTrimmedRight (bounds.getWidth() * 0.5f);

    const float alpha = isButtonDown ? 1.0f : (isMouseOver ? 0.9f : 0.78f);
    g.setColour (accentColour.withAlpha (alpha));
    g.fillRoundedRectangle (selected.reduced (1.5f), radius - 1.5f);

    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    g.setFont (panelFont (juce::jmax (7.5f, 8.5f * uiScale), true));

    const auto leftHalf  = bounds.withTrimmedRight (bounds.getWidth() * 0.5f);
    const auto rightHalf = bounds.withTrimmedLeft (bounds.getWidth() * 0.5f);

    g.setColour (isFilter ? Palette::textDim : Palette::panelEdge);
    g.drawText ("SHELF", leftHalf, juce::Justification::centred, false);

    g.setColour (isFilter ? Palette::panelEdge : Palette::textDim);
    g.drawText (filterModeLabel, rightHalf, juce::Justification::centred, false);
}

//==============================================================================
SPXAmbienceAudioProcessorEditor::SPXAmbienceAudioProcessorEditor (SPXAmbienceAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    struct Binding { KnobPanel& knob; const char* id; };

    const Binding bindings[] =
    {
        { preDelayKnob, SPXAmbienceAudioProcessor::ParamID::preDelay },
        { timeKnob,     SPXAmbienceAudioProcessor::ParamID::time     },
        { decayKnob,    SPXAmbienceAudioProcessor::ParamID::decay    },
        { sizeKnob,     SPXAmbienceAudioProcessor::ParamID::size     },
        { lowFreqKnob,  SPXAmbienceAudioProcessor::ParamID::lowEqFreq  },
        { lowGainKnob,  SPXAmbienceAudioProcessor::ParamID::lowEqGain  },
        { highFreqKnob, SPXAmbienceAudioProcessor::ParamID::highEqFreq },
        { highGainKnob, SPXAmbienceAudioProcessor::ParamID::highEqGain },
        { inputKnob,    SPXAmbienceAudioProcessor::ParamID::input    },
        { mixKnob,      SPXAmbienceAudioProcessor::ParamID::mix      },
        { outputKnob,   SPXAmbienceAudioProcessor::ParamID::output   }
    };

    for (const auto& binding : bindings)
    {
        addAndMakeVisible (binding.knob);

        // The parameter owns the formatting, so the knob and the host agree.
        if (auto* param = processor.apvts.getParameter (binding.id))
        {
            binding.knob.slider.textFromValueFunction = [param] (double value)
            {
                return param->getText (param->convertTo0to1 (static_cast<float> (value)), 0);
            };

            binding.knob.slider.setDoubleClickReturnValue (
                true, param->convertFrom0to1 (param->getDefaultValue()));
        }

        attachments.push_back (std::make_unique<Attachment> (processor.apvts, binding.id, binding.knob.slider));
    }

    addAndMakeVisible (lowModeSwitch);
    addAndMakeVisible (highModeSwitch);

    lowModeAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, SPXAmbienceAudioProcessor::ParamID::lowEqMode, lowModeSwitch);
    highModeAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, SPXAmbienceAudioProcessor::ParamID::highEqMode, highModeSwitch);

    // Gain does nothing in HPF/LPF mode, so the knob greys out rather than
    // sitting there looking live.
    if (auto* lowMode = processor.apvts.getParameter (SPXAmbienceAudioProcessor::ParamID::lowEqMode))
    {
        lowModeWatcher = std::make_unique<juce::ParameterAttachment> (
            *lowMode, [this] (float v) { lowGainKnob.setKnobEnabled (v < 0.5f); });
        lowModeWatcher->sendInitialUpdate();
    }

    if (auto* highMode = processor.apvts.getParameter (SPXAmbienceAudioProcessor::ParamID::highEqMode))
    {
        highModeWatcher = std::make_unique<juce::ParameterAttachment> (
            *highMode, [this] (float v) { highGainKnob.setKnobEnabled (v < 0.5f); });
        highModeWatcher->sendInitialUpdate();
    }

    // Hooked up only once the attachments have pushed their initial values,
    // so opening the editor doesn't leave a knob name sitting in the readout.
    for (const auto& binding : bindings)
        binding.knob.onHighlight = [this] (KnobPanel& knob) { highlight (knob); };

    setResizable (true, true);

    // setResizeLimits installs the default constrainer, so it has to come
    // first; getConstrainer() is null until then.
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);

    if (auto* boundsConstrainer = getConstrainer())
        boundsConstrainer->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);

    setSize (kBaseWidth, kBaseHeight);
}

SPXAmbienceAudioProcessorEditor::~SPXAmbienceAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SPXAmbienceAudioProcessorEditor::highlight (KnobPanel& knob)
{
    readoutText = knob.title + "  " + knob.slider.getTextFromValue (knob.slider.getValue());
    repaint (lcdBounds.getSmallestIntegerContainer());
    startTimer (1600);
}

void SPXAmbienceAudioProcessorEditor::timerCallback()
{
    stopTimer();
    readoutText = "AMBIENCE PROCESSOR";
    repaint (lcdBounds.getSmallestIntegerContainer());
}

//==============================================================================
void SPXAmbienceAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = scale();
    auto bounds = getLocalBounds().toFloat();

    // --- panel ------------------------------------------------------------
    g.setGradientFill (juce::ColourGradient (Palette::panel, 0.0f, 0.0f,
                                             Palette::background, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawLine (0.0f, 0.5f, bounds.getWidth(), 0.5f, 1.0f);

    // --- header -----------------------------------------------------------
    auto header = bounds.removeFromTop (66.0f * s);

    g.setColour (Palette::text);
    g.setFont (panelFont (21.0f * s, true));
    g.drawText ("SPX AMBIENCE", header.withTrimmedLeft (22.0f * s).withTrimmedTop (12.0f * s),
                juce::Justification::topLeft, false);

    g.setColour (Palette::textDim);
    g.setFont (panelFont (9.5f * s));
    g.drawText ("DIGITAL AMBIENCE PROCESSOR", header.withTrimmedLeft (23.0f * s).withTrimmedTop (38.0f * s),
                juce::Justification::topLeft, false);

    // --- readout ----------------------------------------------------------
    g.setColour (Palette::lcdBack);
    g.fillRoundedRectangle (lcdBounds, 3.0f * s);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (lcdBounds, 3.0f * s, 1.0f);

    g.setColour (Palette::lcdText);
    g.setFont (lcdFont (12.5f * s));
    g.drawText (readoutText, lcdBounds, juce::Justification::centred, false);

    g.setColour (Palette::panelEdge);
    g.fillRect (0.0f, 66.0f * s, bounds.getWidth(), 1.0f * s);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRect (0.0f, 67.0f * s, bounds.getWidth(), 1.0f * s);

    // --- section headings ---------------------------------------------------
    drawSectionRule (g, { 22.0f * s, 80.0f * s, getWidth() - 44.0f * s, 14.0f * s },
                     "REVERB", Palette::accentWarm, 9.5f * s);

    drawSectionRule (g, { 22.0f * s, 272.0f * s, getWidth() - 44.0f * s, 14.0f * s },
                     "EQ  (WET ONLY)", Palette::accentEq, 9.5f * s);

    drawSectionRule (g, { 22.0f * s, 452.0f * s, getWidth() - 44.0f * s, 14.0f * s },
                     "LEVELS", Palette::accentCool, 9.5f * s);

    // A hairline between the two EQ bands, so the pairs read as Low and High
    // rather than four unrelated knobs.
    g.setColour (Palette::ridge.withAlpha (0.55f));
    g.fillRect (static_cast<float> (getWidth()) * 0.5f, 296.0f * s, 1.0f, 128.0f * s);

    // --- footer -------------------------------------------------------------
    g.setColour (Palette::textDim.withAlpha (0.7f));
    g.setFont (panelFont (9.0f * s));
    g.drawText (juce::CharPointer_UTF8 ("SPX900-style ambience  \xc2\xb7  "
                                        "16-tap early reflections into an 8-line tail"),
                juce::Rectangle<float> (22.0f * s, getHeight() - 26.0f * s, getWidth() - 44.0f * s, 16.0f * s),
                juce::Justification::centredLeft, false);
}

void SPXAmbienceAudioProcessorEditor::resized()
{
    const float s = scale();

    // Kept here rather than in paint() so partial repaints of the readout are
    // valid before the first paint.
    lcdBounds = juce::Rectangle<float> (getWidth() - 246.0f * s, 16.0f * s, 224.0f * s, 34.0f * s);

    for (auto* knob : { &preDelayKnob, &timeKnob, &decayKnob, &sizeKnob,
                        &lowFreqKnob, &lowGainKnob, &highFreqKnob, &highGainKnob,
                        &inputKnob, &mixKnob, &outputKnob })
        knob->setUiScale (s);

    lowModeSwitch.setUiScale (s);
    highModeSwitch.setUiScale (s);

    const int margin = juce::roundToInt (22.0f * s);
    const int usableWidth = getWidth() - margin * 2;

    // Reverb row: four knobs.
    {
        const int cellWidth = usableWidth / 4;
        const int top = juce::roundToInt (100.0f * s);
        const int height = juce::roundToInt (156.0f * s);

        KnobPanel* row[] { &preDelayKnob, &timeKnob, &decayKnob, &sizeKnob };
        for (int i = 0; i < 4; ++i)
            row[i]->setBounds (margin + i * cellWidth, top, cellWidth, height);
    }

    // EQ row: two bands, each a pair of knobs over a Shelf/Pass switch.
    {
        const int top = juce::roundToInt (292.0f * s);
        const int knobHeight = juce::roundToInt (116.0f * s);
        const int switchHeight = juce::roundToInt (22.0f * s);
        const int switchWidth = juce::roundToInt (108.0f * s);
        const int bandWidth = usableWidth / 2;
        const int cellWidth = bandWidth / 2;

        KnobPanel* bands[2][2] { { &lowFreqKnob, &lowGainKnob },
                                 { &highFreqKnob, &highGainKnob } };
        ModeSwitch* switches[2] { &lowModeSwitch, &highModeSwitch };

        for (int band = 0; band < 2; ++band)
        {
            const int bandLeft = margin + band * bandWidth;

            for (int k = 0; k < 2; ++k)
                bands[band][k]->setBounds (bandLeft + k * cellWidth, top, cellWidth, knobHeight);

            switches[band]->setBounds (bandLeft + (bandWidth - switchWidth) / 2,
                                       top + knobHeight + juce::roundToInt (6.0f * s),
                                       switchWidth, switchHeight);
        }
    }

    // Level row: three knobs.
    {
        const int cellWidth = usableWidth / 3;
        const int top = juce::roundToInt (472.0f * s);
        const int height = juce::roundToInt (122.0f * s);

        KnobPanel* row[] { &inputKnob, &mixKnob, &outputKnob };
        for (int i = 0; i < 3; ++i)
            row[i]->setBounds (margin + i * cellWidth, top, cellWidth, height);
    }
}
