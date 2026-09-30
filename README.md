# Paulascape

Paulascape is an MIT-licensed CLAP instrument plugin that loads a ProTracker MOD file and turns its samples into playable DAW instruments. It sounds like an Amiga 500, and its core engine is based on pt2-clone.

## Features

- **Paula Sound Engine:** 8-bit DMA sample playback with period-to-rate calculation, BLEP anti-aliased synthesis, A500/A1200 RC filters, and 2-pole LED low-pass filter.
- **Playback Modes:**
  - **Instrument Modes:** Single (Channel 1), Multi-channel (Channel $n$ plays sample slot $n$), and Drum Map mode.
  - **Pattern Mode:** Loops MOD patterns from MIDI keys (C1 = pattern 0, C0 = full song order playback).
- **32-Voice Pool:** Amiga 4-channel round-robin voice distribution with hard stereo panning (L R R L) or 4-mono output routing.
- **Resampler Modes:** Authentic (Paula + BLEP) and Clean (linear interpolation).
- **MIDI Effects & Expressiveness:** CC 68 legato footswitch, pitch bend portamento, velocity volume scaling, and CC 20–22 effect command encoding.
- **WAV Sample Import:** Drag/drop sample import converted to Amiga quality (16,574 Hz, 8-bit signed mono, cropped to 128 KB, with `smpl` chunk loop parsing).
- **MIDI Export:** Song unrolling and pattern export to standard `.mid` files with CC 20–22 effect command nibble encoding.
- **ProTracker UI:** Software pixel framebuffer GUI using pt2-clone 8x8 font, sample matrix, 4-channel oscilloscope scope displays, and settings page.

## Building & Testing

### Requirements
- CMake 3.20+
- C++23 capable compiler (GCC 13+, Clang 16+, or MSVC 2022+)

### Build Instructions

```bash
# Configure CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build plugin and test executables
cmake --build build --config Release

# Run test suite
ctest --test-dir build --output-on-failure
```

## License

- Paulascape is released under the [MIT License](LICENSE).
- Includes components from [pt2-clone](https://github.com/8bitbubsy/pt2-clone) under the BSD-3-Clause License (see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).
