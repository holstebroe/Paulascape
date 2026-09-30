# Paulascape

Paulascape is an MIT-licensed CLAP instrument plugin that loads a ProTracker MOD file and turns its samples into playable DAW instruments. It sounds like an Amiga 500, and its core engine is based on pt2-clone.

## Features

- **Paula sound engine:** 8-bit sample playback with period-to-rate calculation, BLEP anti-aliasing, A500/A1200 RC filters and the 2-pole LED filter. Clean (interpolated) mode is the alternative.
- **Playback modes:** single, multi-channel and drum instrument modes on a 32-voice pool with Amiga round-robin channels (stereo L R R L with adjustable separation, or four mono outputs), and pattern mode (C1 = pattern 0, C0 = whole song, looped while the key is held).
- **MIDI control:** velocity, pitch bend, mod wheel vibrato, channel pressure tremolo, CC 68 legato, CC 5 glide, CC 70-74 sample offset / retrigger / note cut / LED filter, CC 20-22 effect commands. All CC numbers can be changed.
- **Full ProTracker replayer** for pattern mode (all effects except EFx funk repeat), tempo scaled to the host.
- **WAV import** into any slot, converted to Amiga quality (16,574 Hz, 8-bit, cropped to 128 KB).
- **MIDI export:** a pattern-mode clip, or a note-by-note export (four tracks, tempo map, program changes, effect-command CCs) that plays back through the plugin.
- **ProTracker-style GUI** drawn into a software framebuffer with pt2-clone's font: front page with sample matrix and live channel scopes, settings page, MIDI mapping page. X11 and Win32 windows; macOS has no GUI yet.

See `PROJECT_PLAN.md` for what is done and what is still open.

## Building & Testing

### Requirements
- CMake 3.20+
- C++23 capable compiler (GCC 13+, Clang 16+, or MSVC 2022+)
- Linux: X11 development headers (`libx11-dev`); `xvfb` to run the window tests

### Build Instructions

```bash
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

### Deploying while developing

Set `CLAPTEST` to the folder your DAW scans for CLAP plugins, then build the `deploy_clap` target. It builds the plugin and copies `Paulascape.clap` there. It is not part of the default build, because a host that has the plugin loaded locks the file.

```bash
export CLAPTEST=~/.clap          # Windows: set CLAPTEST=C:\Program Files\Common Files\CLAP
cmake --build build --target deploy_clap
```

## License

- Paulascape is released under the [MIT License](LICENSE).
- Includes components from [pt2-clone](https://github.com/8bitbubsy/pt2-clone) under the BSD-3-Clause License (see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).
