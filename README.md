# SPX Ambience

A small, dense ambience reverb for macOS, voiced after the **Yamaha SPX900
"Ambience"** algorithm and the **Waves H-Reverb Ambience** preset: a thick
early-reflection cluster in front of a modest, slightly dark tail. It is built
for the short end — the 0.2–2 s room tone you put on drums, vocals and guitars
to make them sound like they were recorded somewhere — but the Reverb Time
control reaches all the way to 10 s when you want a wash.

Builds as **AU**, **VST3** and a **Standalone** app, as a universal
(Apple Silicon + Intel) binary.

```
┌──────────────────────────────────────────────────────────┐
│  SPX AMBIENCE                        ┌────────────────┐  │
│  DIGITAL AMBIENCE PROCESSOR          │  REV TIME 1.20s│  │
│                                      └────────────────┘  │
│  REVERB ─────────────────────────────────────────────────│
│     ( )         ( )         ( )         ( )              │
│  PRE-DELAY   REV TIME     DECAY        SIZE              │
│                                                          │
│  LEVELS ─────────────────────────────────────────────────│
│        ( )            ( )            ( )                 │
│      INPUT           MIX           OUTPUT                │
└──────────────────────────────────────────────────────────┘
```

## Controls

| Control | Range | What it does |
|---|---|---|
| **Input** | −24 … +24 dB | Level into the processor. Affects dry and wet together. |
| **Pre-Delay** | 0 … 250 ms | Gap between the dry signal and the first reflection. 10–30 ms keeps a vocal in front of the room; longer separates them. |
| **Rev Time** | 0.1 … 10 s | RT60 of the late tail. The control is skewed so most of the travel sits in the short ambience range. |
| **Decay** | 0 … 100 % | Shape of the **early reflection** envelope — how quickly the reflection cluster dies away. Low is a tight, close room; high is a longer, more open spray. This is a separate control from Rev Time, exactly as it is on the SPX900. |
| **Size** | 0 … 100 % | Room scale, 0.45× to 1.9×. Stretches both the reflection pattern and the tail network together, so the room grows rather than just getting longer. |
| **Mix** | 0 … 100 % | Dry/wet, equal power. At 0 % the dry signal passes through bit-identical, so the plugin is safe to leave inserted. |
| **Output** | −24 … +24 dB | Level out. |

Six factory programs are included (SPX Ambience, Tight Room, Drum Ambience,
Vocal Space, Wide Chamber, Long Hall), reachable from the host's program menu.

### Why Decay and Rev Time are separate

On the SPX900 the ambience algorithms expose both, and they do different jobs:
`DECAY` governs the early reflection cluster, `REV TIME` governs the tail
behind it. Keeping them apart is most of what makes the box sound like a room
rather than a reverb — you can have a tight, snappy cluster over a long tail,
or a long spray over almost no tail.

## Building

You need Xcode command line tools and CMake 3.22+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

JUCE 8.0.15 is fetched automatically on the first configure. If you already
have a JUCE checkout, point at it and skip the download:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/path/to/JUCE
```

`COPY_PLUGIN_AFTER_BUILD` is on, so a successful build installs to:

- `~/Library/Audio/Plug-Ins/Components/SPX Ambience.component`
- `~/Library/Audio/Plug-Ins/VST3/SPX Ambience.vst3`

The standalone app lands in
`build/SPXAmbience_artefacts/Release/Standalone/`.

Logic caches its plugin scan, so after the first install run:

```sh
killall -9 AudioComponentRegistrar
auval -v aufx Spxa Ccor
```

### Signing

An unsigned component loads fine locally. To load it on another machine
without Gatekeeper complaints, ad-hoc sign it at minimum:

```sh
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/"SPX Ambience.component"
```

## Testing the DSP

The reverb core in `Source/dsp/` has **no JUCE dependency**, so it builds and
runs anywhere:

```sh
cmake -S . -B build-test -DSPX_BUILD_PLUGIN=OFF
cmake --build build-test -j
ctest --test-dir build-test --output-on-failure
```

`tools/DspTest.cpp` measures, at 44.1 / 48 / 96 kHz:

- **RT60 tracking** — 500 Hz octave-band T30 by Schroeder backward
  integration, against the Reverb Time control, from 0.3 s to 10 s. Currently
  within 4 %.
- **Decay** spreading the reflection cluster without acting as a level control.
- **Size** pushing reflection energy later.
- **Pre-delay** offsetting the wet onset.
- **L/R decorrelation** of the impulse response.
- **Bypass transparency** at 0 % mix (bit-identical).
- **Stability** — a minute of silence after a full-scale burst into a 10 s
  tail, checking for NaN, runaway feedback and denormal stalls.

## How it works

```
in ─┬─────────────────────────────────────────────── dry ──┐
    │                                                       │
    └─ pre-delay ─ 85 Hz HP ─ 12.5 kHz LP ─┬─ 16-tap ER ──┬─┴─ mix ─ out
                                            │              │
                                            └─ 4 allpass ─ 8×8 FDN ┘
```

- **Band limiting.** The SPX900 ran at a 31.25 kHz sample rate. The wet path is
  rolled off at 12.5 kHz to keep that darker, slightly grainy character, with
  an 85 Hz highpass so long tails don't build up mud.
- **Early reflections.** Sixteen taps per channel, two decorrelated patterns,
  signed so the cluster doesn't comb-filter into a single smear. Tap gains are
  shaped by an exponential envelope whose time constant is the Decay control
  (8 ms to 160 ms), then normalised to constant energy so Decay changes the
  room and not the level.
- **Diffusion.** Four series Schroeder allpasses per channel ahead of the tail,
  which is what builds density fast enough to sound like a real small room.
- **Late tail.** An 8-line feedback delay network with an orthogonal
  Householder mixing matrix and slow, mutually detuned modulation (~2 samples)
  to break up metallic ringing.
- **Damping.** Each line uses a Jot-style absorbent filter that folds the
  feedback gain and the high-frequency damping into one element: DC gain sets
  the requested RT60, and the pole sets high frequencies to decay at 0.35× that
  time. A plain lowpass in the loop would also attenuate low frequencies on
  every pass — that costs about half the requested decay time, which is exactly
  what the RT60 test caught during development.
- **Mono send.** Like the hardware, the reverb is fed from a mono sum. It keeps
  the reflection pattern stable and avoids phase problems on stereo sources;
  the returns are stereo.

## Licence

JUCE 8 is licensed under AGPLv3 or a commercial JUCE licence. (JUCE 8 no
longer has a splash screen, so there is no flag to set either way.) Building
this for your own use is unaffected. If you want to distribute a binary, you
either release your source under the AGPL or buy a JUCE licence.
