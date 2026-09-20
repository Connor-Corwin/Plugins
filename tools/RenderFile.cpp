// Offline WAV renderer. Runs an input file through every factory preset and
// writes one output file per preset, so versions can be compared against each
// other — or against another reverb — without a DAW in the way.
//
//     SPXAmbienceRender <input.wav> [output-dir] [options]
//
//       --mix <0-100>    override every preset's mix (100 = wet only, which
//                        is what you want for an A/B against another reverb)
//       --tail <seconds> extra silence rendered after the input so the tail
//                        is not cut off (default: reverb time + pre-delay + 0.5 s)
//       --preset <n>     render only preset n (1-based)
//       --list           print the presets and exit
//
// The engine used here is the same one the plugin runs, driven from the same
// preset table, so a render matches what the corresponding program sounds
// like in a host.

#include "../Source/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{

/** Lowercase, hyphen-separated form of a preset name, for filenames. */
juce::String slugFor (const juce::String& name)
{
    juce::String slug;
    for (auto c : name)
        slug << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (juce::CharacterFunctions::toLowerCase (c))
                                                               : juce::String ("-"));

    while (slug.contains ("--"))
        slug = slug.replace ("--", "-");

    return slug.trimCharactersAtEnd ("-").trimCharactersAtStart ("-");
}

struct Source
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    int originalChannels = 0;
};

/** Reads any format JUCE knows about, folded up to stereo. */
bool readSource (const juce::File& file, Source& out, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "could not read " + file.getFullPathName() + " (unsupported or corrupt)";
        return false;
    }

    if (reader->lengthInSamples <= 0)
    {
        error = file.getFileName() + " contains no audio";
        return false;
    }

    if (reader->lengthInSamples > 60 * 60 * 192000LL)
    {
        error = file.getFileName() + " is unreasonably long";
        return false;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    out.sampleRate = reader->sampleRate;
    out.originalChannels = static_cast<int> (reader->numChannels);
    out.audio.setSize (2, numSamples);
    out.audio.clear();

    // Read into however many channels the file has, then lay that out as stereo.
    juce::AudioBuffer<float> raw (juce::jmax (1, out.originalChannels), numSamples);
    reader->read (&raw, 0, numSamples, 0, true, true);

    if (out.originalChannels == 1)
    {
        out.audio.copyFrom (0, 0, raw, 0, 0, numSamples);
        out.audio.copyFrom (1, 0, raw, 0, 0, numSamples);
    }
    else
    {
        out.audio.copyFrom (0, 0, raw, 0, 0, numSamples);
        out.audio.copyFrom (1, 0, raw, 1, 0, numSamples);
    }

    return true;
}

struct RenderResult
{
    bool ok = false;
    float peak = 0.0f;
    double seconds = 0.0;
    juce::String error;
};

RenderResult renderPreset (const Source& source,
                           const spx::Preset& preset,
                           float mixOverride,
                           double tailOverride,
                           const juce::File& outputFile)
{
    RenderResult result;

    auto params = spx::toEngineParameters (preset);
    if (mixOverride >= 0.0f)
        params.mix = mixOverride;

    spx::AmbienceEngine engine;
    engine.prepare (source.sampleRate, 512);
    engine.setParameters (params, true);

    const double tailSeconds = tailOverride >= 0.0
                                 ? tailOverride
                                 : params.reverbTimeS + params.preDelayMs * 0.001 + 0.5;

    const int tailSamples = static_cast<int> (tailSeconds * source.sampleRate);
    const int totalSamples = source.audio.getNumSamples() + tailSamples;

    juce::AudioBuffer<float> buffer (2, totalSamples);
    buffer.clear();
    buffer.copyFrom (0, 0, source.audio, 0, 0, source.audio.getNumSamples());
    buffer.copyFrom (1, 0, source.audio, 1, 0, source.audio.getNumSamples());

    constexpr int blockSize = 512;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - offset);
        engine.process (buffer.getWritePointer (0, offset),
                        buffer.getWritePointer (1, offset),
                        count);
    }

    result.peak = buffer.getMagnitude (0, totalSamples);
    result.seconds = totalSamples / source.sampleRate;

    outputFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = outputFile.createOutputStream();

    if (stream == nullptr)
    {
        result.error = "could not open " + outputFile.getFullPathName() + " for writing";
        return result;
    }

    // Takes ownership of the stream on success.
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (source.sampleRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));

    if (writer == nullptr)
    {
        result.error = "could not create a 24-bit WAV writer for " + outputFile.getFileName();
        return result;
    }

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, totalSamples))
    {
        result.error = "failed while writing " + outputFile.getFileName();
        return result;
    }

    writer.reset();     // flush and close before we report success
    result.ok = true;
    return result;
}

