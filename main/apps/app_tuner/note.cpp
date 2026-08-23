/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "note.h"

#include <cmath>
#include <cstdio>

namespace note {

namespace {

// Indexed by pitch class. The white keys are spelled the same either way;
// only the five black keys differ.
// clang-format off
const char* const SHARP_NAMES[SEMITONES_PER_OCTAVE] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};
const char* const FLAT_NAMES[SEMITONES_PER_OCTAVE]  = {"C",  "Db", "D",  "Eb", "E",  "F",
                                                       "Gb", "G",  "Ab", "A",  "Bb", "B"};
// clang-format on

// Shown in place of a name when there is nothing to name.
const char* const NAME_NONE = "--";

// Frequency ratio of half a semitone, the widest a pitch can stray from a
// note's centre before it rounds to the neighbouring note.
float semitone_half_ratio()
{
    return std::pow(2.0f, 0.5f / SEMITONES_PER_OCTAVE);
}

}  // namespace

int pitch_class(int midi)
{
    // The extra modulo keeps negative MIDI numbers on the 0..11 range; a C
    // sits at a multiple of 12 in either direction.
    return ((midi % SEMITONES_PER_OCTAVE) + SEMITONES_PER_OCTAVE) % SEMITONES_PER_OCTAVE;
}

float to_frequency(int midi)
{
    return CONCERT_PITCH_HZ * std::pow(2.0f, (float)(midi - A4) / SEMITONES_PER_OCTAVE);
}

int from_frequency(float hz)
{
    if (hz <= 0.0f) {
        return NONE;
    }
    // Inverse of to_frequency(), rounded to the nearest semitone.
    return (int)std::lround(A4 + SEMITONES_PER_OCTAVE * std::log2(hz / CONCERT_PITCH_HZ));
}

float lower_edge_hz(int midi)
{
    return to_frequency(midi) / semitone_half_ratio();
}

float upper_edge_hz(int midi)
{
    return to_frequency(midi) * semitone_half_ratio();
}

float cents_off(int midi, float hz)
{
    if (midi == NONE || hz <= 0.0f) {
        return 0.0f;
    }
    return CENTS_PER_SEMITONE * SEMITONES_PER_OCTAVE * std::log2(hz / to_frequency(midi));
}

const char* format(int midi, char* dst, size_t dst_len, Accidental style)
{
    if (midi == NONE) {
        std::snprintf(dst, dst_len, "%s", NAME_NONE);
    } else {
        const char* const* names = (style == Accidental::Flat) ? FLAT_NAMES : SHARP_NAMES;
        // Scientific pitch notation: MIDI 0 is C-1, so C4 (60) is middle C.
        int octave = midi / SEMITONES_PER_OCTAVE - 1;
        std::snprintf(dst, dst_len, "%s%d", names[pitch_class(midi)], octave);
    }
    return dst;
}

}  // namespace note
