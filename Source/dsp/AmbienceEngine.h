#pragma once

#include "DspUtils.h"

#include <array>
#include <cstddef>

namespace spx
{

//==============================================================================
/**
    Ambience reverb core, voiced after the Yamaha SPX900 "Ambience" algorithm
    and the Waves H-Reverb Ambience preset: a dense early-reflection cluster
    in front of a modest, slightly dark tail.

    Topology (mono send, stereo return, as on the hardware):

        in -> pre-delay -> band limit -> early reflections (tapped delay) -+-> wet
                                      \                                    |
                                       -> diffusion -> 8x8 FDN tail -------+

    The two decay controls are separate, as they are on the SPX900:
      * Decay      shapes the early-reflection envelope (room tightness).
      * Reverb Time sets the RT60 of the late tail.

    This class deliberately has no JUCE dependency so the DSP can be built and
    measured on its own (see tools/DspTest.cpp).
*/
class AmbienceEngine
{
public:
    static constexpr std::size_t kNumTailLines = 8;
    static constexpr std::size_t kNumTaps      = 16;
    static constexpr std::size_t kNumDiffusers = 4;
    static constexpr int         kControlBlock = 32;

    // Parameter ranges, shared with the plugin's parameter layout.
    static constexpr float kMinPreDelayMs   = 0.0f;
    static constexpr float kMaxPreDelayMs   = 250.0f;
    static constexpr float kMinReverbTimeS  = 0.10f;
    static constexpr float kMaxReverbTimeS  = 10.0f;
    static constexpr float kMinLevelDb      = -24.0f;
    static constexpr float kMaxLevelDb      = 24.0f;

    // Wet-path tone controls. The two bands meet at 1.5 kHz.
    static constexpr float kMinLowEqHz   = 20.0f;
    static constexpr float kMaxLowEqHz   = 1500.0f;
    static constexpr float kMinHighEqHz  = 1500.0f;
    static constexpr float kMaxHighEqHz  = 20000.0f;
    static constexpr float kMaxEqGainDb  = 6.0f;

    /** Butterworth, 12 dB/octave, for the two Pass modes. */
    static constexpr float kPassQ = 0.70710678f;

    /** A shelf within this much of flat is skipped rather than computed, so
        the default settings leave the wet path untouched. */
    static constexpr float kEqBypassDb = 0.05f;

    enum class BandMode
    {
        shelf = 0,   /**< Low band: low shelf. High band: high shelf. */
        pass  = 1    /**< Low band: high-pass. High band: low-pass. */
    };

    struct Parameters
    {
        float inputDb      = 0.0f;   // -24 .. +24 dB
        float outputDb     = 0.0f;   // -24 .. +24 dB
        float mix          = 0.30f;  // 0 .. 1 (equal power)
        float preDelayMs   = 12.0f;  // 0 .. 250 ms
        float reverbTimeS  = 1.20f;  // 0.1 .. 10 s
        float decay        = 0.45f;  // 0 .. 1, early-reflection envelope
        float size         = 0.40f;  // 0 .. 1, room scale

        // Tone controls, applied to the wet signal only.
        float    lowEqHz    = 120.0f;             // 20 .. 1500 Hz
        float    lowEqGainDb = 0.0f;              // -6 .. +6 dB (shelf mode only)
        BandMode lowEqMode  = BandMode::shelf;
        float    highEqHz   = 8000.0f;            // 1500 .. 20000 Hz
        float    highEqGainDb = 0.0f;             // -6 .. +6 dB (shelf mode only)
        BandMode highEqMode = BandMode::shelf;
    };

    void prepare (double sampleRateIn, int maxBlockSize);
    void reset();

    /** Applies a new parameter set. Cheap enough to call every block.
        Pass snap = true to jump the smoothers straight to the new values,
        which is what you want on prepare rather than mid-stream. */
    void setParameters (const Parameters& p, bool snap = false);

    /** In-place stereo processing. */
    void process (float* left, float* right, int numSamples);

private:
    void updateControlRate();
    void recomputeEarlyReflections();
    void recomputeTailGains();
    void updateToneFilters();
    void snapSmoothers();

    double sampleRate = 44100.0;

    Parameters params;

    // --- level / mix smoothing -------------------------------------------
    Smoother inputGain, outputGain, dryGain, wetGain;

    // --- pre-delay --------------------------------------------------------
    DelayLine preDelay;
    Smoother  preDelaySamples;

    // --- band limiting (the SPX ran at 31.25 kHz; keep the wet path dark) --
    OnePoleHP sendHighPass;
    OnePoleLP sendLowPass;

    // --- early reflections -------------------------------------------------
    DelayLine erLine;
    std::array<float, kNumTaps> erTimeL {}, erTimeR {};   // samples
    std::array<float, kNumTaps> erGainL {}, erGainR {};
    float erSizeScaleUsed = -1.0f;
    float erDecayUsed     = -1.0f;

    // --- diffusion ---------------------------------------------------------
    std::array<AllPass, kNumDiffusers> diffuserL, diffuserR;

    // --- late tail (feedback delay network) --------------------------------
    std::array<DelayLine, kNumTailLines>         tailLine;
    std::array<AbsorbentFeedback, kNumTailLines> tailAbsorb;
    std::array<float, kNumTailLines>             tailState {};
    std::array<Smoother, kNumTailLines>          tailDelay;      // samples, modulated
    std::array<Smoother, kNumTailLines>          tailFeedback;   // DC gain per pass
    std::array<Smoother, kNumTailLines>          tailDamping;    // absorbent pole
    std::array<float, kNumTailLines>     tailBaseSamples {};
    std::array<float, kNumTailLines>     lfoPhase {};
    std::array<float, kNumTailLines>     lfoInc {};
    float lfoDepthSamples = 0.0f;

    float tailSizeScaleUsed = -1.0f;
    float tailTimeUsed      = -1.0f;

    DCBlocker dcL, dcR;

    // --- wet-path tone controls -------------------------------------------
    // Frequencies are smoothed in the log domain so a sweep sounds even.
    Smoother lowEqLogHz, lowEqGain, highEqLogHz, highEqGain;
    Biquad   lowBandL, lowBandR, highBandL, highBandR;
    bool     lowBandActive = false;
    bool     highBandActive = false;
    bool     lowBandWasActive = false;
    bool     highBandWasActive = false;

    int controlCounter = 0;
};

} // namespace spx
