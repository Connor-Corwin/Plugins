// Times the ambience core against realtime. The engine has no JUCE
// dependency, so this measures the DSP alone with no host in the way.
//
//     cmake -S . -B build-test -DSPX_BUILD_PLUGIN=OFF
//     cmake --build build-test --target SPXAmbienceDspBench
//     ./build-test/SPXAmbienceDspBench

#include "AmbienceEngine.h"

#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
    constexpr int blockSize = 512;
    constexpr double renderSeconds = 60.0;

    std::printf ("Rendering %.0f s of stereo audio at a 2 s reverb time.\n\n", renderSeconds);
    std::printf ("  %-10s  %-12s  %-14s  %s\n", "rate", "elapsed", "vs realtime", "one core");

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        spx::AmbienceEngine engine;
        engine.prepare (sampleRate, blockSize);

        spx::AmbienceEngine::Parameters p;
        p.reverbTimeS = 2.0f;
        p.size = 0.5f;
        p.mix = 0.3f;
        engine.setParameters (p, true);

        std::vector<float> left (blockSize, 0.1f), right (blockSize, 0.1f);
        const int blocks = static_cast<int> (sampleRate * renderSeconds / blockSize);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
            engine.process (left.data(), right.data(), blockSize);
        const auto finish = std::chrono::steady_clock::now();

        const double elapsed = std::chrono::duration<double> (finish - start).count();

        std::printf ("  %-10.0f  %-12.3f  %-14.0f  %.2f %%\n",
                     sampleRate, elapsed, renderSeconds / elapsed, elapsed / renderSeconds * 100.0);
    }

    return 0;
}
