/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace tuner {

/**
 * @brief Generates the samples of one played note, a block at a time.
 *
 * Speaker::tone() starts at full amplitude and stops dead, which leaves a
 * step at both ends of a note -- a click, and a worse one the lower the note,
 * because a long period cut at an arbitrary phase leaves a big step. This
 * generates the waveform instead, so the amplitude can be shaped per sample.
 *
 * The phase accumulator carries across calls to fill(), so consecutive blocks
 * join seamlessly as long as the caller keeps the speaker fed. A gap between
 * blocks is an underrun, and an underrun is a click again.
 *
 * The envelope runs attack -> decay -> hold -> release. release() can be
 * called during any stage and ramps down from the level actually reached,
 * so a key tapped and let go mid-attack does not jump to full first. The
 * final block of a note ends at amplitude zero, leaving nothing to click.
 */
// Amplitude scale for a note, falling by TONE_GAIN_DB_PER_OCTAVE for every
// octave above TONE_GAIN_REF_NOTE, so that high notes do not drown out low
// ones. Pass it to ToneGenerator::start().
float gain_for_note(int midi);

class ToneGenerator {
public:
    // Begins a note. `gain` scales the whole envelope, in [0, 1]; it is how
    // the caller makes high notes quieter than low ones.
    void start(float frequency_hz, float gain, uint32_t sample_rate_hz);

    // Moves the envelope into its release ramp. Harmless if already there.
    void release();

    // True once the release has run out and there is nothing left to play.
    bool finished() const
    {
        return _stage == Stage::Idle;
    }

    // Writes up to `capacity` samples of mono 16-bit PCM into `out` and
    // returns how many were written. Short of `capacity` only at the end of
    // a note; 0 once finished.
    size_t fill(int16_t* out, size_t capacity);

private:
    enum class Stage {
        Idle,
        Attack,   // silence up to the peak
        Decay,    // peak down to the hold level
        Hold,     // steady, for as long as the key is down
        Release,  // down to silence from wherever the envelope had reached
    };

    // Advances the envelope by one sample and returns its new level, in
    // [0, 1] before `_gain` is applied.
    float next_envelope_level();

    Stage _stage = Stage::Idle;
    float _gain  = 0.0f;

    // Turns per sample, in [0, 1), and where in the current turn we are.
    float _phase      = 0.0f;
    float _phase_step = 0.0f;

    // Stage lengths in samples, worked out from the sample rate in start().
    uint32_t _attack_samples  = 0;
    uint32_t _decay_samples   = 0;
    uint32_t _release_samples = 0;

    uint32_t _stage_sample = 0;     // samples elapsed in the current stage
    float _level           = 0.0f;  // envelope level of the last sample
    float _release_from    = 0.0f;  // level release() started its ramp at
};

}  // namespace tuner
