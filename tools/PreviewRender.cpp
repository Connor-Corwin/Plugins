// Renders the plugin editor to a PNG without a host or a display, which is
// how the screenshot in the README is produced. Also a cheap way to check
// that paint() and resized() run clean after a layout change.
//
//     cmake -S . -B build -DSPX_BUILD_PREVIEW=ON
//     cmake --build build --target SPXAmbiencePreview
//     ./build/SPXAmbiencePreview docs/plugin.png [scale] [--pass]
//
// --pass flips the EQ bands to their filter modes (HPF and LPF) before
// rendering, which is how the Gain knobs' greyed-out state gets checked.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s <output.png> [scale]\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    int scale = 2;
    bool passMode = false;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--pass")
            passMode = true;
        else
            scale = juce::jlimit (1, 4, arg.getIntValue());
    }

    SPXAmbienceAudioProcessor processor;

    if (passMode)
    {
        for (const char* id : { SPXAmbienceAudioProcessor::ParamID::lowEqMode,
                                SPXAmbienceAudioProcessor::ParamID::highEqMode })
            if (auto* param = processor.apvts.getParameter (id))
                param->setValueNotifyingHost (1.0f);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::fprintf (stderr, "the processor produced no editor\n");
        return 1;
    }

    const int width  = SPXAmbienceAudioProcessorEditor::kBaseWidth;
    const int height = SPXAmbienceAudioProcessorEditor::kBaseHeight;
    editor->setSize (width, height);

    juce::Image image (juce::Image::ARGB, width * scale, height * scale, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
        editor->paintEntireComponent (g, true);
    }

    juce::File output (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    output.getParentDirectory().createDirectory();
    output.deleteFile();

    if (auto stream = output.createOutputStream())
    {
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, *stream))
        {
            std::fprintf (stderr, "failed to encode %s\n", output.getFullPathName().toRawUTF8());
            return 1;
        }
    }
    else
    {
        std::fprintf (stderr, "failed to open %s\n", output.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", output.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}
