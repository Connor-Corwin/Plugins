#include "AmbienceEngine.h"

namespace spx
{

namespace
{
    constexpr float kPi = 3.14159265358979f;

    // Largest early-reflection tap and the largest room scale, used for sizing
    // the delay buffers.
    constexpr float kMaxErTapMs   = 90.0f;
    constexpr float kMaxSizeScale = 1.90f;

    // Early-reflection pattern. Two decorrelated tap sets modelled on a small
    // wood-and-plaster room: a tight front cluster followed by a thinning
    // spray of later reflections.
    constexpr float kErTimeMsL[AmbienceEngine::kNumTaps] =
    {
         6.7f, 10.3f, 14.9f, 19.1f, 23.8f, 28.3f, 33.7f, 38.2f,
        43.9f, 48.6f, 54.1f, 59.7f, 65.3f, 71.2f, 77.8f, 84.1f
    };

    constexpr float kErTimeMsR[AmbienceEngine::kNumTaps] =
    {
         8.2f, 12.1f, 16.4f, 20.9f, 25.3f, 30.7f, 35.1f, 40.8f,
        45.7f, 51.3f, 56.9f, 62.4f, 68.1f, 74.3f, 80.2f, 86.9f
    };

    // Signed base weights. The sign pattern keeps the cluster from summing
    // into a single comb-filtered smear.
    constexpr float kErGainL[AmbienceEngine::kNumTaps] =
    {
         1.00f, -0.89f,  0.81f,  0.72f, -0.68f,  0.61f, -0.55f, -0.50f,
         0.46f, -0.41f,  0.37f, -0.33f, -0.30f,  0.27f, -0.24f,  0.21f
    };

    constexpr float kErGainR[AmbienceEngine::kNumTaps] =
    {
         0.94f,  0.86f, -0.78f,  0.70f,  0.64f, -0.58f,  0.53f, -0.48f,
        -0.44f,  0.39f,  0.35f, -0.31f,  0.28f, -0.25f,  0.22f, -0.19f
    };

    // Tail network delays, in ms at size = 1.0. Ratios are close to mutually
    // prime so the modes spread out instead of stacking.
    constexpr float kTailBaseMs[AmbienceEngine::kNumTailLines] =
    {
        21.73f, 26.51f, 31.97f, 37.41f, 43.79f, 50.33f, 57.91f, 65.27f
    };

    // Slow, mutually detuned modulation to break up metallic ringing.
    constexpr float kLfoRateHz[AmbienceEngine::kNumTailLines] =
    {
        0.091f, 0.127f, 0.163f, 0.211f, 0.247f, 0.293f, 0.331f, 0.379f
    };

    // Sign pattern for injecting the diffused send into the network.
    constexpr float kInjectSign[AmbienceEngine::kNumTailLines] =
    {
        1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f
    };

    constexpr float kDiffuserMsL[AmbienceEngine::kNumDiffusers] = {  5.31f,  8.17f, 11.93f, 17.29f };
    constexpr float kDiffuserMsR[AmbienceEngine::kNumDiffusers] = {  6.13f,  9.41f, 13.07f, 19.11f };
    constexpr float kDiffuserGain[AmbienceEngine::kNumDiffusers] = { 0.68f,  0.66f,  0.62f,  0.58f };

    // High frequencies decay this fraction as long as the Reverb Time
    // control's value, which is what keeps long settings from turning into a
    // bright metallic plate.
    constexpr float kHighFrequencyRatio = 0.35f;

    // Wet-path balance between the reflection cluster and the tail.
    constexpr float kEarlyLevel = 0.85f;
    constexpr float kTailLevel  = 0.75f;

    // How much of the reflection cluster feeds the tail, so the tail grows out
    // of the room rather than starting from nothing.
    constexpr float kSendToTail  = 0.75f;
    constexpr float kEarlyToTail = 0.35f;

    inline float sizeToScale (float size) noexcept
    {
        return 0.45f + size * 1.45f;   // 0.45x .. 1.90x
    }

