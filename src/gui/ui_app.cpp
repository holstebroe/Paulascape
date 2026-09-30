#include "ui_app.hpp"
#include "file_dialog.hpp"
#include "font_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace paulascape {

namespace {

// ProTracker-like palette
constexpr uint32_t COL_BG = 0xFF888888;
constexpr uint32_t COL_PANEL = 0xFFAAAAAA;
constexpr uint32_t COL_LIGHT = 0xFFDDDDDD;
constexpr uint32_t COL_DARK = 0xFF444444;
constexpr uint32_t COL_TEXT = 0xFF000000;
constexpr uint32_t COL_SEL_BG = 0xFF3344AA;
constexpr uint32_t COL_SEL_TEXT = 0xFFFFFFFF;
constexpr uint32_t COL_DIM = 0xFF666666;
constexpr uint32_t COL_LCD_BG = 0xFF222233;
constexpr uint32_t COL_LCD_TEXT = 0xFFCCCCFF;

constexpr int ROW_H = 10;
constexpr int MATRIX_Y = 62;

const char* NOTE_NAMES[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};

std::string noteName(int key) {
    key = std::clamp(key, 0, 127);
    return std::string(NOTE_NAMES[key % 12]) + std::to_string(key / 12 - 2);
}

std::string padRight(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    while (s.size() < n) s.push_back(' ');
    return s;
}

std::string padLeft(const std::string& s, size_t n) {
    return s.size() >= n ? s : std::string(n - s.size(), ' ') + s;
}

void bevel(Framebuffer& fb, int x, int y, int w, int h, bool pressed, uint32_t fill) {
    fb.fillRect(x, y, w, h, fill);
    const uint32_t hi = pressed ? COL_DARK : COL_LIGHT;
    const uint32_t lo = pressed ? COL_LIGHT : COL_DARK;
    fb.fillRect(x, y, w, 1, hi);
    fb.fillRect(x, y, 1, h, hi);
    fb.fillRect(x, y + h - 1, w, 1, lo);
    fb.fillRect(x + w - 1, y, 1, h, lo);
}

void text(Framebuffer& fb, int x, int y, const std::string& s, uint32_t color = COL_TEXT) {
    FontRenderer::drawString(fb, x, y, s, color, 1);
}

// 8x8 gear glyph for the settings icon
void gear(Framebuffer& fb, int x, int y, uint32_t color) {
    static const uint8_t rows[8] = {0x5A, 0xFF, 0xC3, 0xDB, 0xDB, 0xC3, 0xFF, 0x5A};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (rows[r] & (0x80 >> c)) fb.drawPixel(x + c, y + r, color);
}

bool inside(const UiApp*, int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

} // namespace

UiApp::UiApp(PaulascapePlugin& plugin) : plugin(plugin) {}

void UiApp::addHit(int x, int y, int w, int h, std::function<void(int, bool)> action, std::function<void(int)> wheel) {
    hits.push_back({x, y, w, h, std::move(action), std::move(wheel)});
}

void UiApp::button(Framebuffer& fb, int x, int y, int w, int h, const std::string& label, bool active,
                   std::function<void(int, bool)> action) {
    bevel(fb, x, y, w, h, active, active ? COL_SEL_BG : COL_PANEL);
    const int tx = x + (w - static_cast<int>(label.size()) * 8) / 2;
    text(fb, tx, y + (h - 8) / 2, label, active ? COL_SEL_TEXT : COL_TEXT);
    if (action) addHit(x, y, w, h, std::move(action));
}

int UiApp::totalRows() const {
    return snap.params[PARAM_PLAYBACK_MODE] >= 0.5 ? std::max<int>(snap.numPatterns + 1, 1) : 31;
}

void UiApp::clampScroll() {
    scroll = std::clamp(scroll, 0, std::max(0, totalRows() - visibleRows()));
}

void UiApp::cycleParam(clap_id id, int button, bool shift) {
    const double v = snap.params[id];
    const double lo = PaulascapePlugin::paramMin(id);
    const double hi = PaulascapePlugin::paramMax(id);
    const int dir = button == 3 ? -1 : 1;
    double nv;
    if (id == PARAM_STEREO_SEPARATION) {
        nv = std::clamp(v + dir * (shift ? 0.25 : 0.05), lo, hi);
    } else if (id == PARAM_PATTERN_BASE_NOTE || id == PARAM_SONG_ORDER_KEY) {
        nv = std::clamp(v + dir * (shift ? 12 : 1), lo, hi);
    } else {
        nv = v + dir;
        if (nv > hi) nv = lo;
        if (nv < lo) nv = hi;
    }
    plugin.setParamFromGui(id, nv);
}

void UiApp::loadModDialog() {
    const std::string path = openFileDialog("Open MOD file", "ProTracker MOD", "mod");
    if (path.empty()) return;
    status = plugin.loadModFile(path) ? "Loaded " + path.substr(path.find_last_of("/\\") + 1) : "Could not load MOD";
    scroll = 0;
}

void UiApp::importWavDialog() {
    const std::string path = openFileDialog("Import WAV into selected slot", "WAV audio", "wav");
    if (path.empty()) return;
    status = plugin.importWavToSlot(snap.selectedSlot, path) ? "Imported WAV into slot " + std::to_string(snap.selectedSlot)
                                                              : "Could not import WAV";
}

void UiApp::exportMidiDialog(bool notes) {
    const std::string path = saveFileDialog(notes ? "Export song as MIDI notes" : "Export pattern-mode MIDI clip",
                                            notes ? "song_notes.mid" : "song_patterns.mid", "MIDI file", "mid");
    if (path.empty()) return;
    const bool ok = notes ? plugin.exportMidi(path, -1) : plugin.exportPatternClip(path);
    status = ok ? (notes ? "Exported MIDI notes" : "Exported pattern clip") : "MIDI export failed";
}

void UiApp::render(Framebuffer& fb) {
    snap = plugin.snapshot();
    hits.clear();
    clampScroll();
    if (fb.getWidth() != WIDTH || fb.getHeight() != HEIGHT) fb.resize(WIDTH, HEIGHT);
    if (screen == Screen::Front) drawFront(fb);
    else if (screen == Screen::Settings) drawSettings(fb);
    else drawMidiMap(fb);
}

void UiApp::drawFront(Framebuffer& fb) {
    fb.clear(COL_BG);
    const bool patternMode = snap.params[PARAM_PLAYBACK_MODE] >= 0.5;
    const int sub = static_cast<int>(snap.params[PARAM_SUB_MODE]);

    // Top bar
    bevel(fb, 4, 4, 240, 18, true, COL_LCD_BG);
    text(fb, 10, 9, padRight("Song: " + (snap.title.empty() ? "Untitled MOD" : snap.title), 28), COL_LCD_TEXT);
    button(fb, 252, 4, 96, 18, "Instrument", !patternMode, [this](int, bool) {
        plugin.setParamFromGui(PARAM_PLAYBACK_MODE, 0);
    });
    button(fb, 352, 4, 72, 18, "Pattern", patternMode, [this](int, bool) {
        plugin.setParamFromGui(PARAM_PLAYBACK_MODE, 1);
    });
    button(fb, 432, 4, 72, 18, "Open MOD", false, [this](int, bool) { loadModDialog(); });
    button(fb, 508, 4, 40, 18, "MIDI", false, [this](int, bool) { exportMidiDialog(false); });
    button(fb, 550, 4, 48, 18, "Notes", false, [this](int, bool) { exportMidiDialog(true); });
    button(fb, 600, 4, 36, 18, "", false, [this](int, bool) { screen = Screen::Settings; });
    gear(fb, 614, 9, COL_TEXT);

    // Sub-mode row
    const char* subNames[3] = {"Single", "Multi", "Drum"};
    const int subW[3] = {64, 56, 48};
    int bx = 4;
    for (int i = 0; i < 3; ++i) {
        const bool active = !patternMode && sub == i;
        button(fb, bx, 28, subW[i], 16, subNames[i], active, [this, i](int, bool) {
            plugin.setParamFromGui(PARAM_SUB_MODE, i);
            plugin.setParamFromGui(PARAM_PLAYBACK_MODE, 0);
        });
        bx += subW[i] + 4;
    }
    button(fb, bx + 8, 28, 88, 16, "Import WAV", false, [this](int, bool) { importWavDialog(); });
    text(fb, bx + 104, 32, patternMode ? "Keys play patterns" : "Import goes to selected slot", COL_DIM);

    if (patternMode) drawPatternList(fb); else drawSampleMatrix(fb);

    // Hint / status
    const std::string hint = status.empty() ? "L-click +, R-click -, Shift = big step, wheel scrolls" : status;
    text(fb, 8, MATRIX_Y + ROW_H * visibleRows() + 10, padRight(hint, 76), status.empty() ? COL_DIM : COL_TEXT);

    // Scopes
    scopes.pull(plugin.scopeTap());
    scopes.draw(fb, 8, 262, fb.getWidth() - 16, 130);
}

void UiApp::drawSampleMatrix(Framebuffer& fb) {
    const int sub = static_cast<int>(snap.params[PARAM_SUB_MODE]);
    const int listH = ROW_H * visibleRows();

    bevel(fb, 4, 48, fb.getWidth() - 8, 12, false, COL_PANEL);
    text(fb, 8, 50, "#", COL_TEXT);
    text(fb, 32, 50, "Sample name", COL_TEXT);
    text(fb, 216, 50, "Lp", COL_TEXT);
    text(fb, 240, 50, "Vol", COL_TEXT);
    text(fb, 280, 50, "Fine", COL_TEXT);
    text(fb, 312, 50, "Leg", COL_TEXT);
    if (sub == 0) text(fb, 352, 50, "Length", COL_TEXT);
    else if (sub == 1) text(fb, 352, 50, "MIDI ch", COL_TEXT);
    else { text(fb, 352, 50, "In key", COL_TEXT); text(fb, 408, 50, "Out key", COL_TEXT); }

    bevel(fb, 4, MATRIX_Y - 1, fb.getWidth() - 8, listH + 2, true, COL_LCD_BG);
    addHit(4, MATRIX_Y - 1, fb.getWidth() - 8, listH + 2, nullptr, [this](int d) { scroll -= d * 3; clampScroll(); });

    for (int r = 0; r < visibleRows(); ++r) {
        const int slot = scroll + r + 1;
        if (slot > 31) break;
        const SlotView& s = snap.slots[slot];
        const int y = MATRIX_Y + r * ROW_H + 1;
        const bool selected = slot == snap.selectedSlot;
        if (selected) fb.fillRect(6, y - 1, fb.getWidth() - 12, ROW_H, COL_SEL_BG);
        const uint32_t fg = selected ? COL_SEL_TEXT : (s.length > 0 ? COL_LCD_TEXT : COL_DIM);

        char num[8];
        std::snprintf(num, sizeof(num), "%02d", slot);
        text(fb, 8, y, num, fg);
        text(fb, 32, y, padRight(s.name, 22), fg);
        text(fb, 216, y, s.loop ? "on" : "--", fg);
        text(fb, 240, y, padLeft(std::to_string(s.volume), 3), fg);
        text(fb, 272, y, padLeft(std::to_string(s.finetune), 4), fg);
        text(fb, 312, y, s.legato ? "on" : "--", fg);
        if (sub == 0) text(fb, 352, y, padLeft(std::to_string(s.length), 6), fg);
        else if (sub == 1) text(fb, 352, y, "  " + std::to_string(slot), fg);
        else {
            text(fb, 352, y, noteName(s.inKey), fg);
            text(fb, 408, y, noteName(s.outKey), fg);
        }

        const int h = ROW_H;
        const uint8_t u = static_cast<uint8_t>(slot);
        auto step = [](int button, bool shift, int big) { return (button == 3 ? -1 : 1) * (shift ? big : 1); };
        addHit(6, y - 1, 204, h, [this, u](int, bool) { plugin.selectSlot(u); });
        addHit(212, y - 1, 24, h, [this, u](int, bool) { plugin.adjustSlot(u, SlotField::Loop, 0); });
        addHit(238, y - 1, 32, h, [this, u, step](int b, bool sh) { plugin.adjustSlot(u, SlotField::Volume, step(b, sh, 8)); },
               [this, u](int d) { plugin.adjustSlot(u, SlotField::Volume, d); });
        addHit(272, y - 1, 36, h, [this, u, step](int b, bool sh) { plugin.adjustSlot(u, SlotField::Finetune, step(b, sh, 4)); },
               [this, u](int d) { plugin.adjustSlot(u, SlotField::Finetune, d); });
        addHit(310, y - 1, 28, h, [this, u](int, bool) { plugin.adjustSlot(u, SlotField::Legato, 0); });
        if (sub == 2) {
            addHit(350, y - 1, 48, h, [this, u, step](int b, bool sh) { plugin.adjustSlot(u, SlotField::InKey, step(b, sh, 12)); },
                   [this, u](int d) { plugin.adjustSlot(u, SlotField::InKey, d); });
            addHit(406, y - 1, 48, h, [this, u, step](int b, bool sh) { plugin.adjustSlot(u, SlotField::OutKey, step(b, sh, 12)); },
                   [this, u](int d) { plugin.adjustSlot(u, SlotField::OutKey, d); });
        }
    }

    // Scroll bar
    const int total = totalRows();
    if (total > visibleRows()) {
        const int trackX = fb.getWidth() - 14;
        const int thumbH = std::max(10, listH * visibleRows() / total);
        const int thumbY = MATRIX_Y + (listH - thumbH) * scroll / (total - visibleRows());
        fb.fillRect(trackX, MATRIX_Y, 8, listH, COL_DARK);
        bevel(fb, trackX, thumbY, 8, thumbH, false, COL_PANEL);
    }
}

void UiApp::drawPatternList(Framebuffer& fb) {
    const int listH = ROW_H * visibleRows();
    const int base = static_cast<int>(snap.params[PARAM_PATTERN_BASE_NOTE]);
    const int songKey = static_cast<int>(snap.params[PARAM_SONG_ORDER_KEY]);

    bevel(fb, 4, 48, fb.getWidth() - 8, 12, false, COL_PANEL);
    text(fb, 8, 50, "Key   Pattern  Used in song order positions", COL_TEXT);
    bevel(fb, 4, MATRIX_Y - 1, fb.getWidth() - 8, listH + 2, true, COL_LCD_BG);
    addHit(4, MATRIX_Y - 1, fb.getWidth() - 8, listH + 2, nullptr, [this](int d) { scroll -= d * 3; clampScroll(); });

    for (int r = 0; r < visibleRows(); ++r) {
        const int row = scroll + r;
        if (row > snap.numPatterns) break;
        const int y = MATRIX_Y + r * ROW_H + 1;
        const bool song = row == 0;
        const int pat = row - 1;
        const bool playing = song ? false : (snap.playingPattern == pat);
        if (playing) fb.fillRect(6, y - 1, fb.getWidth() - 12, ROW_H, COL_SEL_BG);
        const uint32_t fg = playing ? COL_SEL_TEXT : COL_LCD_TEXT;

        if (song) {
            text(fb, 8, y, padRight(noteName(songKey), 6) + "SONG     plays the whole song from order 0", fg);
            continue;
        }
        char head[32];
        std::snprintf(head, sizeof(head), "%-6s%02d       ", noteName(base + pat).c_str(), pat);
        std::string used;
        for (int o = 0; o < snap.songLength && used.size() < 52; ++o)
            if (snap.orderList[o] == pat) used += std::to_string(o) + " ";
        text(fb, 8, y, head + (used.empty() ? std::string("-") : used), fg);
    }
    if (snap.numPatterns == 0) text(fb, 8, MATRIX_Y + 12, "No patterns: open a MOD file", COL_DIM);
}

void UiApp::drawSettings(Framebuffer& fb) {
    fb.clear(COL_BG);
    button(fb, 4, 4, 72, 18, "< Back", false, [this](int, bool) { screen = Screen::Front; });
    text(fb, 96, 9, "Paulascape settings", COL_TEXT);
    button(fb, 480, 4, 152, 18, "MIDI mapping >", false, [this](int, bool) { screen = Screen::MidiMap; });

    struct Row { const char* label; clap_id id; const char* note; };
    const Row rows[] = {
        {"Output layout", PARAM_OUTPUT_LAYOUT, "changing needs a host rescan"},
        {"Resampler", PARAM_RESAMPLER_MODE, "Clean also skips Amiga filters"},
        {"Filter model", PARAM_FILTER_MODEL, ""},
        {"LED filter", PARAM_LED_FILTER, ""},
        {"Stereo separation", PARAM_STEREO_SEPARATION, ""},
        {"Pitch", PARAM_PITCH_MODE, "Free lets pitch bend glide"},
        {"Tempo", PARAM_TEMPO_MODE, ""},
        {"Clock", PARAM_CLOCK, "changes pitch and timing slightly"},
        {"Rows per beat", PARAM_ROWS_PER_BEAT, ""},
        {"Pattern base note", PARAM_PATTERN_BASE_NOTE, "first pattern key"},
        {"Song-order key", PARAM_SONG_ORDER_KEY, "plays the whole song"},
    };
    int y = 32;
    for (const Row& row : rows) {
        char value[48];
        plugin.paramsValueToText(row.id, snap.params[row.id], value, sizeof(value));
        bevel(fb, 8, y, fb.getWidth() - 16, 24, false, COL_PANEL);
        text(fb, 16, y + 8, row.label, COL_TEXT);
        bevel(fb, 184, y + 3, 152, 18, true, COL_LCD_BG);
        text(fb, 192, y + 8, padRight(value, 17), COL_LCD_TEXT);
        text(fb, 352, y + 8, row.note, COL_DIM);
        const clap_id id = row.id;
        addHit(184, y + 3, 152, 18, [this, id](int b, bool sh) { cycleParam(id, b, sh); },
               [this, id](int d) { cycleParam(id, d > 0 ? 1 : 3, false); });
        y += 28;
    }
    // UI scale (window only, not a DAW parameter)
    bevel(fb, 8, y, fb.getWidth() - 16, 24, false, COL_PANEL);
    text(fb, 16, y + 8, "Window scale", COL_TEXT);
    bevel(fb, 184, y + 3, 152, 18, true, COL_LCD_BG);
    text(fb, 192, y + 8, padRight(std::to_string(uiScale) + "x", 17), COL_LCD_TEXT);
    addHit(184, y + 3, 152, 18, [this](int b, bool) {
        int s = uiScale + (b == 3 ? -1 : 1);
        s = s > 3 ? 1 : (s < 1 ? 3 : s);
        setScale(s);
        if (onScaleChanged) onScaleChanged(uiScale);
    });
}

void UiApp::drawMidiMap(Framebuffer& fb) {
    fb.clear(COL_BG);
    button(fb, 4, 4, 72, 18, "< Back", false, [this](int, bool) { screen = Screen::Settings; });
    text(fb, 96, 9, "MIDI CC mapping (L-click +1, R-click -1, Shift 10)", COL_TEXT);
    const MidiMap map = plugin.getMidiMap();
    const uint8_t values[PaulascapePlugin::MIDI_MAP_ENTRIES] = {map.modWheel, map.glide, map.legato, map.vibratoSpeed,
        map.sampleOffset, map.retrigger, map.noteCut, map.ledFilter, map.fxNumber, map.fxHigh, map.fxLow};
    int y = 32;
    for (size_t i = 0; i < PaulascapePlugin::MIDI_MAP_ENTRIES; ++i) {
        bevel(fb, 8, y, fb.getWidth() - 16, 24, false, COL_PANEL);
        text(fb, 16, y + 8, PaulascapePlugin::midiMapEntryName(i), COL_TEXT);
        bevel(fb, 300, y + 3, 96, 18, true, COL_LCD_BG);
        text(fb, 308, y + 8, "CC " + padLeft(std::to_string(values[i]), 3), COL_LCD_TEXT);
        addHit(300, y + 3, 96, 18, [this, i](int b, bool sh) { plugin.adjustMidiMap(i, (b == 3 ? -1 : 1) * (sh ? 10 : 1)); },
               [this, i](int d) { plugin.adjustMidiMap(i, d); });
        y += 28;
    }
}

void UiApp::onMouseDown(int x, int y, int button, bool shift) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (it->action && inside(this, x, y, it->x, it->y, it->w, it->h)) {
            it->action(button, shift);
            return;
        }
    }
}

