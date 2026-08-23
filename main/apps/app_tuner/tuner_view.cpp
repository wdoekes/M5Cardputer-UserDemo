/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "tuner_view.h"
#include "key_notes.h"
#include "tuner_config.h"

#include <apps/utils/theme.h>
#include <hal/hal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace tuner {
namespace view {

namespace {

/* -------------------------------------------------------------------------- */
/*                                  Colours                                   */
/* -------------------------------------------------------------------------- */

// Bit widths of the display's RGB565 pixel format.
constexpr int R5_BITS = 5;
constexpr int G6_BITS = 6;
constexpr int B5_BITS = 5;
constexpr int R5_MAX  = (1 << R5_BITS) - 1;
constexpr int G6_MAX  = (1 << G6_BITS) - 1;
constexpr int B5_MAX  = (1 << B5_BITS) - 1;

struct Rgb888 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Packs 8-bit-per-channel colour into RGB565, the same way the TFT_* macros do.
constexpr uint16_t to_rgb565(const Rgb888& c)
{
    return (uint16_t)(((c.r >> (8 - R5_BITS)) << (G6_BITS + B5_BITS)) | ((c.g >> (8 - G6_BITS)) << B5_BITS) |
                      (c.b >> (8 - B5_BITS)));
}

// Scales every channel of an RGB565 colour by `intensity` in [0, 1].
uint16_t dim_rgb565(uint16_t color, float intensity)
{
    int r = (int)(((color >> (G6_BITS + B5_BITS)) & R5_MAX) * intensity + 0.5f);
    int g = (int)(((color >> B5_BITS) & G6_MAX) * intensity + 0.5f);
    int b = (int)((color & B5_MAX) * intensity + 0.5f);
    r     = std::clamp(r, 0, R5_MAX);
    g     = std::clamp(g, 0, G6_MAX);
    b     = std::clamp(b, 0, B5_MAX);
    return (uint16_t)((r << (G6_BITS + B5_BITS)) | (g << B5_BITS) | b);
}

// Colour ramps for a detected pitch, walked outward from centre. The first
// stop is the in-tune colour and the last is reached half a semitone out,
// which is as far as a pitch can stray before it is named as its neighbour.
// Sharp warms towards red, flat cools towards purple, so the direction to
// correct in is readable at a glance without reading the cents.
constexpr Rgb888 SHARP_RAMP[] = {
    {0, 255, 255},  // cyan
    {255, 255, 0},  // yellow
    {255, 0, 0},    // red
};
constexpr Rgb888 FLAT_RAMP[] = {
    {0, 255, 255},  // cyan
    {0, 0, 255},    // blue
    {128, 0, 255},  // purple
};

template <size_t N>
uint16_t sample_ramp(const Rgb888 (&stops)[N], float position)
{
    float scaled       = std::clamp(position, 0.0f, 1.0f) * (N - 1);
    size_t index       = std::min((size_t)scaled, N - 2);
    float blend        = scaled - index;
    const Rgb888& from = stops[index];
    const Rgb888& to   = stops[index + 1];
    return to_rgb565({(uint8_t)(from.r + (to.r - from.r) * blend), (uint8_t)(from.g + (to.g - from.g) * blend),
                      (uint8_t)(from.b + (to.b - from.b) * blend)});
}

// Colour for a heard pitch: plain cyan while it is within IN_TUNE_CENTS of
// the note's centre, then along the sharp or flat ramp as it drifts out.
uint16_t in_tune_color(int midi, float frequency_hz)
{
    float cents     = note::cents_off(midi, frequency_hz);
    float magnitude = std::fabs(cents);
    if (magnitude <= IN_TUNE_CENTS) {
        return TFT_CYAN;
    }

    // Remap (IN_TUNE_CENTS, half a semitone] onto (0, 1].
    float position = (magnitude - IN_TUNE_CENTS) / (note::CENTS_PER_SEMITONE_HALF - IN_TUNE_CENTS);
    return cents > 0.0f ? sample_ramp(SHARP_RAMP, position) : sample_ramp(FLAT_RAMP, position);
}

// Notes struck on the keyboard are exact by definition, so they are shown in
// a colour of their own rather than on the in-tune ramp.
constexpr uint16_t PLAYED_COLOR = TFT_GREEN;

/* -------------------------------------------------------------------------- */
/*                                   Layout                                   */
/* -------------------------------------------------------------------------- */

// Both fonts in use have a fixed cell, so text can be measured by counting
// characters. FONT_REPL (efontCN_16) is 8x16 at text size 1; fonts::Font0 is
// 6x8 at text size 1 and scales linearly with the text size.
constexpr int REPL_CHAR_W  = FONT_REPL_WIDTH;
constexpr int REPL_CHAR_H  = FONT_REPL_HEIGHT;
constexpr int FONT0_CHAR_W = 6;

// History panel down the left edge. Every entry is padded to a fixed cell so
// the columns line up whether or not the note has an accidental: "C4  " and
// "C#4 " occupy the same width.
constexpr int HISTORY_ENTRY_CHARS = 4;
constexpr int HISTORY_ENTRY_W     = HISTORY_ENTRY_CHARS * REPL_CHAR_W;
constexpr int HISTORY_ROW_H       = REPL_CHAR_H;
constexpr int HISTORY_PANEL_W     = HISTORY_COLUMNS * HISTORY_ENTRY_W;

// The rest of the canvas, holding the big note and the piano. Everything in
// it is centred on the region rather than pinned to a fixed x.
constexpr int RIGHT_GUTTER = 4;
constexpr int RIGHT_X      = HISTORY_PANEL_W + RIGHT_GUTTER;

// Vertical bands of the right-hand region, in a 109 px tall canvas:
//   big note at text size 3, 24 px tall
//   frequency and confidence at text size 1, two 8 px lines
//   piano, 32 px tall
constexpr int BIG_NOTE_TEXT_SIZE = 3;
constexpr int BIG_NOTE_Y         = 8;
constexpr int INFO_Y             = 40;
constexpr int INFO_LINE_H        = 14;

// One octave of piano, drawn small enough not to dominate the layout.
constexpr int PIANO_WHITE_W = 8;
constexpr int PIANO_W       = note::NATURAL_COUNT * PIANO_WHITE_W;
constexpr int PIANO_H       = 32;
constexpr int PIANO_BLACK_W = 5;
constexpr int PIANO_BLACK_H = 20;
constexpr int PIANO_Y       = 72;

// Where each black key sits: the index of the white key it is drawn after,
// and the pitch class it sounds.
struct BlackKey {
    int after_white;
    int pitch_class;
};
constexpr BlackKey BLACK_KEYS[] = {
    {0, note::PC_C_SHARP},  // between C and D
    {1, note::PC_D_SHARP},  // between D and E
    {3, note::PC_F_SHARP},  // between F and G
    {4, note::PC_G_SHARP},  // between G and A
    {5, note::PC_A_SHARP},  // between A and B
};

// Help text, shown while nothing has been played or heard yet.
constexpr int HELP_Y            = 16;
constexpr int HELP_LINE_SPACING = 24;

/* -------------------------------------------------------------------------- */
/*                               Drawing helpers                              */
/* -------------------------------------------------------------------------- */

// Horizontal centre of the region right of the history panel.
int right_center_x(LGFX_Sprite& canvas)
{
    return (RIGHT_X + canvas.width()) / 2;
}

void print_centered(LGFX_Sprite& canvas, int center_x, int y, int char_w, const char* text)
{
    canvas.setCursor(center_x - (int)std::strlen(text) * char_w / 2, y);
    canvas.print(text);
}

void draw_history(LGFX_Sprite& canvas, const NoteHistory& history)
{
    canvas.setFont(FONT_REPL);
    canvas.setTextSize(FONT_SIZE_REPL);

    for (int i = 0; i < history.size(); ++i) {
        const NoteHistory::Entry& entry = history[i];

        char name[note::NAME_CAPACITY];
        note::format(entry.midi, name, sizeof(name));
        // Left-aligned and truncated to exactly one cell, so a name that
        // somehow runs long cannot push the column out of line.
        char cell[HISTORY_ENTRY_CHARS + 1];
        std::snprintf(cell, sizeof(cell), "%-*.*s", HISTORY_ENTRY_CHARS, HISTORY_ENTRY_CHARS, name);

        // Fade the older entries so the newest one catches the eye first.
        float age_fraction = (history.size() > 1) ? (float)i / (history.size() - 1) : 1.0f;
        float intensity    = HISTORY_FADE_FLOOR + (1.0f - HISTORY_FADE_FLOOR) * age_fraction;

        uint16_t color = (entry.source == NoteHistory::Source::Played) ? PLAYED_COLOR : (uint16_t)TFT_CYAN;
        canvas.setTextColor(dim_rgb565(color, intensity), THEME_COLOR_BG);
        canvas.setCursor((i / HISTORY_ROWS) * HISTORY_ENTRY_W, (i % HISTORY_ROWS) * HISTORY_ROW_H);
        canvas.print(cell);
    }

    canvas.setFont(&fonts::Font0);
}

// Takes over the history panel until the first note arrives.
void draw_key_help(LGFX_Sprite& canvas)
{
    canvas.setFont(FONT_REPL);
    canvas.setTextSize(FONT_SIZE_REPL);
    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);

