/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "tuner_config.h"
#include <cstdint>

namespace tuner {

/**
 * @brief Reads a tempo out of a run of key presses.
 *
 * Only press times go in; how long a key is held says nothing about the
 * beat. The last TAP_TEMPO_MAX_TAPS presses form the window, and the
 * fastest and slowest intervals in it are dropped before averaging so one
 * fumbled tap cannot drag the reading.
 *
 * A gap longer than TAP_TEMPO_TIMEOUT_MS ends the run: the next tap starts
 * a fresh one rather than averaging across the pause. The last reading
 * stays available after a run ends, so it can still be read off the screen.
 */
class TapTempo {
public:
    void clear();

    // Records one press. `now_ms` is when the key went down.
    void tap(uint32_t now_ms);

    // Beats per minute, or 0 before a run has produced a second tap.
    float bpm() const
    {
        return _bpm;
    }

private:
    void recompute();

    uint32_t _times[TAP_TEMPO_MAX_TAPS] = {};
    int _count                          = 0;
    float _bpm                          = 0.0f;
};

}  // namespace tuner
