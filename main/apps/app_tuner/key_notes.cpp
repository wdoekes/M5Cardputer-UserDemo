/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_notes.h"

namespace tuner {

// clang-format off
const KeyRow KEY_ROWS[KEY_ROW_COUNT] = {
    {"z..m", {KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M}},
    {"s..k", {KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K}},
    {"e..o", {KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O}},
};
// clang-format on

int key_to_note(KeScanCode_t code, int semitone_shift)
{
    for (int row = 0; row < KEY_ROW_COUNT; ++row) {
        for (int natural = 0; natural < note::NATURAL_COUNT; ++natural) {
            if (KEY_ROWS[row].naturals[natural] != code) {
                continue;
            }
            return note::c_of_octave(octave_of_row(row)) + note::NATURALS[natural] + semitone_shift;
        }
    }
    return note::NONE;
}

}  // namespace tuner