    int y = 0;
    canvas.setCursor(0, y);
    canvas.print("keys:");
    y += HISTORY_ROW_H;

    for (int row = 0; row < KEY_ROW_COUNT; ++row) {
        int octave = octave_of_row(row);
        char line[32];
        std::snprintf(line, sizeof(line), "%s C%d..B%d", KEY_ROWS[row].hint, octave, octave);
        canvas.setCursor(0, y);
        canvas.print(line);
        y += HISTORY_ROW_H;
    }

    canvas.setCursor(0, y);
    canvas.print("Aa : #");
    y += HISTORY_ROW_H;
    canvas.setCursor(0, y);
    canvas.print("Fn : b");

    canvas.setFont(&fonts::Font0);
}

// Right-hand counterpart to the key help: says what the app is doing while
// it waits and what it can hear.
void draw_listening_help(LGFX_Sprite& canvas)
{
    char range[32];
    char low[note::NAME_CAPACITY];
    char high[note::NAME_CAPACITY];
    std::snprintf(range, sizeof(range), "ear %s..%s", note::format(DETECT_NOTE_MIN, low, sizeof(low)),
                  note::format(DETECT_NOTE_MAX, high, sizeof(high)));

    canvas.setFont(FONT_REPL);
    canvas.setTextSize(FONT_SIZE_REPL);
    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    print_centered(canvas, right_center_x(canvas), HELP_Y, REPL_CHAR_W, "play or sing");
    print_centered(canvas, right_center_x(canvas), HELP_Y + HELP_LINE_SPACING, REPL_CHAR_W, range);
    canvas.setFont(&fonts::Font0);
}

void draw_note_readout(LGFX_Sprite& canvas, const Model& model, uint16_t color)
{
    char name[note::NAME_CAPACITY];
    note::format(model.note, name, sizeof(name));

    canvas.setTextSize(BIG_NOTE_TEXT_SIZE);
    canvas.setTextColor(color, THEME_COLOR_BG);
    print_centered(canvas, right_center_x(canvas), BIG_NOTE_Y, FONT0_CHAR_W * BIG_NOTE_TEXT_SIZE, name);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);

