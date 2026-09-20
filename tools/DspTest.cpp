// Standalone measurement harness for the ambience core.
//
// The DSP in Source/dsp has no JUCE dependency, so it can be built and
// measured directly:
//
//     c++ -std=c++17 -O2 -I Source/dsp tools/DspTest.cpp
//         Source/dsp/AmbienceEngine.cpp -o dsptest && ./dsptest
//
// Checks RT60 accuracy against the Reverb Time control, stability at the
// extremes, the early-reflection envelope response to Decay, and bypass
// transparency at 0% mix.

#include "AmbienceEngine.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using spx::AmbienceEngine;

namespace
{

int failures = 0;

void check (bool ok, const char* what, const char* detail = "")
{
    std::printf ("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (! ok)
        ++failures;
}

/** Renders the wet-only impulse response of one engine configuration. */
std::vector<float> impulseResponse (const AmbienceEngine::Parameters& p,
                                    double sampleRate,
                                    double seconds,
                                    std::vector<float>* rightOut = nullptr)
{
    AmbienceEngine engine;
    engine.prepare (sampleRate, 512);

    auto params = p;
    params.mix = 1.0f;              // wet only, so the dry path cannot skew the measurement
    engine.setParameters (params, true);

    const int n = static_cast<int> (seconds * sampleRate);
    std::vector<float> l (static_cast<size_t> (n), 0.0f), r (static_cast<size_t> (n), 0.0f);
    l[0] = r[0] = 1.0f;

    const int block = 256;
    for (int i = 0; i < n; i += block)
    {
        const int count = std::min (block, n - i);
        engine.process (l.data() + i, r.data() + i, count);
    }

    if (rightOut != nullptr)
        *rightOut = r;

    return l;
}

/** Cascaded RBJ band-pass, used to isolate an octave band of the IR.

    One section has 6 dB/octave skirts, which is fine for reading a decay
    slope but nowhere near enough to measure how much energy a filter removed
    from a band: broadband content several octaves away leaks straight into
    the reading. `passes` cascades the section for 6 dB/octave each.
*/
std::vector<float> bandPass (const std::vector<float>& in, double sampleRate, double centreHz, double q,
                             int passes = 1)
{
    const double w0 = 2.0 * M_PI * centreHz / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);

    const double a0 =  1.0 + alpha;
    const double b0 =  alpha / a0;
    const double b2 = -alpha / a0;
    const double a1 = (-2.0 * std::cos (w0)) / a0;
    const double a2 = ( 1.0 - alpha) / a0;

    std::vector<float> out = in;

    for (int pass = 0; pass < passes; ++pass)
    {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        for (size_t i = 0; i < out.size(); ++i)
        {
            const double x = out[i];
            const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            out[i] = static_cast<float> (y);
        }
    }

    return out;
}

/** T30 estimate by Schroeder backward integration, extrapolated to RT60.

    Reverberation time is a band quantity, so this measures the 500 Hz octave
    band and skips the first `trimMs`, where the direct sound and the early
    reflection cluster would otherwise dominate the slope. That matches how
    RT60 is measured on a real room or hardware unit.
*/
double measureRT60 (const std::vector<float>& irFull, double sampleRate, double trimMs = 60.0)
{
    const auto banded = bandPass (irFull, sampleRate, 500.0, 1.0);

    const size_t trim = std::min (banded.size(), static_cast<size_t> (trimMs * 0.001 * sampleRate));
    const std::vector<float> ir (banded.begin() + static_cast<long> (trim), banded.end());

    const size_t n = ir.size();
    std::vector<double> schroeder (n, 0.0);

    double running = 0.0;
    for (size_t i = n; i-- > 0;)
    {
        running += static_cast<double> (ir[i]) * static_cast<double> (ir[i]);
        schroeder[i] = running;
    }

    if (schroeder[0] <= 0.0)
        return -1.0;

    const double ref = schroeder[0];

    auto timeAtDb = [&] (double db) -> double
    {
        const double threshold = ref * std::pow (10.0, db / 10.0);
        for (size_t i = 0; i < n; ++i)
            if (schroeder[i] <= threshold)
                return static_cast<double> (i) / sampleRate;
        return -1.0;
    };

    const double t5  = timeAtDb (-5.0);
    const double t35 = timeAtDb (-35.0);

    if (t5 < 0.0 || t35 < 0.0 || t35 <= t5)
        return -1.0;

    return (t35 - t5) * 2.0;   // T30 -> RT60
}

double peakAbs (const std::vector<float>& v, size_t from = 0)
{
    double peak = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        peak = std::max (peak, std::fabs (static_cast<double> (v[i])));
    return peak;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

/** Centre of gravity of the energy envelope, in ms. */
double energyCentroidMs (const std::vector<float>& ir, double sampleRate, double windowMs)
{
    const size_t n = std::min (ir.size(), static_cast<size_t> (windowMs * 0.001 * sampleRate));
    double weighted = 0.0, total = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double e = static_cast<double> (ir[i]) * static_cast<double> (ir[i]);
        weighted += e * (static_cast<double> (i) / sampleRate * 1000.0);
        total += e;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

//==============================================================================
void testReverbTimeAccuracy (double sampleRate)
{
    std::printf ("\nRT60 tracking @ %.0f Hz\n", sampleRate);

    const float targets[] = { 0.3f, 0.6f, 1.2f, 2.5f, 5.0f, 8.0f, 10.0f };

    for (float target : targets)
    {
        AmbienceEngine::Parameters p;
        p.reverbTimeS = target;
        p.size = 0.4f;
        p.decay = 0.0f;          // tight cluster, so the tail dominates the slope
        p.preDelayMs = 0.0f;

        const auto ir = impulseResponse (p, sampleRate, target * 1.8 + 1.0);
        const double measured = measureRT60 (ir, sampleRate);
        const double errorPct = (measured - target) / target * 100.0;

        char detail[160];
        std::snprintf (detail, sizeof (detail),
                       "target %.2fs -> measured %.2fs (%+.1f%%)", target, measured, errorPct);

        check (measured > 0.0 && std::fabs (errorPct) < 15.0, "reverb time tracks", detail);
    }
}

void testSizeAffectsDensity (double sampleRate)
{
    std::printf ("\nSize scaling\n");

    double previousCentroid = 0.0;
    bool monotonic = true;

    for (float size : { 0.0f, 0.35f, 0.7f, 1.0f })
    {
        AmbienceEngine::Parameters p;
        p.size = size;
        p.reverbTimeS = 1.5f;
        p.decay = 0.8f;
        p.preDelayMs = 0.0f;

        const auto ir = impulseResponse (p, sampleRate, 1.0);
        const double centroid = energyCentroidMs (ir, sampleRate, 250.0);

        char detail[160];
        std::snprintf (detail, sizeof (detail), "size %.2f -> energy centroid %.1f ms", size, centroid);
        std::printf ("       %s\n", detail);

        if (size > 0.0f && centroid <= previousCentroid)
            monotonic = false;

        previousCentroid = centroid;
    }

    check (monotonic, "larger size pushes reflection energy later");
}

void testDecayShapesEarlyReflections (double sampleRate)
{
    std::printf ("\nDecay (early-reflection envelope)\n");

    AmbienceEngine::Parameters tight;
    tight.decay = 0.0f;
    tight.reverbTimeS = 0.4f;
    tight.size = 0.4f;
    tight.preDelayMs = 0.0f;

    auto spread = tight;
    spread.decay = 1.0f;

    const auto irTight  = impulseResponse (tight, sampleRate, 1.0);
    const auto irSpread = impulseResponse (spread, sampleRate, 1.0);

    const double cTight  = energyCentroidMs (irTight, sampleRate, 150.0);
    const double cSpread = energyCentroidMs (irSpread, sampleRate, 150.0);

    char detail[160];
    std::snprintf (detail, sizeof (detail), "centroid %.1f ms at Decay 0 vs %.1f ms at Decay 1", cTight, cSpread);
    check (cSpread > cTight * 1.25, "Decay spreads the reflection cluster", detail);

    // Sweeping Decay should not behave like a volume control.
    const double pTight  = peakAbs (irTight);
    const double pSpread = peakAbs (irSpread);
    const double ratioDb = 20.0 * std::log10 (pSpread / pTight);

    std::snprintf (detail, sizeof (detail), "peak difference %.1f dB", ratioDb);
    check (std::fabs (ratioDb) < 9.0, "Decay is not a level control", detail);
}

void testStability (double sampleRate)
{
    std::printf ("\nStability and decay to silence\n");

    AmbienceEngine::Parameters worst;
    worst.reverbTimeS = AmbienceEngine::kMaxReverbTimeS;
    worst.size = 1.0f;
    worst.decay = 1.0f;
    worst.preDelayMs = AmbienceEngine::kMaxPreDelayMs;
    worst.mix = 1.0f;

    AmbienceEngine engine;
    engine.prepare (sampleRate, 512);
    engine.setParameters (worst, true);

    const int burst = static_cast<int> (sampleRate);          // 1 s of full-scale noise
    const int tail  = static_cast<int> (sampleRate * 60.0);   // then a full minute of silence

    unsigned int seed = 12345u;
    auto noise = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<float> (seed >> 8) / 8388608.0f) - 1.0f;
    };

    std::vector<float> l (512), r (512);
    double peakDuringBurst = 0.0;
    bool finite = true;

    for (int i = 0; i < burst; i += 512)
    {
        for (int k = 0; k < 512; ++k)
            l[k] = r[k] = noise();

        engine.process (l.data(), r.data(), 512);
        peakDuringBurst = std::max (peakDuringBurst, peakAbs (l));
        finite = finite && allFinite (l) && allFinite (r);
    }

    double peakLate = 0.0;
    for (int i = 0; i < tail; i += 512)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.process (l.data(), r.data(), 512);
        finite = finite && allFinite (l) && allFinite (r);

        if (i > static_cast<int> (sampleRate * 45.0))
            peakLate = std::max (peakLate, peakAbs (l));
    }

