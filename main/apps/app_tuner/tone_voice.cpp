/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "tone_voice.h"
#include "note.h"
#include <hal/hal.h>

namespace tuner {

void ToneVoice::open(int channel)
{
    _channel = channel;
    for (size_t i = 0; i < TONE_BLOCK_COUNT; ++i) {
        _blocks[i] = new int16_t[TONE_BLOCK_SAMPLES]();
    }
    _block_index = 0;
    _key_code    = 0;
    _started_ms  = 0;
    _generator   = ToneGenerator();
}

void ToneVoice::close()
{
    for (size_t i = 0; i < TONE_BLOCK_COUNT; ++i) {
        delete[] _blocks[i];
        _blocks[i] = nullptr;
    }
    _channel = -1;
}

void ToneVoice::start(int midi, uint8_t key_code, uint32_t now_ms, uint32_t sample_rate_hz)
{
    _sample_rate_hz = sample_rate_hz;
    _key_code       = key_code;
    _started_ms     = now_ms;
    _generator.start(note::to_frequency(midi), gain_for_note(midi), sample_rate_hz);
}

void ToneVoice::release()
{
    _generator.release();
}

void ToneVoice::feed()
{
    // playRaw() blocks until the reserved slot frees, so only call it while
    // one is free; otherwise a frame would stall waiting on the mixer.
    while (GetHAL().speaker.isPlaying(_channel) < SPEAKER_SLOTS_PER_CHANNEL) {
        int16_t* block = _blocks[_block_index];
        size_t count   = _generator.fill(block, TONE_BLOCK_SAMPLES);
        if (count == 0) {
            return;
        }
        _block_index = (_block_index + 1) % TONE_BLOCK_COUNT;
        GetHAL().speaker.playRaw(block, count, _sample_rate_hz, false, 1, _channel, false);
    }
}

bool ToneVoice::sounding() const
{
    // The generator runs out first; the mixer keeps going until the blocks
    // already handed over have played.
    return !_generator.finished() || GetHAL().speaker.isPlaying(_channel) > 0;
}

}  // namespace tuner