    inline float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

//==============================================================================
void AmbienceEngine::prepare (double sampleRateIn, int /*maxBlockSize*/)
{
    sampleRate = sampleRateIn;

    const auto msToSamples = [this] (float ms) { return static_cast<int> (std::ceil (ms * 0.001f * sampleRate)) + 8; };

    preDelay.prepare (msToSamples (kMaxPreDelayMs));
    erLine.prepare   (msToSamples (kMaxErTapMs * kMaxSizeScale));

    for (std::size_t i = 0; i < kNumTailLines; ++i)
    {
        // Headroom above the longest modulated delay this line can reach.
        tailLine[i].prepare (msToSamples (kTailBaseMs[i] * kMaxSizeScale) + 64);
        tailAbsorb[i].reset();
        lfoPhase[i] = static_cast<float> (i) / static_cast<float> (kNumTailLines);
        lfoInc[i]   = kLfoRateHz[i] / static_cast<float> (sampleRate);
        tailDelay[i].prepare (sampleRate, 25.0f);
        tailFeedback[i].prepare (sampleRate, 60.0f);
        tailDamping[i].prepare (sampleRate, 60.0f);
    }

    for (std::size_t i = 0; i < kNumDiffusers; ++i)
    {
        diffuserL[i].prepare (static_cast<int> (kDiffuserMsL[i] * 0.001f * sampleRate) + 1, kDiffuserGain[i]);
        diffuserR[i].prepare (static_cast<int> (kDiffuserMsR[i] * 0.001f * sampleRate) + 1, kDiffuserGain[i]);
    }

    lfoDepthSamples = static_cast<float> (sampleRate) * 0.00005f;   // ~2.2 samples at 44.1 kHz

    sendHighPass.setCutoff (85.0f, sampleRate);
    sendLowPass.setCutoff (12500.0f, sampleRate);   // the SPX900 ran at 31.25 kHz

    inputGain.prepare  (sampleRate, 20.0f);
    outputGain.prepare (sampleRate, 20.0f);
    dryGain.prepare    (sampleRate, 20.0f);
    wetGain.prepare    (sampleRate, 20.0f);
    preDelaySamples.prepare (sampleRate, 60.0f);

    // Force a full recompute on the next parameter update.
    erSizeScaleUsed = erDecayUsed = tailSizeScaleUsed = tailTimeUsed = -1.0f;

    setParameters (params, true);
    reset();
}

void AmbienceEngine::reset()
{
    preDelay.reset();
    erLine.reset();
    sendHighPass.reset();
    sendLowPass.reset();
    dcL.reset();
    dcR.reset();

    for (std::size_t i = 0; i < kNumDiffusers; ++i)
    {
        diffuserL[i].reset();
        diffuserR[i].reset();
    }

    for (std::size_t i = 0; i < kNumTailLines; ++i)
    {
        tailLine[i].reset();
        tailAbsorb[i].reset();
        tailState[i] = 0.0f;
    }

    controlCounter = 0;
}

//==============================================================================
void AmbienceEngine::setParameters (const Parameters& p, bool snap)
{
    params = p;
    params.mix         = clampf (params.mix, 0.0f, 1.0f);
    params.decay       = clampf (params.decay, 0.0f, 1.0f);
    params.size        = clampf (params.size, 0.0f, 1.0f);
    params.preDelayMs  = clampf (params.preDelayMs, kMinPreDelayMs, kMaxPreDelayMs);
    params.reverbTimeS = clampf (params.reverbTimeS, kMinReverbTimeS, kMaxReverbTimeS);
    params.inputDb     = clampf (params.inputDb, kMinLevelDb, kMaxLevelDb);
    params.outputDb    = clampf (params.outputDb, kMinLevelDb, kMaxLevelDb);

    inputGain.setTarget  (dbToGain (params.inputDb));
    outputGain.setTarget (dbToGain (params.outputDb));
    dryGain.setTarget    (std::cos (params.mix * kPi * 0.5f));
    wetGain.setTarget    (std::sin (params.mix * kPi * 0.5f));

    preDelaySamples.setTarget (clampf (params.preDelayMs * 0.001f * static_cast<float> (sampleRate),
                                       1.0f, static_cast<float> (preDelay.capacity() - 2)));

    const float scale = sizeToScale (params.size);

    if (differs (scale, erSizeScaleUsed) || differs (params.decay, erDecayUsed))
        recomputeEarlyReflections();

    if (differs (scale, tailSizeScaleUsed) || differs (params.reverbTimeS, tailTimeUsed))
        recomputeTailGains();

    if (snap)
        snapSmoothers();
}

void AmbienceEngine::snapSmoothers()
{
    inputGain.setImmediate  (inputGain.getTargetValue());
    outputGain.setImmediate (outputGain.getTargetValue());
    dryGain.setImmediate    (dryGain.getTargetValue());
    wetGain.setImmediate    (wetGain.getTargetValue());
    preDelaySamples.setImmediate (preDelaySamples.getTargetValue());

    for (std::size_t i = 0; i < kNumTailLines; ++i)
    {
        tailDelay[i].setImmediate (tailBaseSamples[i]);
        tailFeedback[i].setImmediate (tailFeedback[i].getTargetValue());
        tailDamping[i].setImmediate (tailDamping[i].getTargetValue());
    }
}

void AmbienceEngine::recomputeEarlyReflections()
{
    const float scale = sizeToScale (params.size);
    erSizeScaleUsed = scale;
    erDecayUsed     = params.decay;

    // Decay maps to the time constant of the reflection envelope: a tight
    // 8 ms burst at 0, a 160 ms spray at 1.
    const float tauMs      = 8.0f * std::pow (20.0f, params.decay);
    const float tauSamples = tauMs * 0.001f * static_cast<float> (sampleRate);
    const float maxTap     = static_cast<float> (erLine.capacity() - 2);

    float energy = 0.0f;

    for (std::size_t i = 0; i < kNumTaps; ++i)
    {
        erTimeL[i] = clampf (kErTimeMsL[i] * scale * 0.001f * static_cast<float> (sampleRate), 1.0f, maxTap);
        erTimeR[i] = clampf (kErTimeMsR[i] * scale * 0.001f * static_cast<float> (sampleRate), 1.0f, maxTap);

        erGainL[i] = kErGainL[i] * std::exp (-erTimeL[i] / tauSamples);
        erGainR[i] = kErGainR[i] * std::exp (-erTimeR[i] / tauSamples);

        energy += erGainL[i] * erGainL[i] + erGainR[i] * erGainR[i];
    }

    // Normalise so the cluster holds a constant level as Decay is swept; the
    // control changes the character of the room, not how loud it is.
    const float norm = (energy > 1.0e-9f) ? std::sqrt (2.0f / energy) : 0.0f;

    for (std::size_t i = 0; i < kNumTaps; ++i)
    {
        erGainL[i] *= norm;
        erGainR[i] *= norm;
    }
}

void AmbienceEngine::recomputeTailGains()
{
    const float scale = sizeToScale (params.size);
    tailSizeScaleUsed = scale;
    tailTimeUsed      = params.reverbTimeS;

    const float rt60Samples = params.reverbTimeS * static_cast<float> (sampleRate);

    for (std::size_t i = 0; i < kNumTailLines; ++i)
    {
        const float lengthSamples = kTailBaseMs[i] * scale * 0.001f * static_cast<float> (sampleRate);
        tailBaseSamples[i] = clampf (lengthSamples, 4.0f, static_cast<float> (tailLine[i].capacity() - 4) - lfoDepthSamples);

        // -60 dB after one RT60, given this line's round-trip length.
        const float gDc = std::pow (10.0f, -3.0f * tailBaseSamples[i] / rt60Samples);

        // Same calculation for the shortened high-frequency decay; the ratio
        // of the two sets the absorbent filter's pole.
        const float gHf = std::pow (10.0f, -3.0f * tailBaseSamples[i] / (rt60Samples * kHighFrequencyRatio));
        const float ratio = clampf (gHf / std::fmax (gDc, 1.0e-12f), 1.0e-4f, 1.0f);
        const float b = clampf ((1.0f - ratio) / (1.0f + ratio), 0.0f, 0.85f);

        tailFeedback[i].setTarget (clampf (gDc, 0.0f, 0.9995f));
        tailDamping[i].setTarget (b);
    }
}

//==============================================================================
void AmbienceEngine::updateControlRate()
{
    for (std::size_t i = 0; i < kNumTailLines; ++i)
    {
        lfoPhase[i] += lfoInc[i] * static_cast<float> (kControlBlock);
        if (lfoPhase[i] >= 1.0f)
            lfoPhase[i] -= 1.0f;

        const float mod = std::sin (2.0f * kPi * lfoPhase[i]) * lfoDepthSamples;
        const float target = clampf (tailBaseSamples[i] + mod, 2.0f,
                                     static_cast<float> (tailLine[i].capacity() - 3));
        tailDelay[i].setTarget (target);
    }
}

void AmbienceEngine::process (float* left, float* right, int numSamples)
{
    const float maxPre = static_cast<float> (preDelay.capacity() - 2);

    for (int n = 0; n < numSamples; ++n)
    {
        if (controlCounter == 0)
            updateControlRate();

        controlCounter = (controlCounter + 1) % kControlBlock;

        const float inG = inputGain.next();
        const float dryL = left[n]  * inG;
        const float dryR = right[n] * inG;

        // The SPX900 is a mono-in, stereo-out box; keeping the send mono is
        // both authentic and phase-safe on stereo sources.
        const float send = 0.5f * (dryL + dryR);

        preDelay.write (send);
        const float delayed = preDelay.read (clampf (preDelaySamples.next(), 1.0f, maxPre));

        const float banded = sendLowPass.process (sendHighPass.process (delayed));

        // --- early reflections -------------------------------------------
        erLine.write (banded);

        float erL = 0.0f, erR = 0.0f;
        for (std::size_t i = 0; i < kNumTaps; ++i)
        {
            erL += erLine.read (erTimeL[i]) * erGainL[i];
            erR += erLine.read (erTimeR[i]) * erGainR[i];
        }

        // --- diffusion ------------------------------------------------------
        const float tailIn = banded * kSendToTail + 0.5f * (erL + erR) * kEarlyToTail;

        float dL = tailIn, dR = tailIn;
        for (std::size_t i = 0; i < kNumDiffusers; ++i)
        {
            dL = diffuserL[i].process (dL);
            dR = diffuserR[i].process (dR);
        }

        // --- late tail: 8x8 Householder feedback delay network --------------
        std::array<float, kNumTailLines> v {};
        float sum = 0.0f;

        for (std::size_t i = 0; i < kNumTailLines; ++i)
        {
            const float y = tailLine[i].read (tailDelay[i].next());
            v[i] = tailAbsorb[i].process (y, tailFeedback[i].next(), tailDamping[i].next());
            sum += v[i];
        }

        const float householder = sum * (2.0f / static_cast<float> (kNumTailLines));
        const float inject = 0.353553f;   // 1 / sqrt(8)

        for (std::size_t i = 0; i < kNumTailLines; ++i)
        {
            const float feed = ((i & 1) == 0 ? dL : dR) * kInjectSign[i] * inject;
            tailState[i] = flushDenormal (v[i] - householder + feed);
            tailLine[i].write (tailState[i]);
        }

        const float tailL = 0.5f * (tailState[0] - tailState[2] + tailState[4] - tailState[6]);
        const float tailR = 0.5f * (tailState[1] - tailState[3] + tailState[5] - tailState[7]);

        // --- wet / dry -------------------------------------------------------
        const float wetL = dcL.process (erL * kEarlyLevel + tailL * kTailLevel);
        const float wetR = dcR.process (erR * kEarlyLevel + tailR * kTailLevel);

        const float dg = dryGain.next();
        const float wg = wetGain.next();
        const float og = outputGain.next();

        left[n]  = (dryL * dg + wetL * wg) * og;
        right[n] = (dryR * dg + wetR * wg) * og;
    }
}

} // namespace spx