    char detail[160];
    std::snprintf (detail, sizeof (detail), "peak %.3f during burst, %.3g after 45 s of silence",
                   peakDuringBurst, peakLate);
    check (finite, "no NaN or Inf at the longest tail");
    check (peakLate < 1.0e-6, "tail decays to silence", detail);

    // Full-scale noise into a 10 s tail at unity gain should stay within a few
    // dB of the input, not build up.
    check (peakDuringBurst < 4.0, "wet output stays in range", detail);

    // Separately, the gain controls at both extremes must not produce anything
    // non-finite.
    auto hot = worst;
    hot.inputDb = AmbienceEngine::kMaxLevelDb;
    hot.outputDb = AmbienceEngine::kMaxLevelDb;

    AmbienceEngine hotEngine;
    hotEngine.prepare (sampleRate, 512);
    hotEngine.setParameters (hot, true);

    bool hotFinite = true;
    for (int i = 0; i < static_cast<int> (sampleRate * 5.0); i += 512)
    {
        for (int k = 0; k < 512; ++k)
            l[k] = r[k] = noise();

        hotEngine.process (l.data(), r.data(), 512);
        hotFinite = hotFinite && allFinite (l) && allFinite (r);
    }

    check (hotFinite, "no NaN or Inf at +24 dB in and out");
}