void printPresets()
{
    std::printf ("  %-3s %-16s %9s %9s %7s %6s %6s\n",
                 "#", "name", "pre-delay", "rev time", "decay", "size", "mix");

    for (std::size_t i = 0; i < spx::kNumPresets; ++i)
    {
        const auto& p = spx::kPresets[i];
        std::printf ("  %-3zu %-16s %7.0f ms %8.2f s %6.0f%% %5.0f%% %5.0f%%\n",
                     i + 1, p.name, p.preDelayMs, p.reverbTimeS,
                     p.decayPercent, p.sizePercent, p.mixPercent);
    }
}

} // namespace

int main (int argc, char** argv)
{
    juce::File inputFile, outputDir;
    float mixOverride = -1.0f;
    double tailOverride = -1.0;
    int onlyPreset = 0;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        auto nextValue = [&] () -> juce::String { return (i + 1 < argc) ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--list")
        {
            printPresets();
            return 0;
        }
        if (arg == "--help" || arg == "-h")
        {
            std::printf ("usage: %s <input.wav> [output-dir] "
                         "[--mix 0-100] [--tail seconds] [--preset n] [--list]\n", argv[0]);
            return 0;
        }
        if (arg == "--mix")     { mixOverride = nextValue().getFloatValue() * 0.01f; continue; }
        if (arg == "--tail")    { tailOverride = nextValue().getDoubleValue(); continue; }
        if (arg == "--preset")  { onlyPreset = nextValue().getIntValue(); continue; }

        if (arg.startsWith ("-"))
        {
            std::fprintf (stderr, "unknown option: %s\n", arg.toRawUTF8());
            return 1;
        }

        if (inputFile == juce::File())
            inputFile = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
        else if (outputDir == juce::File())
            outputDir = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
    }

    if (inputFile == juce::File())
    {
        std::fprintf (stderr, "usage: %s <input.wav> [output-dir] "
                              "[--mix 0-100] [--tail seconds] [--preset n] [--list]\n", argv[0]);
        return 1;
    }

    if (! inputFile.existsAsFile())
    {
        std::fprintf (stderr, "no such file: %s\n", inputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    if (mixOverride >= 0.0f)
        mixOverride = juce::jlimit (0.0f, 1.0f, mixOverride);

    if (onlyPreset != 0 && ! juce::isPositiveAndBelow (onlyPreset - 1, static_cast<int> (spx::kNumPresets)))
    {
        std::fprintf (stderr, "--preset must be between 1 and %zu\n", spx::kNumPresets);
        return 1;
    }

    if (outputDir == juce::File())
        outputDir = inputFile.getParentDirectory().getChildFile (inputFile.getFileNameWithoutExtension() + " renders");

    const auto created = outputDir.createDirectory();
    if (! created.wasOk())
    {
        std::fprintf (stderr, "could not create %s: %s\n",
                      outputDir.getFullPathName().toRawUTF8(), created.getErrorMessage().toRawUTF8());
        return 1;
    }

    Source source;
    juce::String error;
    if (! readSource (inputFile, source, error))
    {
        std::fprintf (stderr, "%s\n", error.toRawUTF8());
        return 1;
    }

    std::printf ("in   %s\n", inputFile.getFullPathName().toRawUTF8());
    std::printf ("     %.0f Hz, %d channel%s, %.2f s\n",
                 source.sampleRate, source.originalChannels,
                 source.originalChannels == 1 ? "" : "s",
                 source.audio.getNumSamples() / source.sampleRate);
    std::printf ("out  %s\n", outputDir.getFullPathName().toRawUTF8());

    if (mixOverride >= 0.0f)
        std::printf ("     mix forced to %.0f%%\n", mixOverride * 100.0f);

    std::printf ("\n  %-16s %8s %8s  %s\n", "preset", "length", "peak", "file");

    const std::size_t first = onlyPreset != 0 ? static_cast<std::size_t> (onlyPreset - 1) : 0;
    const std::size_t last  = onlyPreset != 0 ? first + 1 : spx::kNumPresets;

    int failures = 0;

    for (std::size_t i = first; i < last; ++i)
    {
        const auto& preset = spx::kPresets[i];

        const juce::String fileName = inputFile.getFileNameWithoutExtension()
                                    + "_" + juce::String (i + 1).paddedLeft ('0', 2)
                                    + "_" + slugFor (preset.name) + ".wav";

        const auto outputFile = outputDir.getChildFile (fileName);
        const auto result = renderPreset (source, preset, mixOverride, tailOverride, outputFile);

        if (! result.ok)
        {
            std::fprintf (stderr, "  %-16s FAILED: %s\n", preset.name, result.error.toRawUTF8());
            ++failures;
            continue;
        }

        std::printf ("  %-16s %7.2fs %8.3f  %s%s\n",
                     preset.name, result.seconds, result.peak, fileName.toRawUTF8(),
                     result.peak > 1.0f ? "   <- clipping, lower Input" : "");
    }

    if (failures > 0)
    {
        std::fprintf (stderr, "\n%d render%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    std::printf ("\ndone\n");
    return 0;
}
