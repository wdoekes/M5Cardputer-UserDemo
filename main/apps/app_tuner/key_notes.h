/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "note.h"
#include "tuner_config.h"

#include <hal/keyboard/keymap.h>

namespace tuner {

/**
 * @brief The keyboard laid out as a piano.
 *
 * Three rows of seven naturals, one octave each, lowest octave first. The
 * rows are the home-row-and-neighbours letters that fall under the fingers
 * on the Cardputer, so C..B reads left to right in each row.
 */
struct KeyRow {
    // Shorthand for the row, e.g. "z..m", shown in the on-screen help.
    const char* hint;
    // Scan codes of the row's naturals, C through B.
    KeScanCode_t naturals[note::NATURAL_COUNT];
};

// The Cardputer has three letter rows below the number row, so the piano
// spans three octaves. Must match the length of KEY_ROWS.
constexpr int KEY_ROW_COUNT = 3;

extern const KeyRow KEY_ROWS[KEY_ROW_COUNT];

// Octave the notes of `row` belong to, in scientific pitch notation.
constexpr int octave_of_row(int row)
{
    return PLAY_LOWEST_OCTAVE + row;
}

// MIDI note a key plays, or note::NONE if the key is not on the piano.
// `semitone_shift` is added to the natural: +1 for Aa (sharp), -1 for Fn
// (flat), 0 for a plain press. Every natural accepts both, so the
// enharmonic edges work out as expected: E# is F, Fb is E, B# is the C of
// the next octave and Cb the B of the previous one.
int key_to_note(KeScanCode_t code, int semitone_shift);

}  // namespace tuner
