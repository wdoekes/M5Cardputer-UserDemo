/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "note.h"

#include <cstddef>
#include <cstdint>

/**
 * @brief Every tunable of the tuner app, gathered in one place.
 *
 * Nothing here is stored in NVS or exposed in a settings screen; changing a
 * value means rebuilding. The comments say what moving each one does, so the
 * next person does not have to reverse-engineer the number from its use site.
 *
 * The concert pitch itself lives in note.h as note::CONCERT_PITCH_HZ, because
 * the note math is defined relative to it.
 */
namespace tuner {

/* ------------------------------ Playing ---------------------------------- */

// Octave of the lowest playable row. The keyboard rows play one octave
// each going up, so this shifts the whole playable range: at 4 the bottom
// row (z..m) plays C4..B4, at 3 it plays C3..B3. How many rows there are is
// dictated by the keyboard, not configured -- see tuner::KEY_ROWS.
constexpr int PLAY_LOWEST_OCTAVE = 4;

// A tone plays for as long as its key is held, bounded on both ends.
// MIN keeps a quick tap audible and avoids the click that a near-instant
// tone()/stop() pair produces on the I2S amp; MAX is a safety net for a key
// whose release event never arrives.
constexpr uint32_t MIN_PLAY_MS = 150;
constexpr uint32_t MAX_PLAY_MS = 5000;

// How long the speaker stays open and silent after a note ends. A follow-up
// note within this window skips the I2S teardown entirely, which is what
// makes playing a run of notes sound continuous instead of popping between
// each one. Raising it delays the return to listening by the same amount.
constexpr uint32_t COOLDOWN_MS = 250;

// The speaker gets harsh as the pitch climbs, so the drive level tapers off
// above middle C: PLAY_VOLUME_BASE up to PLAY_VOLUME_FLAT_UP_TO, then down
// by PLAY_VOLUME_FALLOFF_PER_SEMITONE per semitone until PLAY_VOLUME_MIN.
constexpr int PLAY_VOLUME_BASE                 = 200;
constexpr int PLAY_VOLUME_MIN                  = 60;
constexpr int PLAY_VOLUME_FALLOFF_PER_SEMITONE = 4;
constexpr int PLAY_VOLUME_FLAT_UP_TO           = note::c_of_octave(4);

/* ------------------------------ Listening -------------------------------- */

// Notes outside this range are discarded. The bottom reaches below the
// played range so a bass or baritone voice registers. The top is where YIN
// runs out of room: at MIC_SAMPLE_RATE_HZ a period that short spans barely
// a dozen samples, and above it the detector starts reporting octave errors
// rather than pitches.
constexpr int DETECT_NOTE_MIN = note::c_of_octave(2);
constexpr int DETECT_NOTE_MAX = note::c_of_octave(6) + note::PC_E;

// 1024 samples at 16 kHz is a 64 ms frame, which holds several periods even
// of the lowest detected note. Three buffers is the minimum that lets us
// run YIN over one while the driver fills the other two.
constexpr size_t MIC_SAMPLE_RATE_HZ = 16000;
constexpr size_t MIC_FRAME_SAMPLES  = 1024;
constexpr size_t MIC_FRAME_COUNT    = 3;

// Mic gain and noise gate handed to the M5Unified mic driver. Same values
// the record app uses.
constexpr uint8_t MIC_MAGNIFICATION      = 128;
constexpr uint8_t MIC_NOISE_FILTER_LEVEL = 2;

// A candidate note has to survive DETECT_STABLE_FRAMES consecutive frames,
// each at least DETECT_MIN_CONFIDENCE strong, before it replaces the note on
// screen. Raising either makes the display steadier but slower to react.
constexpr int DETECT_STABLE_FRAMES    = 2;
constexpr float DETECT_MIN_CONFIDENCE = 0.9f;

// Consecutive silent frames that reset candidate tracking, so the next sound
// has to stabilise on its own rather than continuing an earlier run. The
// note on screen is not cleared by silence; only a new pitch replaces it.
constexpr int DETECT_SILENCE_FRAMES = 5;

/* ------------------------------ Display ---------------------------------- */

// Recent notes shown in the history panel, laid out as a column-major grid:
// oldest at the top of the leftmost column, newest at the bottom of the
// rightmost. The grid size is what bounds the history, so widening the
// panel is the only thing needed to remember more notes.
constexpr int HISTORY_COLUMNS  = 3;
constexpr int HISTORY_ROWS     = 6;
constexpr int HISTORY_CAPACITY = HISTORY_COLUMNS * HISTORY_ROWS;

// A detected pitch within this many cents of centre counts as in tune and
// is drawn plain cyan; past it the colour ramps towards red (sharp) or
// purple (flat), reaching the far end half a semitone out, which is as far
// as a pitch can sit before it is named as its neighbour instead.
constexpr float IN_TUNE_CENTS = 20.0f;

// Oldest history entry is drawn at this fraction of full brightness, the
// newest at full, so the eye lands on the newest first.
constexpr float HISTORY_FADE_FLOOR = 0.5f;

}  // namespace tuner
