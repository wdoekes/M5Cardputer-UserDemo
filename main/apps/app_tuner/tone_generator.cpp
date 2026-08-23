/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "tone_generator.h"
#include "tuner_config.h"
#include "note.h"
#include <algorithm>
#include <cmath>

namespace tuner {

namespace {

constexpr float TWO_PI = 6.283185307179586f;

// Largest magnitude a signed 16-bit sample can carry.
constexpr float SAMPLE_FULL_SCALE = 32767.0f;

constexpr uint32_t MS_PER_SECOND = 1000;

uint32_t ms_to_samples(uint32_t ms, uint32_t sample_rate_hz)
{
    return (uint32_t)((uint64_t)ms * sample_rate_hz / MS_PER_SECOND);
}

// Straight line from `from` to `to` across `duration`, at `elapsed`.
float ramp(float from, float to, uint32_t elapsed, uint32_t duration)
{
    if (duration == 0 || elapsed >= duration) {
        return to;
    }
    return from + (to - from) * ((float)elapsed / duration);
}

// Decibels spanned by a factor of ten in amplitude.
constexpr float DB_PER_AMPLITUDE_DECADE = 20.0f;

}  // namespace

float gain_for_note(int midi)
{
    float octaves_above = (float)(midi - TONE_GAIN_REF_NOTE) / note::SEMITONES_PER_OCTAVE;
    float decibels      = TONE_GAIN_DB_PER_OCTAVE * octaves_above;
    return std::clamp(std::pow(10.0f, decibels / DB_PER_AMPLITUDE_DECADE), TONE_GAIN_MIN, 1.0f);
}

void ToneGenerator::start(float frequency_hz, float gain, uint32_t sample_rate_hz)
{
    _gain       = std::clamp(gain, 0.0f, 1.0f);
    _phase      = 0.0f;
    _phase_step = (sample_rate_hz > 0) ? (frequency_hz / sample_rate_hz) : 0.0f;

    _attack_samples  = ms_to_samples(TONE_ATTACK_MS, sample_rate_hz);
    _decay_samples   = ms_to_samples(TONE_DECAY_MS, sample_rate_hz);
    _release_samples = ms_to_samples(TONE_RELEASE_MS, sample_rate_hz);

    _stage        = Stage::Attack;
    _stage_sample = 0;
    _level        = 0.0f;
    _release_from = 0.0f;
}

void ToneGenerator::release()
{
    if (_stage == Stage::Idle || _stage == Stage::Release) {
        return;
    }
    // Ramp down from where the envelope actually got to, not from the hold
    // level: a key tapped during the attack must not jump up to fade down.
    _release_from = _level;
    _stage        = Stage::Release;
    _stage_sample = 0;
}

float ToneGenerator::next_envelope_level()
{
    switch (_stage) {
        case Stage::Attack:
            _level = ramp(0.0f, 1.0f, _stage_sample, _attack_samples);
            if (++_stage_sample >= _attack_samples) {
                _stage        = Stage::Decay;
                _stage_sample = 0;
            }
            break;

        case Stage::Decay:
            _level = ramp(1.0f, TONE_HOLD_LEVEL, _stage_sample, _decay_samples);
            if (++_stage_sample >= _decay_samples) {
                _stage        = Stage::Hold;
                _stage_sample = 0;
            }
            break;

        case Stage::Hold:
            _level = TONE_HOLD_LEVEL;
            break;

        case Stage::Release:
            _level = ramp(_release_from, 0.0f, _stage_sample, _release_samples);
            if (++_stage_sample >= _release_samples) {
                _stage = Stage::Idle;
                _level = 0.0f;
            }
            break;

        case Stage::Idle:
            _level = 0.0f;
            break;
    }
    return _level;
}

size_t ToneGenerator::fill(int16_t* out, size_t capacity)
{
    size_t written = 0;
    while (written < capacity && _stage != Stage::Idle) {
        float amplitude = SAMPLE_FULL_SCALE * TONE_PEAK_AMPLITUDE * _gain * next_envelope_level();
        out[written++]  = (int16_t)std::lround(amplitude * std::sin(TWO_PI * _phase));

        // Keep the phase on [0, 1) rather than letting it grow without
        // bound: a note held for minutes would otherwise lose precision.
        _phase += _phase_step;
        if (_phase >= 1.0f) {
            _phase -= 1.0f;
        }
    }
    return written;
}

}  // namespace tuner
