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
| Core: MOD loader, tables, Paula voice, BLEP, RC filters | [x] | Voice/BLEP/filters were approximations; now ports verified against pt2-clone output (see J). Low sample rates and periods below 113 are handled by internal oversampling |
| Voice pool | [x] | Was partial; see E and F |
| MIDI effects / CC mapping | [x] | Was missing entirely; see F |
| Replayer (pattern mode) | [x] | Was: only Bxx, Cxx, Dxx, Fxx. Now all effects (see G) |
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
- [x] **E. Modes:** multi-channel and drum routing (unmapped drum keys silent), selected slot in single mode, 4-mono output with `audio-ports-config` and one mono port per Amiga channel (GUI change triggers host rescan/restart), 2 ms clean-mode release fade (authentic cuts hard), clean mode skips the Amiga filters, PAL/NTSC clock, pitch mapping fixed (MIDI 60 = C-3 = period 214, extended by octaves outside the C-1..B-3 table, real Free mode). *Open:* "asked on first open" layout prompt; sub-31 kHz oversampling; NTSC timing of pattern mode
- [x] **F. MIDI effects:** CC 1/76 vibrato, pressure tremolo, pitch bend (stepped on ticks in table mode, smooth in Free), CC 68 legato, CC 5 glide (tone portamento), CC 70 offset, CC 71 retrigger, CC 72 cut, CC 74 LED, CC 20-22 effect commands (applied to the sounding voice and to a note arriving within 2 ticks), CC 120/123; effect ticks follow host tempo; every CC number editable on a MIDI mapping page. *Open:* the pattern replayer and voice pool still carry two copies of the per-tick effect code (shared tables only); note expressions; MIDI 2.0
- [x] **G. Replayer:** all ProTracker effects except EFx funk repeat and E8x (unused): arpeggio, slides, tone porta, vibrato/tremolo + waveforms, volume slides, 9xx, Bxx/Dxx/Fxx, E1-EE incl. loop, delay, retrigger, cut, pattern delay. Fixed sample-loop end bug in the Paula voice. Tests cover each effect, tempo scaling and a 55 s render of BEDROCK. *Open:* compare against pt2-clone reference renders (J)
- [~] **H. WAV import in UI:** "Import WAV" button (file dialog) and Win32 file drop onto a slot row, result stored in state. *Open:* XDND drop on X11 (and MOD drop there), Cocoa drop
- [~] **I. MIDI export in UI:** "MIDI" (pattern-mode clip) and "Notes" (full export) buttons with save dialog. Shared song unroller (Bxx/Dxx/E6x/EEx), tempo map, program changes, velocity from Cxx, EDx/ECx timing, tone portamento as a legato overlap, effect CCs with E-effect encoding and clear triples. Round trip through the voice pool matches the replayer tick by tick (period, volume, sample, retriggers, offsets) on BEDROCK, JULEMAND and on a module exercising every exported effect; slides go out as pitch bend (RPN 0 range 48), with events at a shared time in file order and notes-first order (`midi_roundtrip_test`). *Open:* drag-out is done (X11 XDND source tested against a mock target; Win32 OLE source untested; no macOS), generic non-Paulascape export, 0xy via legato overlap
- [~] **J. Validation:** `clap_host_test` loads the built `.clap` and drives lifecycle, params, ports, events, state, GUI embedding (under Xvfb in ctest). Tests keep `assert` in Release. The Paula voice, BLEP table and A500/A1200/LED filters are now ports of pt2-clone's `pt2_paula.c`/`pt2_blep.c`/`pt2_rcfilters.c`; `paula_reference_test` compares 12 renders (4 periods x A500, A500+LED, A1200) with output of pt2-clone's own code (`test/resources/paula`, regenerated with `tools/paula_reference`): max difference below 4e-4, period 124 bit-exact. Periods below 113 use internal oversampling (pt2-clone clamps there). *Open:* whole-MOD reference renders (`pt2_mod2wav`), clap-validator in CI (not reachable from this session)
- [~] **K. Release:** CI builds and tests on three OSes, uploads per-OS artifacts and zips them into a GitHub release on `v*.*.*` tags; README corrected. *Open:* a tagged release has not been exercised; Windows build untested by the author of this change
- [ ] **L. Known gaps to decide:** velocity maps to Cxx volume as in the design, so the slot "Vol" column only affects pattern mode; layout is not asked on first open; macOS GUI; keyboard input; undo of slot edits; `request_resize` is called from the GUI thread

## Progress log
- 2026-09-30: audit, plan rewritten, `deploy_clap` target added.
- 2026-09-30: milestones A-D implemented: plugin state/API rewrite, UI, X11/Win32 windows, `clap.gui`. Tests now keep `assert` in Release (they were no-ops). New tests use `test/mods/BEDROCK.MOD`; `gui_window_x11_test_xvfb` runs under Xvfb. Replaced the stub font with pt2-clone's real one.
- 2026-09-30: milestone G done (full replayer, tests).
- 2026-09-30: milestones E and F done (voice pool rewrite, MIDI events, 4-mono ports, MIDI map page, voice pool tests).
- 2026-09-30: milestones H-K partly done: MIDI export rewrite with round-trip test, host test, CI release job, README.
- 2026-09-30: Paula voice, BLEP and filters replaced by ports of pt2-clone; found and fixed a 2x pitch error while building the reference comparison.
- 2026-09-30: user feedback round: WAV import names the slot from the file; non-overlapping notes stay on one Amiga channel; sample wave/loop panel; note names follow the DAW convention (song key C-0 = MIDI 12, patterns from C-1 = MIDI 24); pattern and sample rows play while the mouse is held; MIDI and Notes buttons are drag sources (right-click saves a file).