void UiApp::onWheel(int x, int y, int delta) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (it->wheel && inside(this, x, y, it->x, it->y, it->w, it->h)) {
            it->wheel(delta);
            return;
        }
    }
}

void UiApp::onFileDropped(int x, int y, const std::string& path) {
    auto ends = [&](const char* ext) {
        const size_t n = std::char_traits<char>::length(ext);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(path[path.size() - n + i])) != ext[i]) return false;
        return true;
    };
    if (ends(".mod")) {
        status = plugin.loadModFile(path) ? "Loaded MOD" : "Could not load MOD";
        scroll = 0;
    } else if (ends(".wav") && screen == Screen::Front && snap.params[PARAM_PLAYBACK_MODE] < 0.5) {
        int slot = snap.selectedSlot;
        if (y >= MATRIX_Y && y < MATRIX_Y + ROW_H * visibleRows()) slot = scroll + (y - MATRIX_Y) / ROW_H + 1;
        (void)x;
        if (slot >= 1 && slot <= 31) {
            plugin.selectSlot(static_cast<uint8_t>(slot));
            status = plugin.importWavToSlot(static_cast<uint8_t>(slot), path) ? "Imported WAV into slot " + std::to_string(slot)
                                                                               : "Could not import WAV";
        }
    }
}

} // namespace paulascape