void testDryBypass (double sampleRate)
{
    std::printf ("\nDry path\n");

    AmbienceEngine::Parameters p;
    p.mix = 0.0f;
    p.inputDb = 0.0f;
    p.outputDb = 0.0f;
    p.reverbTimeS = 4.0f;

    AmbienceEngine engine;
    engine.prepare (sampleRate, 512);
    engine.setParameters (p, true);

    const int n = 8192;
    std::vector<float> l (n), r (n), reference (n);

    for (int i = 0; i < n; ++i)
    {
        const float v = std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (i) / static_cast<float> (sampleRate));
        l[i] = r[i] = reference[i] = v;
    }

    engine.process (l.data(), r.data(), n);

    double maxError = 0.0;
    for (int i = 0; i < n; ++i)
        maxError = std::max (maxError, std::fabs (static_cast<double> (l[i] - reference[i])));

    char detail[160];
    std::snprintf (detail, sizeof (detail), "max deviation %.3g", maxError);
    check (maxError < 1.0e-6, "0% mix passes dry signal untouched", detail);
}

void testPreDelay (double sampleRate)
{
    std::printf ("\nPre-delay\n");

    for (float ms : { 0.0f, 40.0f, 120.0f, 250.0f })
    {
        AmbienceEngine::Parameters p;
        p.preDelayMs = ms;
        p.reverbTimeS = 1.0f;
        p.size = 0.3f;
        p.decay = 0.3f;

        const auto ir = impulseResponse (p, sampleRate, 1.5);

        // First sample whose magnitude clears a small threshold.
        size_t onset = 0;
        const double threshold = peakAbs (ir) * 0.05;
        for (size_t i = 0; i < ir.size(); ++i)
            if (std::fabs (static_cast<double> (ir[i])) > threshold) { onset = i; break; }

        const double onsetMs = static_cast<double> (onset) / sampleRate * 1000.0;
        const double expectedMin = ms;

        char detail[160];
        std::snprintf (detail, sizeof (detail), "%.0f ms requested -> wet onset at %.1f ms", ms, onsetMs);
        check (onsetMs >= expectedMin - 1.0 && onsetMs < expectedMin + 60.0, "pre-delay offsets the wet path", detail);
    }
}

