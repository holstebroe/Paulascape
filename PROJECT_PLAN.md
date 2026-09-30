# Paulascape Project Plan & Implementation Progress

This document tracks implementation progress against the design goals in `Paulascape_design.md`.

## Status

An audit against `Paulascape_design.md` (2026-09-30) found the earlier "all milestones complete" claim premature.
What exists is an engine skeleton plus static GUI renderers. The plugin has **no window**, cannot load a MOD,
and most design features are stubs. Legend: [x] done, [~] partial, [ ] missing.

## Audit: design vs. code

| Area | State | Gap |
| --- | --- | --- |
| Build, CMake, deploy_clap, CI | [x] | `deploy_clap` now copies to `%CLAPTEST%` (Acidus style). No clap-validator step, no release job on `v*.*.*` tags, no test MODs/reference renders/tools |
| Core: MOD loader, tables, Paula voice, BLEP, RC filters | [~] | Unit-tested in isolation; not compared with pt2-clone renders; no PAL/NTSC clock setting, no <31.4 kHz oversampling |
| Voice pool | [~] | Round-robin, legato by slot/CC flag exist. Missing: multi-channel and drum routing by MIDI channel / in-key, velocity->Cxx check, note-off fade in clean mode, 4-mono output, CC 5 glide |
| MIDI effects / CC mapping | [ ] | No CC handling in the plugin at all (mod wheel, CC 68/70-74/20-22, pressure, pitch bend events ignored) |
| Replayer (pattern mode) | [~] | Only Bxx, Cxx, Dxx, Fxx implemented. Missing 0xy, 1xx, 2xx, 3xx, 4xy, 5xy, 6xy, 7xy, 9xx, Axy and all Exy; no loop-while-held/newest-wins verification, no song-order key verification, rows-per-beat / start-speed tempo math to verify |
| CLAP wrapper | [~] | Params: only 7 of 12 exposed and they are read only via events (flush is empty, no GUI->host param changes). No audio-ports-config (4-mono needs it), no `clap.gui`, no `clap.latency`/`tail`. State saves only the MOD title: no samples, settings or slot settings |
| MOD loading | [ ] | Nothing calls `ModLoader` from the plugin; no open dialog, no drag and drop |
| WAV import | [~] | Converter done; not reachable from the UI (no drop, result not stored in state) |
| MIDI export | [~] | Writer exists; no UI icon, no drag-out, no effect CC verification against plugin playback |
| GUI | [~] | (see milestones A-D for what is now done) Originally: renderers only draw hard-coded text. No native window (X11/Win32/Cocoa), no `clap.gui`, no mouse/keyboard input, no scale, no real ProTracker bitmaps/bevels, settings screen shows constants, sample matrix ignores mode, only 12 of 31 rows, scopes not fed by audio |
| Docs/release | [ ] | README overstates features; no tagged release flow |

## Milestones

### Done
- [x] M1 Repo setup, CMake C++23, CLAP submodule, CI, `deploy_clap`
- [x] M2 MOD loader and pt2 tables/font data
- [x] M3 Paula voice, BLEP, RC filters (unit tested)
- [x] M4 Voice pool basics
- [x] M6 WAV converter (engine side only)
- [x] M7 MIDI writer (basic)

### Open (work order)
- [x] **A. Usable plugin shell:** `clap.gui` with native X11 and Win32 windows (Win32 code is untested: no Windows machine here), framebuffer blit at 1x-3x, mouse input, GUI/audio state sharing with a mutex and snapshot. *Open:* Cocoa window (macOS has no GUI yet), keyboard input, XDND file drop on X11, host-thread-safe `request_resize`
- [x] **B. Front page that works:** open MOD dialog, drop (Win32), 31-row scrollable matrix with per-mode columns, select/loop/volume/finetune/legato/in/out key editing, instrument/pattern and sub-mode buttons, live scopes, real pt2-clone font. *Open:* pt2-clone bevel bitmaps/palette, MIDI drag-out, WAV drop on X11
- [x] **C. Settings screen:** all sound settings editable. *Open:* Clock (PAL/NTSC), MIDI mapping table (needs F)
- [x] **D. State and params:** all 12 params with flush, text conversion, enum flags; state saves params, slot settings and the whole module (samples, patterns). *Open:* host notification of GUI changes beyond `request_flush` is untested in a real host
- [ ] **E. Modes:** multi-channel and drum routing, 4-mono + audio-ports-config, clean-mode fade, PAL/NTSC
- [ ] **F. MIDI effects:** CC/pitch bend/pressure/mod wheel handling, tick-based per-voice effects, CC 5 glide, CC 68
- [ ] **G. Replayer:** all ProTracker effects; verify loop/newest-wins/song key; tempo math
- [ ] **H. WAV import in UI:** drop on slot, store in state
- [ ] **I. MIDI export in UI:** icon, save/drag-out, effect CC round trip test
- [ ] **J. Validation:** clap-validator in CI, pt2-clone reference renders, test MODs, tools/
- [ ] **K. Release:** tag workflow, README corrected

## Progress log
- 2026-09-30: audit, plan rewritten, `deploy_clap` target added.
- 2026-09-30: milestones A-D implemented: plugin state/API rewrite, UI, X11/Win32 windows, `clap.gui`. Tests now keep `assert` in Release (they were no-ops). New tests use `test/mods/BEDROCK.MOD`; `gui_window_x11_test_xvfb` runs under Xvfb. Replaced the stub font with pt2-clone's real one.
