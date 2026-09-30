# Paulascape Project Plan & Implementation Progress

This document tracks implementation progress against the design goals in `Paulascape_design.md`.

## Milestones & Status

- [x] **Milestone 1: Repository Setup & Build Infrastructure**
  - CMake build system configured with C++23.
  - CLAP SDK integrated.
  - Test framework initialized with `ctest`.
  - Github Actions CI workflow.

- [x] **Milestone 2: Core Data Structures & MOD Loader**
  - ProTracker 2 period tables, finetune tables, vibrato sine, font, and UI bitmaps ported (`src/core/pt2/`).
  - Parser for 31-sample and legacy 15-sample ProTracker MOD files (`src/core/mod_loader.cpp`).
  - Unit tests verified (`src/mod_loader_test_main.cpp`).

- [x] **Milestone 3: Paula Voice, BLEP Synthesis & RC Filters**
  - 8-bit signed sample playback with period-to-rate calculation (`src/core/paula_voice.cpp`).
  - BLEP synthesis for anti-aliased authentic Amiga output (`src/core/blep.cpp`).
  - Linear/interpolated clean mode resampler.
  - A500/A1200 low-pass / high-pass RC filter model and 2-pole LED filter (`src/core/rc_filters.cpp`).
  - Unit tests verified (`src/paula_voice_test_main.cpp`).

- [x] **Milestone 4: Voice Pool & Polyphonic Routing Engine**
  - 32-voice allocation manager (`src/core/voice_pool.cpp`).
  - Amiga 4-channel round-robin voice distribution.
  - Stereo panning (hard L R R L / adjustable stereo separation) and 4-mono output bus layouts.
  - Legato mode and CC 68 / CC 5 handling.
  - Unit tests verified (`src/voice_pool_test_main.cpp`).

- [x] **Milestone 5: ProTracker Replayer Engine**
  - Tick-based replayer processing standard ProTracker 2 effects (`src/core/replayer.cpp`).
  - Pattern playback and song order traversal.
  - Scaled host tempo synchronization (host tempo / MOD starting tempo).
  - Unit tests verified (`src/replayer_test_main.cpp`).

- [x] **Milestone 6: WAV Sample Importer**
  - Mono downmixing, resampling to 16,574 Hz, 8-bit signed quantization, 128 KB crop (`src/import/wav_import.cpp`).
  - Extraction of loop points and root pitch from `smpl` chunk.
  - Unit tests verified (`src/import/wav_import_test_main.cpp`).

- [x] **Milestone 7: MIDI Export Engine**
  - Pattern note export & full song timeline unrolling to MIDI tracks (`src/export/midi_export.cpp`).
  - Effect command encoding on CC 20–22.
  - Unit tests verified (`src/export/midi_export_test_main.cpp`).

- [x] **Milestone 8: CLAP Plugin Wrapper & State Management**
  - CLAP plugin entry point, descriptor, and factory (`src/clap/clap_entry.cpp`).
  - Sound settings & CC mapping parameter interface (`src/clap/paulascape_plugin.cpp`).
  - Audio rendering & note event handling.
  - State serialization/deserialization.
  - Unit tests verified (`src/clap/clap_plugin_test_main.cpp`).

- [x] **Milestone 9: Software Framebuffer & GUI Screens**
  - Software framebuffer renderer (`src/gui/framebuffer.cpp`) and pt2-clone 8x8 font engine (`src/gui/font_renderer.cpp`).
  - Scope visualizer for 4 Amiga channels (`src/gui/scopes.cpp`).
  - Front Page UI (`src/gui/front_page.cpp`).
  - Settings Screen UI (`src/gui/settings_screen.cpp`).
  - Unit tests verified (`src/gui/gui_render_test_main.cpp`).