    char line[16];
    std::snprintf(line, sizeof(line), "%.1f Hz", model.frequency_hz);
    print_centered(canvas, right_center_x(canvas), INFO_Y, FONT0_CHAR_W, line);
    std::snprintf(line, sizeof(line), "conf %.2f", model.confidence);
    print_centered(canvas, right_center_x(canvas), INFO_Y + INFO_LINE_H, FONT0_CHAR_W, line);
}

// One octave of keys, with every key of the shown note's pitch class lit --
// octave is deliberately ignored, so any C from C2 up lights the same key.
void draw_piano(LGFX_Sprite& canvas, int midi, uint16_t color)
{
    int lit    = (midi == note::NONE) ? note::NONE : note::pitch_class(midi);
    int base_x = right_center_x(canvas) - PIANO_W / 2;

    for (int i = 0; i < note::NATURAL_COUNT; ++i) {
        int x       = base_x + i * PIANO_WHITE_W;
        bool is_lit = (lit == note::NATURALS[i]);
        // Inset the fill by a pixel so the outline stays visible between keys.
        canvas.fillRect(x + 1, PIANO_Y, PIANO_WHITE_W - 2, PIANO_H - 1, is_lit ? color : TFT_WHITE);
        canvas.drawRect(x, PIANO_Y, PIANO_WHITE_W, PIANO_H, TFT_BLACK);
    }

    for (const BlackKey& key : BLACK_KEYS) {
        // Straddle the boundary between its two white neighbours.
        int x       = base_x + (key.after_white + 1) * PIANO_WHITE_W - PIANO_BLACK_W / 2;
        bool is_lit = (lit == key.pitch_class);
        canvas.fillRect(x, PIANO_Y, PIANO_BLACK_W, PIANO_BLACK_H, is_lit ? color : TFT_BLACK);
        if (is_lit) {
            // A lit black key needs its own outline; unlit it is already black
            // on white and reads as an outline by itself.
            canvas.drawRect(x, PIANO_Y, PIANO_BLACK_W, PIANO_BLACK_H, TFT_BLACK);
        }
    }
}

}  // namespace

void render(const Model& model)
{
    LGFX_Sprite& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);

    if (model.history->empty()) {
        draw_key_help(canvas);
    } else {
        draw_history(canvas, *model.history);
    }

    if (model.note == note::NONE) {
        draw_listening_help(canvas);
    } else {
        uint16_t color = model.played ? PLAYED_COLOR : in_tune_color(model.note, model.frequency_hz);
        draw_note_readout(canvas, model, color);
    }

    // The piano keeps the plain played/detected colour: it points at a key,
    // it is not a tuning readout, and the in-tune ramp is hard to read on a
    // 5 px wide black key.
    draw_piano(canvas, model.note, model.played ? PLAYED_COLOR : (uint16_t)TFT_CYAN);

    GetHAL().pushCanvas();
}

}  // namespace view
}  // namespace tuner
