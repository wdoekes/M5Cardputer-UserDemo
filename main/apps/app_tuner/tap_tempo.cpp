/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "tap_tempo.h"
#include <algorithm>

namespace tuner {

namespace {

constexpr float MS_PER_MINUTE = 60000.0f;

// Intervals a window of TAP_TEMPO_MAX_TAPS presses can hold.
constexpr int MAX_INTERVALS = TAP_TEMPO_MAX_TAPS - 1;

}  // namespace

void TapTempo::clear()
{
    _count = 0;
    _bpm   = 0.0f;
}

void TapTempo::tap(uint32_t now_ms)
{
    // Too long since the last tap to be the same beat: start over rather
    // than average across the pause.
    if (_count > 0 && (now_ms - _times[_count - 1]) > TAP_TEMPO_TIMEOUT_MS) {
        _count = 0;
    }

    if (_count == TAP_TEMPO_MAX_TAPS) {
        // Window full: drop the oldest tap to make room. Ten timestamps
        // shuffled a few times a second costs nothing worth avoiding.
        for (int i = 0; i < TAP_TEMPO_MAX_TAPS - 1; ++i) {
            _times[i] = _times[i + 1];
        }
        --_count;
    }

    _times[_count++] = now_ms;
    recompute();
}

void TapTempo::recompute()
{
    int intervals = _count - 1;
    if (intervals < 1) {
        // First tap of a run: nothing to measure yet, and the previous
        // run's reading no longer describes what is being tapped.
        _bpm = 0.0f;
        return;
    }

    uint32_t sorted[MAX_INTERVALS];
    for (int i = 0; i < intervals; ++i) {
        sorted[i] = _times[i + 1] - _times[i];
    }
    std::sort(sorted, sorted + intervals);

    // Drop the fastest and the slowest, but only once that leaves something
    // in the middle to average.
    int first = 0;
    int last  = intervals;
    if (intervals > TAP_TEMPO_OUTLIERS) {
        first = TAP_TEMPO_OUTLIERS / 2;
        last -= TAP_TEMPO_OUTLIERS - first;
    }

    uint64_t total = 0;
    for (int i = first; i < last; ++i) {
        total += sorted[i];
    }

    uint32_t mean_ms = (uint32_t)(total / (last - first));
    _bpm             = (mean_ms > 0) ? (MS_PER_MINUTE / mean_ms) : 0.0f;
}

}  // namespace tuner
