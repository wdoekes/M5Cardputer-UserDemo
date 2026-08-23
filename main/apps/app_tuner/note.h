/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>

/**
 * @brief MIDI note-number arithmetic.
 *
 * Notes are plain MIDI numbers throughout the tuner: 60 is middle C (C4),
 * 69 is the concert-pitch A4 reference. `note::NONE` stands for "no note",
 * which is what an empty history slot or a frame without a detected pitch
 * carries.
 */
namespace note {

// Sentinel for "no note". Chosen negative so it can never collide with a
// real MIDI number.
constexpr int NONE = -1;

constexpr int SEMITONES_PER_OCTAVE = 12;
constexpr int CENTS_PER_SEMITONE   = 100;

// Half a semitone. A frequency further than this from its note's centre
// would have been rounded to the neighbouring note instead, so it is the
// widest offset `cents_off()` can return.
constexpr float CENTS_PER_SEMITONE_HALF = CENTS_PER_SEMITONE / 2.0f;

// MIDI number of A4, the note the concert pitch is defined at.
constexpr int A4 = 69;

// The pitch A4 is tuned to. 440 Hz is the modern standard; ensembles that
// tune brighter (442/443 Hz) or to baroque pitch (415 Hz) can change this
// and the whole app -- detection, naming and the played tones -- follows.
constexpr float CONCERT_PITCH_HZ = 440.0f;

// How a black key is spelled. The two are the same pitch -- Bb and A# are
// one MIDI number -- so this is notation, never a different note.
enum class Accidental {
    Sharp,  // C#, D#, F#, G#, A#
    Flat,   // Db, Eb, Gb, Ab, Bb
};

// Buffer size `format()` needs. The longest name it can produce is four
// characters -- two of pitch class and two of octave, as in "C#-1" -- plus
// the terminator.
constexpr size_t NAME_CAPACITY = 5;

// MIDI number of the C that opens `octave`, e.g. c_of_octave(4) == 60.
// Octave numbering is scientific pitch notation, where C4 is middle C and
// MIDI 0 sits in octave -1.
constexpr int c_of_octave(int octave)
{
    return (octave + 1) * SEMITONES_PER_OCTAVE;
}

// Semitone offset of each pitch class from the C that opens its octave.
// Handy both for naming a note and for building one from an octave, as in
// note::c_of_octave(6) + note::PC_E.
enum PitchClass {
    PC_C = 0,
    PC_C_SHARP,
    PC_D,
    PC_D_SHARP,
    PC_E,
    PC_F,
    PC_F_SHARP,
    PC_G,
    PC_G_SHARP,
    PC_A,
    PC_A_SHARP,
    PC_B,
};

// The seven naturals -- the white keys -- as pitch-class offsets from C.
constexpr int NATURAL_COUNT           = 7;
constexpr int NATURALS[NATURAL_COUNT] = {PC_C, PC_D, PC_E, PC_F, PC_G, PC_A, PC_B};

// PC_C through PC_B. Undefined for note::NONE.
int pitch_class(int midi);

// Centre frequency of `midi` in Hz.
float to_frequency(int midi);

// Nearest note to `hz`, or note::NONE for a non-positive frequency.
int from_frequency(float hz);

// Lowest and highest frequency that still rounds to `midi`: half a semitone
// either side of its centre frequency.
float lower_edge_hz(int midi);
float upper_edge_hz(int midi);

// How far `hz` sits from the centre of `midi`, in cents: positive is sharp,
// negative is flat. Returns 0 for note::NONE or a non-positive frequency.
float cents_off(int midi, float hz);

// Writes a name like "C4" or "C#5" into `dst`, or "--" for note::NONE, and
// returns `dst`. `dst_len` should be at least NAME_CAPACITY. `style` picks
// the spelling of the black keys.
const char* format(int midi, char* dst, size_t dst_len, Accidental style = Accidental::Sharp);

}  // namespace note