void testChannelDecorrelation (double sampleRate)
{
    std::printf ("\nStereo image\n");

    AmbienceEngine::Parameters p;
    p.reverbTimeS = 2.0f;
    p.size = 0.5f;
    p.decay = 0.6f;

    std::vector<float> right;
    const auto left = impulseResponse (p, sampleRate, 2.0, &right);

    double dot = 0.0, energyL = 0.0, energyR = 0.0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        dot += static_cast<double> (left[i]) * static_cast<double> (right[i]);
        energyL += static_cast<double> (left[i]) * static_cast<double> (left[i]);
        energyR += static_cast<double> (right[i]) * static_cast<double> (right[i]);
    }

    const double correlation = dot / std::sqrt (energyL * energyR);

    char detail[160];
    std::snprintf (detail, sizeof (detail), "L/R correlation %.3f", correlation);
    check (std::fabs (correlation) < 0.4, "channels are decorrelated", detail);
}

//==============================================================================
/** Magnitude response of a biquad at one frequency, by DFT of its impulse
    response. */
double biquadGainDb (spx::Biquad filter, double freqHz, double sampleRate)
{
    constexpr int n = 16384;
    double re = 0.0, im = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const float y = filter.process (i == 0 ? 1.0f : 0.0f);
        const double w = 2.0 * M_PI * freqHz * i / sampleRate;
        re += y * std::cos (w);
        im -= y * std::sin (w);
    }

    return 20.0 * std::log10 (std::sqrt (re * re + im * im));
}

