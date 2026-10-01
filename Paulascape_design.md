# Paulascape: Design Document

Søren Holstebroe · 2026-09-30

## Overview

Paulascape is an MIT-licensed CLAP instrument plugin that loads a ProTracker MOD file and turns its samples into playable DAW instruments. It sounds like an Amiga 500, and its engine is based on pt2-clone.

**Goals**

- Play MOD samples from MIDI in single, multi-channel, drum and pattern modes.
- Offer authentic Amiga output (Paula aliasing, A500/A1200 filters, hard panning), or a clean mode as the alternative.
- Map MIDI CCs, mod wheel and pitch bend to MOD effects.
- Use a ProTracker look and feel.
- Advanced: export the song as MIDI the DAW can play back through the plugin.

**Non-goals**

- Writing or saving modified .mod files. The plugin is for DAW use, not for tracker editing.
- Formats beyond ProTracker MOD (XM, S3M, IT, MED), at least for v1.
- Plugin formats other than CLAP (no VST3 or AU).

## Decision log

| Date | Decision | Why |
| --- | --- | --- |
| 2026-09-30 | Name: Paulascape | Built on Paula, the Amiga sound chip |
| 2026-09-30 | MIT license | Same as Acidus; pt2-clone's BSD-3 allows it |
| 2026-09-30 | CLAP only | |
| 2026-09-30 | Project layout follows [Acidus](https://github.com/holstebroe/Acidus) | Raw CLAP SDK, own pixel GUI, CMake, GitHub Actions |
| 2026-09-30 | Latest C++ (C++23), not Acidus's C++17 | New project, no legacy constraint |
| 2026-09-30 | Base the engine on pt2-clone | The most accurate ProTracker 2 replayer and Paula emulation available |
| 2026-09-30 | MIDI note-off cuts the note | MOD has no note-off, so looped samples would otherwise play forever |
| 2026-09-30 | 32-voice pool in every instrument mode | Plenty; keeping to four voices is left to the user |
| 2026-09-30 | Voices go round-robin to the 4 outputs, several per output allowed | Simple, and spreads voices like a tracker |
| 2026-09-30 | Legato per slot, switchable live with CC 68 | Exact arpeggios and slides without retriggering |
| 2026-09-30 | Four channel scopes in v1 | |
| 2026-09-30 | Authentic vs clean resampler is a setting | Choose between Paula character and clean output |
| 2026-09-30 | Imported samples converted to Amiga quality, cropped at 128 KB | Imports should sound like native MOD samples |
| 2026-09-30 | No writing of .mod files | The plugin is for DAW use |
| 2026-09-30 | ProTracker look and feel, front page plus a settings screen | Familiar to the target audience |
| 2026-09-30 | Switch between instrument mode and pattern mode | Lets a MOD also be used as a pattern and loop source |
| 2026-09-30 | Pattern mode loops a pattern while its key is held; the newest key wins | Play patterns like an instrument; no layering |
| 2026-09-30 | A song-order key outside the pattern-key range plays the whole song | Quick way to hear or use the full track |
| 2026-09-30 | MOD timing is scaled by host tempo ÷ the MOD's starting tempo | Follows the host, and in-song tempo and speed changes such as shuffle keep their feel |
| 2026-09-30 | MIDI export effect commands on CC 20–22 | Unassigned CCs; nibble parameters fit 7-bit values |

## Engine

The engine is a port of [pt2-clone](https://github.com/8bitbubsy/pt2-clone) (BSD-3-Clause, © Olav Sørensen), not a library we link. Its code is written around one app with four fixed voices, so we lift the parts we need into reentrant C++ classes.

**What pt2-clone gives us**

| File | What it does | How we use it |
| --- | --- | --- |
| `pt2_paula.c` | Paula voice: 8-bit DMA fetch, period-to-rate, volume, BLEP synthesis (by aciddose) | Core of our voice class |
| `pt2_blep.c` | Band-limited step synthesis for authentic, alias-shaped output | Authentic resampler mode |
| `pt2_rcfilters.c` | A500/A1200 RC low-pass and high-pass, 2-pole "LED" filter | Filter model setting |
| `pt2_replayer.c` | ProTracker 2 effect and tick logic (about 1,900 lines) | Per-voice effects, pattern mode |
| `pt2_module_loader.c` | MOD parsing | Loading MODs |
| `pt2_tables.c` | Period tables, finetune, vibrato sine | Pitch and effects |
| `pt2_scopes.c` | Channel scopes | Scope display |
| `src/gfx/*.c` | UI bitmaps and font as C arrays | ProTracker look (see UI) |

**Changes the port needs**

- **Instance state:** Paula, filters and replayer are file-level `static` globals. Two plugin instances in one DAW would share them, so all state moves into an engine object.
- **Voice count:** pt2-clone has four voices (`PAULA_VOICES`). We need a pool of 32 voices, each with its own BLEP state.
- **Filters per output bus:** on a real Amiga the filters sit after the channel mix. We run one filter chain on the stereo bus, or one per output in 4-mono mode.
- **Sample rate:** Paula emulation needs at least 31,389 Hz output. Below that (rare in DAWs) we oversample internally.
- **No SDL or UI coupling in the audio path:** the audio code is used without pt2-clone's app shell.

License: BSD-3 lets us ship under MIT, provided pt2-clone's copyright notice and license text are kept in the source and shipped with the binaries.

## Playback modes

A switch at the top sets **instrument mode** or **pattern mode**. Instrument mode has three sub-modes. Every instrument mode shares a pool of 32 voices; keeping to four is left to the user.

| Mode | MIDI channels | What a key does | Voices |
| --- | --- | --- | --- |
| Single | Channel 1 | Plays the selected sample at the key's pitch | 32 |
| Multi-channel | Channel n plays sample slot n | Plays that sample at the key's pitch | 32 |
| Drum | Channel 1 | Input key picks a sample and plays it at its output key's pitch | 32 |
| Pattern | Channel 1 | Loops a pattern while the key is held | The MOD's own 4 channels |

**Voice routing**

Each new voice takes the next of four Amiga channels in turn (1, 2, 3, 4, 1 …), and several voices can share a channel. In the 4-mono layout channel n goes to output n. In stereo the channels are panned L R R L as on the Amiga, so the same rule works for both layouts. Pattern mode keeps the MOD's own channel numbers.

**Note-off**

MIDI note-off cuts the voice. MOD has no note-off, so looped samples would otherwise play forever. Samples without loops also stop on note-off, which keeps behavior predictable. In clean mode a cut gets a short fade (about 2 ms) to avoid clicks; authentic mode cuts hard like Paula.

**Legato**

Each sample slot has a legato setting, off by default, and CC 68 (the MIDI legato footswitch) switches it on and off live per MIDI channel. With legato on, the slot plays one voice per MIDI channel: a new note while one is held changes pitch without restarting the sample, like Paula does for arpeggios. With CC 5 above 0 it glides there instead (3xx tone portamento). Chord and drum slots stay polyphonic with legato off. Legato has no effect in drum or pattern mode.

**Drum mode**

The matrix has two key columns per sample: input key (what you play) and output key (the pitch it sounds at). Several samples can share channel 1 this way, laid out like a drum map.

**Pattern mode**

- Pattern keys start at C1 (MIDI 24): pattern 00 is C1, 01 is C#1 and so on. The ProTracker maximum of 100 patterns ends at D#9 (MIDI 123).
- The **song-order key** is C0 (MIDI 12), below the pattern range, so the two never overlap. Both base notes can be changed in settings.
- A pattern loops from row 0 while its key is held; note-off stops it.
- The song-order key plays the song from order position 0, follows jumps and breaks, and loops at the end (the MOD's restart position) while held.
- One pattern plays at a time: a new pattern key or the song key takes over from whatever is playing.
- To arrange the song yourself, sequence pattern notes back to back in the DAW. A standard 64-row pattern at speed 6 is 16 beats, so each note is 4 bars long.
- Pattern breaks (Dxx) shorten a pattern's loop; jumps (Bxx) are ignored when a single pattern loops.

**Tempo**

The MOD's timing is scaled by one factor: host tempo ÷ the MOD's starting tempo. All later Fxx changes keep their relative size, so shuffle made by alternating speeds (F05/F07) and mid-song tempo changes still sound right.

The starting tempo counts speed as well as BPM. ProTracker's tick rate is BPM × 0.4 Hz (125 BPM gives 50 Hz), and a row lasts *speed* ticks. At 4 rows per beat, the musical tempo is BPM × 6 ÷ speed. Using BPM alone is correct only when the song starts at speed 6: a MOD starting at 125 BPM and speed 3 plays at 250 BPM.

- Example: a MOD starting at 125 BPM, speed 6, in a 120 BPM project plays at 120/125 = 0.96 × its speed.
- A **rows per beat** setting (default 4) covers MODs written at 8 rows per beat.
- A **tempo** setting chooses between *follow host* (scaled as above) and *MOD native*.

## Sound settings

Everything below lives on the settings screen and is saved with the DAW project.

| Setting | Options | Default | Notes |
| --- | --- | --- | --- |
| Output layout | Stereo, 4 × mono | Asked on first open | Uses CLAP's audio-ports-config extension; changing it later needs a host rescan |
| Resampler | Authentic (Paula + BLEP), Clean (interpolated) | Authentic | Clean mode also skips the Amiga filters |
| Filter model | A500, A1200, Off | A500 | A500 has a 1-pole low-pass at about 4.4 kHz |
| LED filter | On, Off | Off | Also settable by CC and by effect E0x in pattern mode |
| Panning | Amiga hard (L R R L), stereo separation % | Separation 20% | Separation softens the hard panning for headphones |
| Clock | PAL, NTSC | PAL | Changes pitch and pattern-mode timing slightly |
| Pitch | Period table (snaps to ProTracker notes + finetune), Free | Period table | Free lets pitch bend glide smoothly |
| Tempo | Follow host (scaled), MOD native | Follow host | See Playback modes, Tempo |
| Rows per beat | 4, 8 | 4 | Used to find the MOD's starting tempo |
| Pattern base note | Any MIDI note | C1 (24) | First pattern key |
| Song-order key | Any MIDI note outside the pattern range | C0 (12) | Plays the whole song |

## MIDI input mapping

Performance controls drive the same effect code the replayer uses, so vibrato and slides step at the tracker tick rate like the real thing. The CC numbers below are the defaults, and every mapping can be changed on the settings screen.

| MIDI input | MOD effect | Behavior |
| --- | --- | --- |
| Velocity | Cxx set volume | 1–127 scaled to 0–64 |
| Pitch bend | 1xx / 2xx portamento | ±2 semitones by default; smooth in Free pitch mode |
| Mod wheel (CC 1) | 4xy vibrato depth | Speed from CC 76 |
| CC 68 (legato footswitch) | Legato on/off | 64 and up = on, per MIDI channel; overrides the slot's legato setting |
| Overlapping notes with legato on | Pitch change without retrigger; 3xx tone portamento when CC 5 > 0 | Glide time from CC 5 |
| Channel pressure | 7xy tremolo depth | |
| CC 70 | 9xx sample offset | Start point for the next note, the classic way to chop breaks |
| CC 71 | E9x retrigger | Rate in ticks; tempo-synced stutter |
| CC 72 | ECx note cut | Gate length in ticks |
| CC 74 | E0x LED filter | Off below 64, on from 64 up |
| CC 20–22 | Any effect command | See MIDI export, effect-command encoding |

Arpeggio (0xy) needs no mapping: overlapping or sequenced notes with legato on cover it in the DAW.

## Sample import

A WAV dropped on a sample slot becomes a typical Amiga sample in memory: 8-bit signed mono at about 16.6 kHz, cropped to the MOD limit of 131,070 bytes (about 7.9 s at that rate). The original file is not changed.

1. Mix to mono.
2. Resample to 16,574 Hz (the PAL rate of ProTracker note C-3, period 214), with an anti-alias low-pass.
3. Normalize to full 8-bit range.
4. Quantize to signed 8-bit. Dither is optional and off by default, since most original Amiga samples had none.
5. Crop to 131,070 bytes, with a short fade at the crop point.
6. Take loop points and root note from the WAV's `smpl` chunk if present; otherwise no loop, root note C-3.

The converted sample is stored in the plugin state, so projects don't depend on the WAV file staying on disk.

## MIDI export

Most effects export cleanly as notes, timing or a tempo map. The hard ones change pitch or volume every tick, or depend on state the replayer remembers. The export sends those as effect-command CCs, and the plugin runs the real effect code on playback.

| Effects | Difficulty | Why | Approach |
| --- | --- | --- | --- |
| Notes, ECx note cut, EDx note delay | Easy | Just note timing and length | Notes |
| Fxx speed/tempo, Bxx jump, Dxx break, E6x loop, EEx pattern delay | Easy | Only change the timeline | Unroll the song; tempo map in the MIDI file |
| Cxx set volume, E0x filter | Easy | One value at one time | Velocity / CC |
| 0xy arpeggio | Medium | Paula changes pitch without restarting the sample. Overlapping notes restart it, so the attack repeats | Overlapping notes with legato on: a new note on a held channel changes pitch only |
| 9xx sample offset | Medium | 256 steps, more than a 7-bit CC holds | Effect-command CCs before the note |
| Axy, EAx/EBx volume slides | Medium | Change every tick | Effect-command CCs; the plugin slides |
| 1xx, 2xx, 3xx portamento | Hard | Slides are linear in Amiga periods, not semitones, and can exceed the bend range. 3xx with 00 reuses the last speed | Effect-command CCs |
| 4xy vibrato, 7xy tremolo, E4x/E7x waveform | Hard | Phase and waveform carry across rows | Effect-command CCs |
| E5x finetune, E3x glissando | Hard | Change the pitch table itself | Effect-command CCs |
| EFx invert loop (funk repeat) | Hard | Rewrites sample data while playing | Effect-command CCs, or skip in v1 |

**Channel layout:** the export writes four MIDI tracks, one per MOD channel. A MOD has up to 31 samples, more than MIDI's 16 channels, so sample changes are sent as program changes. A new note on a track cuts the previous one, as on Paula.

**Effect-command encoding:** three CCs per effect on the MOD channel's track. CC 20–22 are undefined in the MIDI spec, so they won't clash with common controllers.

| CC | Carries | Values |
| --- | --- | --- |
| CC 20 | Effect number | 0–15 = effects 0–F; 16–31 = E0–EF |
| CC 21 | Parameter high nibble (x) | 0–15 |
| CC 22 | Parameter low nibble (y) | 0–15; the effect applies when this arrives |

Splitting the 8-bit parameter into nibbles fits in 7-bit CCs and matches how MOD parameters read (xy). Program changes and effect CCs go one MIDI tick (a quarter of a MOD tick) before the row, so they reach the plugin before the row's notes even when a host sends notes first at a shared time. A row with no effect after one with an effect gets a clear triple (0, 0, 0); a sample number without a note sends Cxx with the sample's volume.

The plugin keeps the effect memory (3xx speed, 4xy/7xy speed and depth, 9xx offset, E3x/E4x/E7x) per MIDI channel as ProTracker does per MOD channel, so a new note keeps it. Each effect command or note restarts the channel's tick clock as tick 0 of a row, so a row of speed 6 gets five effect ticks.

Tone portamento with a note: the new key goes down while the old one is held and the old key is released a tick later; the pending 3xx/5xy makes the voice slide at its speed instead of restarting. CC 68/CC 5 are also written for other synths. Velocity 1–127 is volume 0–64, so volume 0 notes survive.

**Pattern-mode export:** the easiest exact export is a clip of pattern-mode notes, one per order-list position, since the plugin then plays its own patterns. This ships before the full note-by-note export.

A second, generic export (notes, velocity, pitch bend, CC 7) could work with other synths, but only approximately.

## UI

The UI copies ProTracker's look: its bitmap font, bevelled grey panels and palette, drawn as pixel art at integer scale (2× or 3×). There are two screens: the front page and a settings screen behind the gear icon.

Front page mock-up (drum mode selected):

```
+--------------------------------------------------------------------------+
| Song: MOD TITLE            [Instrument] [ Pattern ]          [MIDI]  (*) |
+--------------------------------------------------------------------------+
| [Single] [Multi] [Drum]                Drop a WAV on a slot to replace it|
|                                                                          |
|  #   Sample name          Loop   Vol   Fine        In key   Out key      |
| >01  SAMPLE NAME 01       off    64    0           C-1      C-3          |
|  02  SAMPLE NAME 02       off    64    0           D-1      C-3          |
|  03  SAMPLE NAME 03       on     48    -2          F#1      A-3          |
|      ... up to 31 samples                                                |
|  31  SAMPLE NAME 31                                --       --           |
|--------------------------------------------------------------------------|
|  Channel scopes                                                          |
|  +-Ch 1-------+  +-Ch 2-------+  +-Ch 3-------+  +-Ch 4-------+          |
|  |  /\/\/~~~  |  |  /\/\/~~~  |  |  /\/\/~~~  |  |  /\/\/~~~  |          |
|  +------------+  +------------+  +------------+  +------------+          |
+--------------------------------------------------------------------------+
  (*) = gear icon, opens the settings screen
```

The sample matrix's columns change with the mode: MIDI channel in multi-channel mode, in key and out key in drum mode, and pattern numbers in pattern mode. Under the matrix, four scopes show the four Amiga channels.

**Front page**

- Top bar: MOD title, instrument/pattern switch, MIDI export icon (drag to the DAW), gear icon top right.
- Sub-mode row: single, multi, drum.
- Sample matrix: one row per sample with number, name, loop, volume, finetune, legato and the mode's columns. Click a row to select it (single mode); drop a WAV on it to replace the sample.
- Four channel scopes.
- A MOD loads by drag and drop onto the window, or through an open dialog.

**Settings screen**

Everything in Sound settings and the MIDI mapping table, plus a back button.

**Font and graphics**

- Use pt2-clone's own bitmap font (`src/gfx/pt2_gfx_font.c`, an 8×8 font stored as a C array) and its UI bitmaps. They're BSD-3, so we can ship them.
- Don't bundle [echolevel's protracker.ttf](https://github.com/echolevel/protracker-font). Its [license](https://github.com/echolevel/protracker-font/blob/master/license.txt) is the FontStruct Non-Commercial EULA, which forbids distributing or modifying the font. Fine for mock-ups, not for the plugin.

## Implementation plan

Build the engine first and test it against pt2-clone's own renderer, then add the plugin, modes and UI on top.

**Stack, as in [Acidus](https://github.com/holstebroe/Acidus):**

- C++23 (the newest standard GCC, Clang and MSVC support well; move up as C++26 support lands) and CMake 3.20+. Acidus uses C++17, but this project starts fresh. The plugin is one `MODULE` library with a `.clap` suffix, optimized for size in GUI and CLAP glue and for speed in DSP.
- The [CLAP SDK](https://github.com/free-audio/clap) as a git submodule `clap` on branch `next`, used directly with no wrapper framework.
- Its own pixel GUI with native windows: X11 on Linux, Win32 on Windows, Cocoa on macOS. Acidus's `Graphics`, `Font` and `GuiWindow` are the starting point, with pt2-clone's bitmaps and font drawn into the framebuffer.
- A `deploy_clap` target that copies the `.clap` to the folder in `%CLAPTEST%`.
- Tests as standalone `*_test_main.cpp` executables, with Python tools for render comparisons.
- GitHub Actions `build.yml`: a build matrix on Ubuntu, Windows and macOS, and a release job that zips each OS build on `v*.*.*` tags.
- MIT `LICENSE`, plus a third-party notice for pt2-clone's BSD-3 license.

**Repo layout**

```
CMakeLists.txt
LICENSE                    MIT
THIRD_PARTY_LICENSES.md    pt2-clone BSD-3 notice
README.md
clap/                      CLAP SDK submodule (branch next)
cmake/DeployClap.cmake     copy .clap to %CLAPTEST%
src/core/                  voice pool, Paula voice, BLEP, filters, replayer, MOD loader
src/core/pt2/              ported pt2-clone code, tables, gfx and font arrays
src/clap/                  CLAP entry, params, state, audio ports, note handling
src/gui/                   framebuffer, font, screens, scopes, drag and drop
src/import/                WAV to 8-bit conversion
src/export/                song unroll, MIDI file writer
src/*_test_main.cpp        standalone test executables
test/resources/            test MODs + reference renders from pt2-clone
tools/                     Python render and compare scripts
docs/                      this design document
.github/workflows/build.yml
```

**Milestones**

1. **Skeleton:** repo in the Acidus layout, CLAP submodule, CMake, CI on three OSes, an empty CLAP instrument that passes [clap-validator](https://github.com/free-audio/clap-validator).
2. **Engine port:** Paula voice class, BLEP, filters and MOD loader as instance state. Render tests compare our output with pt2-clone's MOD-to-WAV (`pt2_mod2wav.c`) for a set of test MODs.
3. **Single mode:** 32-voice pool, round-robin channels, note on/cut, velocity, pitch bend, stereo out, state save and load.
4. **Multi and drum modes,** plus the 4-mono output layout.
5. **UI:** framebuffer screens with pt2-clone's font, front page, settings screen, scopes. Drag-and-drop of MOD and WAV files needs native code per OS (OLE on Windows, NSDraggingDestination on macOS, XDND on X11).
6. **MIDI effects:** tick-based effect engine per voice, CC mapping, legato per slot and with CC 68.
7. **WAV import:** conversion pipeline, drop onto slot, stored in state.
8. **Pattern mode:** replayer synced to host transport, tempo scaling, loop while held, newest key wins, song-order key.
9. **MIDI export:** pattern-note export first, then full song unroll with effect-command CCs (CC 20–22). Dragging the .mid out of the MIDI icon needs a native drag source per OS; X11 is the hardest.
10. **First release:** tagged build, zipped per OS by the release job, README, third-party notices.

On GitHub, each milestone becomes a GitHub milestone with its tasks as issues.
