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