void testToneFilterShapes (double sampleRate)
{
    std::printf ("\nTone filter responses\n");

    auto near = [] (double value, double expected, double tolerance, const char* what)
    {
        char detail[160];
        std::snprintf (detail, sizeof (detail), "%s: %+.2f dB (expected %+.1f +/- %.1f)",
                       what, value, expected, tolerance);
        check (std::fabs (value - expected) < tolerance, "response", detail);
    };

    // Low shelf: +6 dB below the corner, flat above it.
    {
        spx::Biquad f;
        f.setLowShelf (200.0f, 6.0f, sampleRate);
        near (biquadGainDb (f, 25.0, sampleRate), 6.0, 0.5, "low shelf +6 dB at 200 Hz, measured at 25 Hz");
        near (biquadGainDb (f, 8000.0, sampleRate), 0.0, 0.5, "  same filter at 8 kHz");
    }

    // And symmetrically for a cut.
    {
        spx::Biquad f;
        f.setLowShelf (200.0f, -6.0f, sampleRate);
        near (biquadGainDb (f, 25.0, sampleRate), -6.0, 0.5, "low shelf -6 dB at 200 Hz, measured at 25 Hz");
    }

    // High shelf: +6 dB above the corner, flat below.
    {
        spx::Biquad f;
        f.setHighShelf (5000.0f, 6.0f, sampleRate);
        near (biquadGainDb (f, 18000.0, sampleRate), 6.0, 0.7, "high shelf +6 dB at 5 kHz, measured at 18 kHz");
        near (biquadGainDb (f, 200.0, sampleRate), 0.0, 0.5, "  same filter at 200 Hz");
    }

    // HPF and LPF: -3 dB at the corner, 12 dB per octave beyond it.
    {
        spx::Biquad f;
        f.setHighPass (500.0f, spx::AmbienceEngine::kPassQ, sampleRate);
        near (biquadGainDb (f, 500.0, sampleRate), -3.0, 0.4, "high-pass at its 500 Hz corner");
        near (biquadGainDb (f, 250.0, sampleRate), -12.3, 1.0, "  one octave below");
        near (biquadGainDb (f, 4000.0, sampleRate), 0.0, 0.4, "  well above");
    }

    {
        spx::Biquad f;
        // Measured well away from Nyquist: the bilinear transform warps the
        // response near it, so a digital filter rolls off faster there than
        // the 12.3 dB an analogue prototype gives one octave up.
        f.setLowPass (2000.0f, spx::AmbienceEngine::kPassQ, sampleRate);
        near (biquadGainDb (f, 2000.0, sampleRate), -3.0, 0.4, "low-pass at its 2 kHz corner");
        near (biquadGainDb (f, 4000.0, sampleRate), -12.3, 1.0, "  one octave above");
        near (biquadGainDb (f, 250.0, sampleRate), 0.0, 0.4, "  well below");
    }
}

void testEqDefaultsAreTransparent (double sampleRate)
{
    std::printf ("\nTone controls at their defaults\n");

    AmbienceEngine::Parameters flat;          // shelf mode, 0 dB on both bands
    flat.reverbTimeS = 1.5f;

    auto moved = flat;                        // same, but with the frequency knobs elsewhere
    moved.lowEqHz = 900.0f;
    moved.highEqHz = 3000.0f;

    const auto a = impulseResponse (flat, sampleRate, 1.5);
    const auto b = impulseResponse (moved, sampleRate, 1.5);

    double maxDifference = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maxDifference = std::max (maxDifference, std::fabs (static_cast<double> (a[i] - b[i])));

    char detail[160];
    std::snprintf (detail, sizeof (detail), "max difference %.3g across the whole impulse response", maxDifference);
    check (maxDifference == 0.0, "a 0 dB shelf is skipped, whatever its frequency", detail);
}

void testEqIsWetOnly (double sampleRate)
{
    std::printf ("\nTone controls are wet only\n");

    AmbienceEngine::Parameters p;
    p.mix = 0.0f;
    p.reverbTimeS = 3.0f;
    p.lowEqMode = AmbienceEngine::BandMode::pass;    // as destructive as the controls get
    p.lowEqHz = AmbienceEngine::kMaxLowEqHz;
    p.highEqMode = AmbienceEngine::BandMode::pass;
    p.highEqHz = AmbienceEngine::kMinHighEqHz;

    AmbienceEngine engine;
    engine.prepare (sampleRate, 512);
    engine.setParameters (p, true);

    const int n = 8192;
    std::vector<float> l (n), r (n), reference (n);

    for (int i = 0; i < n; ++i)
    {
        const float v = std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (i) / static_cast<float> (sampleRate));
        l[i] = r[i] = reference[i] = v;
    }

    engine.process (l.data(), r.data(), n);

    double maxError = 0.0;
    for (int i = 0; i < n; ++i)
        maxError = std::max (maxError, std::fabs (static_cast<double> (l[i] - reference[i])));

    char detail[160];
    std::snprintf (detail, sizeof (detail), "max deviation %.3g with the low band in HPF and the high band in LPF", maxError);
    check (maxError < 1.0e-6, "0% mix still passes dry untouched", detail);
}

