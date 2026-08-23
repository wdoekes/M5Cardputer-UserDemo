/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "tone_generator.h"
#include "tuner_config.h"
#include <cstdint>

namespace tuner {

/**
 * @brief One note being played: a generator, its blocks, and a mixer channel.
 *
 * Voices exist so that pressing a key while an earlier note is still
 * sounding starts a new note instead of dropping the press -- which is what
 * a run of notes needs, and what playing legato needs even more.
 *
 * A voice is never stolen from a note that is still going. Blocks already
 * handed to the mixer cannot be recalled, so a new note queued behind them
 * would start from whatever amplitude the old one was cut at, which is the
 * click that generating the waveform got rid of. Callers take a voice only
 * once sounding() has gone false.
 */
class ToneVoice {
public:
    // Claims `channel` for the life of the voice and allocates its blocks.
    void open(int channel);
    void close();

    // Begins `midi`, remembering which key is holding it down so a release
    // can find its way back here.
    void start(int midi, uint8_t key_code, uint32_t now_ms, uint32_t sample_rate_hz);

    // Moves the note into its release ramp. Harmless if already there.
    void release();

    // Hands the mixer as much as it will take without blocking.
    void feed();

    // True while the generator still has samples to render or the mixer
    // still holds blocks of this note.
    bool sounding() const;

    uint8_t key_code() const
    {
        return _key_code;
    }
    uint32_t started_ms() const
    {
        return _started_ms;
    }

private:
    ToneGenerator _generator;
    int _channel                       = -1;
    int16_t* _blocks[TONE_BLOCK_COUNT] = {};
    size_t _block_index                = 0;
    uint32_t _sample_rate_hz           = 0;
    uint8_t _key_code                  = 0;
    uint32_t _started_ms               = 0;
};

}  // namespace tuner
