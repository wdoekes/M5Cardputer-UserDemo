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

// A tone plays for as long as its key is held. This is only a safety net
// for a key whose release event never arrives.
constexpr uint32_t MAX_PLAY_MS = 5000;

// A played note is synthesised rather than handed to Speaker::tone(), so
// its shape is described here in full.
//
// The envelope runs attack -> decay -> hold -> release. The attack can be
// short because it is applied per sample rather than once per rendered
// frame; the hold sits below the peak deliberately, so a note has some
// shape instead of being a flat tone.
constexpr uint32_t TONE_ATTACK_MS  = 6;
constexpr uint32_t TONE_DECAY_MS   = 150;
constexpr uint32_t TONE_RELEASE_MS = 50;
constexpr float TONE_HOLD_LEVEL    = 0.55f;

// How long the speaker stays open and silent after a note ends. A follow-up
// note within this window reuses the speaker instead of tearing it down and
// bringing the mic back up, so a run of notes is not paced by two peripheral
// switches per note. Raising it delays the return to listening by the same
// amount.
constexpr uint32_t COOLDOWN_MS = 250;

// Peak sample amplitude, as a fraction of 16-bit full scale. Full scale
// clips audibly on this speaker. Together with TONE_GAIN_DB_PER_OCTAVE this
// is the pair of knobs for how loud the app is.
constexpr float TONE_PEAK_AMPLITUDE = 0.7f;

// Loudness has to fall as the pitch climbs: the ear is most sensitive
// around 1-4 kHz and a small speaker reproduces the low end poorly, so
// equal amplitude does not sound equally loud. Decibels per octave is the
// natural unit for a logarithmic scale; TONE_GAIN_REF_NOTE plays at full
// gain and everything above it is quieter, down to the floor.
//
// This wants tuning by ear: the taper it replaces worked out to about
// -5.7 dB/octave and the high notes still came out too loud.
constexpr int TONE_GAIN_REF_NOTE        = note::c_of_octave(4);
constexpr float TONE_GAIN_DB_PER_OCTAVE = -9.0f;
constexpr float TONE_GAIN_MIN           = 0.08f;

// Samples per block handed to the speaker, and how many blocks rotate.
// Speaker_Class holds two wav slots per channel, one playing and one
// reserved, so two blocks are in flight while the third is being generated
// -- the mixer is never reading a buffer we are writing.
//
// Block size trades release latency against underrun margin. A key release
// cannot cancel audio already handed over, so a note rings on for up to two
// blocks (about 43 ms at 48 kHz) before its release ramp starts, which is
// inaudible as a delay. An underrun, on the other hand, is a click.
constexpr size_t TONE_BLOCK_SAMPLES = 1024;
constexpr size_t TONE_BLOCK_COUNT   = 3;

// Wav slots Speaker_Class keeps per channel, which is what isPlaying()
// counts. Feeding it only while it reports fewer than this keeps playRaw()
// from blocking the frame waiting on the mixer.
constexpr size_t SPEAKER_SLOTS_PER_CHANNEL = 2;

// Notes that can sound at once. A press takes any idle voice, so a new
// note starts while the previous one is still releasing instead of being
// dropped -- which is what a run of notes needs, and what playing legato,
// with the next key down before the last is up, needs even more.
//
// Three is enough that a press finds a free voice unless three notes are
// releasing at once, which takes faster playing than the keyboard allows.
// Each voice costs TONE_BLOCK_COUNT blocks of its own, so the buffers come
// to about 18 kB at the sizes above.
constexpr size_t TONE_VOICE_COUNT = 3;

// First mixer channel the voices own; they take one each from here up.
// Speaker_Class hands out channels from the top when asked for any, so
// starting at the bottom stays out of its way. Keyboard SFX are off while
// the app is open, so nothing else is mixing anyway.
constexpr int TONE_CHANNEL_FIRST = 0;

// Master volume while the app owns the speaker. Everything else about the
// level is baked into the samples, so this stays out of the way.
constexpr uint8_t TONE_MASTER_VOLUME = 255;

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

/* ----------------------------- Tap tempo --------------------------------- */

// Tapping SPACE reads out a tempo. Only the press time counts: how long the
// key is held says nothing about the beat.
//
// The window of the last TAP_TEMPO_MAX_TAPS presses gives one fewer
// interval than that. The fastest and the slowest are dropped before
// averaging so a single fumbled tap cannot drag the reading, which is only
// worth doing once there are enough intervals to leave something in the
// middle.
constexpr int TAP_TEMPO_MAX_TAPS = 10;
constexpr int TAP_TEMPO_OUTLIERS = 2;

// A gap longer than this ends the run, so the next tap starts a new one
// instead of averaging across a pause. 2 s is 30 bpm, about the slowest
// anyone taps a beat.
constexpr uint32_t TAP_TEMPO_TIMEOUT_MS = 2000;

/* ------------------------------ Display ---------------------------------- */

// Recent notes shown in the history panel, laid out as a column-major grid:
// oldest at the top of the leftmost column, newest at the bottom of the
// rightmost. The grid size is what bounds the history, so widening the
// panel is the only thing needed to remember more notes.
constexpr int HISTORY_COLUMNS  = 3;
constexpr int HISTORY_ROWS     = 6;
constexpr int HISTORY_CAPACITY = HISTORY_COLUMNS * HISTORY_ROWS;

// A detected pitch within this many cents of centre counts as in tune and
// gets no sign at all. Past it a red "+" or "-" appears beside the note to
// say which way it is off.
constexpr float IN_TUNE_CENTS = 20.0f;

// Oldest history entry is drawn at this fraction of full brightness, the
// newest at full, so the eye lands on the newest first.
constexpr float HISTORY_FADE_FLOOR = 0.5f;

}  // namespace tuner
