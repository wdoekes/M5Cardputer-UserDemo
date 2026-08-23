/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "pitch_detector.h"

#include <algorithm>
#include <cmath>

namespace tuner {

namespace {

// Root mean square of a frame, on the 16-bit sample scale.
float frame_rms(const int16_t* samples, size_t count)
{
    double sum_of_squares = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double sample = (double)samples[i];
        sum_of_squares += sample * sample;
    }
    return (float)std::sqrt(sum_of_squares / count);
}

}  // namespace

PitchDetector::PitchDetector(float sample_rate_hz, float min_hz, float max_hz, float threshold, float silence_rms)
    : _sample_rate_hz(sample_rate_hz), _threshold(threshold), _silence_rms(silence_rms)
{
    // A lag of tau samples is a period of sample_rate / tau, so the highest
    // frequency maps to the shortest lag and vice versa. Never let the
    // shortest lag reach 0, which would divide by zero below, or 1, which
    // leaves the parabolic interpolation without a left neighbour.
    _tau_min = std::max(2, (int)std::floor(sample_rate_hz / max_hz));
    _tau_max = std::max(_tau_min + 1, (int)std::ceil(sample_rate_hz / min_hz));

    _scratch.resize(_tau_max + 1);
}

PitchDetector::Result PitchDetector::detect(const int16_t* samples, size_t count)
{
    Result result;

    // The difference function needs at least as many samples past the lag as
    // it has lag, otherwise the longest lags are averaged over a handful of
    // samples and score as noise.
    if (samples == nullptr || count < (size_t)(2 * _tau_max)) {
        return result;
    }

    if (frame_rms(samples, count) < _silence_rms) {
        return result;
    }

    // Step 1: the difference function, d(tau) = sum_j (x[j] - x[j + tau])^2.
    // Lags below _tau_min still have to be computed: step 2 normalises each
    // lag against the running mean of every lag below it.
    _scratch[0] = 0.0f;
    for (int tau = 1; tau <= _tau_max; ++tau) {
        float sum      = 0.0f;
        size_t overlap = count - tau;
        for (size_t j = 0; j < overlap; ++j) {
            float delta = (float)samples[j] - (float)samples[j + tau];
            sum += delta * delta;
        }
        _scratch[tau] = sum;
    }

    // Step 2: cumulative mean normalised difference, in place. Each entry
    // reads its own difference value and the running total of the ones below
    // it, so overwriting as we go is safe.
    float running = 0.0f;
    for (int tau = 1; tau <= _tau_max; ++tau) {
        float difference = _scratch[tau];
        running += difference;
        _scratch[tau] = (running > 0.0f) ? (difference * tau / running) : 1.0f;
    }
    _scratch[0] = 1.0f;

    // Step 3: absolute threshold. Take the first lag that dips below the
    // threshold rather than the global minimum -- the global minimum tends
    // to sit an octave down -- and walk to the bottom of that dip.
    int tau_best = -1;
    for (int tau = _tau_min; tau <= _tau_max; ++tau) {
        if (_scratch[tau] >= _threshold) {
            continue;
        }
        while (tau + 1 <= _tau_max && _scratch[tau + 1] < _scratch[tau]) {
            ++tau;
        }
        tau_best = tau;
        break;
    }
    if (tau_best < 0) {
        return result;
    }

    // Step 4: parabolic interpolation through the minimum and its two
    // neighbours, which recovers the fraction of a sample that the integer
    // lag missed. Without it the resolution at high pitches is coarse
    // enough to swing the cents readout by tens of cents.
    float tau_refined = (float)tau_best;
    if (tau_best > _tau_min && tau_best < _tau_max) {
        float before    = _scratch[tau_best - 1];
        float at        = _scratch[tau_best];
        float after     = _scratch[tau_best + 1];
        float curvature = before + after - 2.0f * at;
        if (curvature != 0.0f) {
            tau_refined += (before - after) / (2.0f * curvature);
        }
    }

    result.frequency_hz = _sample_rate_hz / tau_refined;
    result.confidence   = 1.0f - _scratch[tau_best];
    return result;
}

}  // namespace tuner
