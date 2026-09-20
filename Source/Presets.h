#pragma once

#include "dsp/AmbienceEngine.h"

#include <array>
#include <cstddef>

namespace spx
{

/** A factory program, in the units the controls display.

    Shared by the plugin's program list and the offline renderer so the two
    cannot drift apart: a render is exactly what the corresponding preset
    sounds like in a host.
*/
struct Preset
{
    const char* name;
    float inputDb;        // dB
    float preDelayMs;     // ms
    float reverbTimeS;    // s
    float decayPercent;   // %
    float sizePercent;    // %
    float mixPercent;     // %
    float outputDb;       // dB

    // Wet-path tone controls. Every factory program leaves these flat, so a
    // preset sounds exactly as it did before the EQ existed.
    float lowEqHz = 120.0f;
    float lowEqGainDb = 0.0f;
    bool  lowEqPass = false;
    float highEqHz = 8000.0f;
    float highEqGainDb = 0.0f;
    bool  highEqPass = false;
};

inline constexpr std::array<Preset, 6> kPresets
{ {
    //  name              in    pre-dly  time   decay  size   mix    out
    { "SPX Ambience",    0.0f,  12.0f,  1.20f,  45.0f, 40.0f, 30.0f, 0.0f },
    { "Tight Room",      0.0f,   4.0f,  0.45f,  18.0f, 18.0f, 26.0f, 0.0f },
    { "Drum Ambience",   0.0f,  18.0f,  0.90f,  62.0f, 34.0f, 38.0f, 0.0f },
    { "Vocal Space",     0.0f,  34.0f,  1.80f,  40.0f, 52.0f, 24.0f, 0.0f },
    { "Wide Chamber",    0.0f,  22.0f,  3.20f,  70.0f, 72.0f, 32.0f, 0.0f },
    { "Long Hall",       0.0f,  45.0f,  7.00f,  80.0f, 92.0f, 28.0f, 0.0f }
} };

inline constexpr std::size_t kNumPresets = kPresets.size();

/** Converts a preset's display units into the engine's parameter block. */
inline AmbienceEngine::Parameters toEngineParameters (const Preset& preset) noexcept
{
    AmbienceEngine::Parameters p;
    p.inputDb     = preset.inputDb;
    p.outputDb    = preset.outputDb;
    p.mix         = preset.mixPercent * 0.01f;
    p.preDelayMs  = preset.preDelayMs;
    p.reverbTimeS = preset.reverbTimeS;
    p.decay       = preset.decayPercent * 0.01f;
    p.size        = preset.sizePercent * 0.01f;

    using Mode = AmbienceEngine::BandMode;
    p.lowEqHz      = preset.lowEqHz;
    p.lowEqGainDb  = preset.lowEqGainDb;
    p.lowEqMode    = preset.lowEqPass ? Mode::pass : Mode::shelf;
    p.highEqHz     = preset.highEqHz;
    p.highEqGainDb = preset.highEqGainDb;
    p.highEqMode   = preset.highEqPass ? Mode::pass : Mode::shelf;
    return p;
}

} // namespace spx
