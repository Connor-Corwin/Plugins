#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Small, dependency-free DSP building blocks used by the ambience engine.
// Nothing in this file may include JUCE: the reverb core is built and tested
// standalone (see tools/DspTest.cpp).

namespace spx
{

constexpr float kDenormalFloor = 1.0e-20f;

inline float flushDenormal (float v) noexcept
{
    return (std::fabs (v) < kDenormalFloor) ? 0.0f : v;
}

/** True when two control values differ enough to be worth acting on. Used for
    the engine's "has this parameter changed since the last recompute" caches,
    where an exact == comparison would be both fragile and a warning. */
inline bool differs (float a, float b, float tolerance = 1.0e-9f) noexcept
{
    return std::fabs (a - b) > tolerance;
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

inline int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

//==============================================================================
/** Power-of-two circular buffer with linear-interpolated fractional reads. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        const int size = nextPowerOfTwo (maxDelaySamples + 4);
        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void write (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[static_cast<size_t> (writePos)] = x;
    }

    float readInt (int delaySamples) const noexcept
    {
        const int idx = (writePos - delaySamples) & mask;
        return buffer[static_cast<size_t> (idx)];
    }

    float read (float delaySamples) const noexcept
    {
        const int   i0   = static_cast<int> (delaySamples);
        const float frac = delaySamples - static_cast<float> (i0);
        const int   a    = (writePos - i0) & mask;
        const int   b    = (a - 1) & mask;
        const float ya   = buffer[static_cast<size_t> (a)];
        const float yb   = buffer[static_cast<size_t> (b)];
        return ya + frac * (yb - ya);
    }

    int capacity() const noexcept { return static_cast<int> (buffer.size()); }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

//==============================================================================
/** One-pole lowpass. Unity gain at DC, so it can sit inside a feedback loop
    without disturbing the loop's low-frequency decay time. */
class OnePoleLP
{
public:
    void setCutoff (float hz, double sampleRate) noexcept
    {
        const float fc = std::fmin (hz, static_cast<float> (sampleRate) * 0.49f);
        a = 1.0f - std::exp (-2.0f * 3.14159265358979f * fc / static_cast<float> (sampleRate));
    }

    void reset() noexcept { z = 0.0f; }

    float process (float x) noexcept
    {
        z += a * (x - z);
        z = flushDenormal (z);
        return z;
    }

private:
    float a = 1.0f, z = 0.0f;
};

//==============================================================================
/** One-pole highpass, built as input minus its own lowpass. */
class OnePoleHP
{
public:
    void setCutoff (float hz, double sampleRate) noexcept { lp.setCutoff (hz, sampleRate); }
    void reset() noexcept { lp.reset(); }
    float process (float x) noexcept { return x - lp.process (x); }

private:
    OnePoleLP lp;
};

//==============================================================================
/** DC blocker for the wet path. */
class DCBlocker
{
public:
    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + 0.9975f * y1;
        x1 = x;
        y1 = flushDenormal (y);
        return y1;
    }

private:
    float x1 = 0.0f, y1 = 0.0f;
};

//==============================================================================
/** Unit-gain Schroeder allpass with a fixed integer delay, used for input
    diffusion ahead of the tail network. */
class AllPass
{
public:
    void prepare (int delaySamples, float gain)
    {
        length = delaySamples;
        g = gain;
        line.prepare (delaySamples);
    }

    void reset() noexcept { line.reset(); }

    float process (float x) noexcept
    {
        const float delayed = line.readInt (length);
        const float w = flushDenormal (x + g * delayed);
        line.write (w);
        return delayed - g * w;
    }

private:
    DelayLine line;
    int length = 1;
    float g = 0.5f;
};


//==============================================================================
/** Jot-style absorbent feedback element for a delay network line.

    Combines the line's feedback gain and its high-frequency damping into one
    filter whose DC gain is `gain` and whose Nyquist gain is
    `gain * (1 - b) / (1 + b)`. Folding the two together is what lets the
    reverb time control stay accurate: a plain lowpass in the loop would also
    attenuate low frequencies on every pass, shortening the decay well below
    the requested time.
*/
class AbsorbentFeedback
{
public:
    void reset() noexcept { z = 0.0f; }

    float process (float x, float gain, float b) noexcept
    {
        z = flushDenormal ((1.0f - b) * x + b * z);
        return gain * z;
    }

private:
    float z = 0.0f;
};


//==============================================================================
/** Transposed direct form II biquad, with RBJ cookbook coefficients.

    Used for the wet-path tone controls. TDF2 is the usual choice when
    coefficients are recalculated while audio is running, as they are here
    when a frequency or gain knob is swept: its state variables stay bounded
    and it degrades gracefully across a coefficient change.
*/
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    /** Straight through, for when a band is at 0 dB and can be skipped. */
    void setBypass() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    void setLowShelf (float freqHz, float gainDb, double sampleRate) noexcept
    {
        const float A = std::pow (10.0f, gainDb * 0.025f);      // sqrt of the linear gain
        const float w0 = twoPi * freqHz / static_cast<float> (sampleRate);
        const float cosW = std::cos (w0);
        const float alpha = std::sin (w0) * 0.5f * 1.41421356f; // shelf slope S = 1
        const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;

        normalise (        A * ((A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha),
                    2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW),
                           A * ((A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha),
                               ((A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha),
                   -2.0f *     ((A - 1.0f) + (A + 1.0f) * cosW),
                               ((A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha));
    }

    void setHighShelf (float freqHz, float gainDb, double sampleRate) noexcept
    {
        const float A = std::pow (10.0f, gainDb * 0.025f);
        const float w0 = twoPi * freqHz / static_cast<float> (sampleRate);
        const float cosW = std::cos (w0);
        const float alpha = std::sin (w0) * 0.5f * 1.41421356f;
        const float twoSqrtAAlpha = 2.0f * std::sqrt (A) * alpha;

        normalise (         A * ((A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha),
                    -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW),
                            A * ((A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha),
                                ((A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha),
                     2.0f *     ((A - 1.0f) - (A + 1.0f) * cosW),
                                ((A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha));
    }

    void setHighPass (float freqHz, float q, double sampleRate) noexcept
    {
        const float w0 = twoPi * freqHz / static_cast<float> (sampleRate);
        const float cosW = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);
        const float oneCos = 1.0f + cosW;

        normalise (oneCos * 0.5f, -oneCos, oneCos * 0.5f,
                   1.0f + alpha, -2.0f * cosW, 1.0f - alpha);
    }

    void setLowPass (float freqHz, float q, double sampleRate) noexcept
    {
        const float w0 = twoPi * freqHz / static_cast<float> (sampleRate);
        const float cosW = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);
        const float oneMinusCos = 1.0f - cosW;

        normalise (oneMinusCos * 0.5f, oneMinusCos, oneMinusCos * 0.5f,
                   1.0f + alpha, -2.0f * cosW, 1.0f - alpha);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }

private:
    static constexpr float twoPi = 6.28318530717959f;

    void normalise (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        const float inv = 1.0f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
/** One-pole parameter smoother. Reaches ~63% of the target in `timeMs`. */
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        a = 1.0f - std::exp (-1000.0f / (timeMs * static_cast<float> (sampleRate)));
    }

    void setImmediate (float v) noexcept { current = target = v; }
    void setTarget (float v) noexcept { target = v; }
    float getCurrent() const noexcept { return current; }
    float getTargetValue() const noexcept { return target; }

    float next() noexcept
    {
        current += a * (target - current);
        return current;
    }

private:
    float a = 1.0f, current = 0.0f, target = 0.0f;
};

} // namespace spx