void testEqShapesTheTail (double sampleRate)
{
    std::printf ("\nTone controls shape the reverb\n");

    AmbienceEngine::Parameters flat;
    flat.reverbTimeS = 2.0f;
    flat.size = 0.5f;

    // Energy in an octave band of the wet impulse response.
    auto bandEnergyDb = [&] (const AmbienceEngine::Parameters& p, double centreHz)
    {
        const auto ir = impulseResponse (p, sampleRate, 2.5);
        const auto banded = bandPass (ir, sampleRate, centreHz, 1.0, 4);
        double energy = 0.0;
        for (float v : banded)
            energy += static_cast<double> (v) * static_cast<double> (v);
        return 10.0 * std::log10 (energy + 1.0e-30);
    };

    const double flatLow  = bandEnergyDb (flat, 100.0);
    const double flatHigh = bandEnergyDb (flat, 6000.0);

    // Low band, HPF at 1.5 kHz: the 100 Hz octave should collapse.
    auto lowCut = flat;
    lowCut.lowEqMode = AmbienceEngine::BandMode::pass;
    lowCut.lowEqHz = 1500.0f;
    const double cutLow = bandEnergyDb (lowCut, 100.0);

    char detail[160];
    std::snprintf (detail, sizeof (detail), "100 Hz octave drops %.1f dB", flatLow - cutLow);
    check (flatLow - cutLow > 20.0, "Low band in HPF mode removes lows", detail);

    // High band, LPF at 1.5 kHz: the 6 kHz octave should collapse.
    auto highCut = flat;
    highCut.highEqMode = AmbienceEngine::BandMode::pass;
    highCut.highEqHz = 1500.0f;
    const double cutHigh = bandEnergyDb (highCut, 6000.0);

    std::snprintf (detail, sizeof (detail), "6 kHz octave drops %.1f dB", flatHigh - cutHigh);
    check (flatHigh - cutHigh > 15.0, "High band in LPF mode removes highs", detail);

    // Shelves move the same bands by roughly their gain setting.
    auto lowBoost = flat;
    lowBoost.lowEqGainDb = AmbienceEngine::kMaxEqGainDb;
    lowBoost.lowEqHz = 200.0f;
    const double boostLow = bandEnergyDb (lowBoost, 100.0) - flatLow;

    std::snprintf (detail, sizeof (detail), "100 Hz octave rises %.1f dB for a +6 dB shelf", boostLow);
    check (boostLow > 3.0 && boostLow < 9.0, "Low shelf boosts", detail);

    auto highCutShelf = flat;
    highCutShelf.highEqGainDb = -AmbienceEngine::kMaxEqGainDb;
    highCutShelf.highEqHz = 4000.0f;
    const double shelfHigh = flatHigh - bandEnergyDb (highCutShelf, 6000.0);

    std::snprintf (detail, sizeof (detail), "6 kHz octave drops %.1f dB for a -6 dB shelf", shelfHigh);
    check (shelfHigh > 3.0 && shelfHigh < 9.0, "High shelf cuts", detail);
}

} // namespace

int main()
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n================ %.0f Hz ================\n", sampleRate);
        testReverbTimeAccuracy (sampleRate);
        testToneFilterShapes (sampleRate);
        testEqDefaultsAreTransparent (sampleRate);
        testEqIsWetOnly (sampleRate);
        testEqShapesTheTail (sampleRate);
        testDecayShapesEarlyReflections (sampleRate);
        testSizeAffectsDensity (sampleRate);
        testPreDelay (sampleRate);
        testChannelDecorrelation (sampleRate);
        testDryBypass (sampleRate);
        testStability (sampleRate);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
