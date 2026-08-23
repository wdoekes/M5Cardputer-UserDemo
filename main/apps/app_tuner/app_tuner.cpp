/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_tuner.h"
#include "assets/tuner_big.h"
#include "assets/tuner_small.h"
#include "key_notes.h"
#include "tuner_view.h"

#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>

using namespace mooncake;
using namespace tuner;

namespace {

// Semitones added to the pressed natural by the accidental modifiers.
constexpr int SHARP_SEMITONES   = +1;
constexpr int FLAT_SEMITONES    = -1;
constexpr int NATURAL_SEMITONES = 0;

// Aa (Shift) sharpens the pressed natural and Fn flattens it; Shift wins if
// both are down. Shift-means-up is the conventional reading, and Fn sits in
// the bottom-left corner, the end of a piano where the low notes are.
int accidental_semitones()
{
    Keyboard& keyboard = GetHAL().keyboard;
    if (keyboard.getModifierMask() & KEY_MOD_LSHIFT) {
        return SHARP_SEMITONES;
    }
    if (keyboard.getFnState()) {
        return FLAT_SEMITONES;
    }
    return NATURAL_SEMITONES;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*                               App lifecycle                                */
/* -------------------------------------------------------------------------- */

AppTuner::AppTuner()
{
    setAppInfo().name     = "Tuner";
    setAppInfo().userData = new AppIcon_t(image_data_tuner_big, image_data_tuner_small);
}

AppTuner::~AppTuner()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppTuner::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    reset_state();

    // Our own notes would otherwise fight with the launcher's key clicks,
    // both for the speaker and for the microphone.
    audio::set_keyboard_sfx_enable(false);

    allocate_buffers();

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& event) { handle_key_event(event); });

    enter_listening();
    render();
}

void AppTuner::onRunning()
{
    update_audio(GetHAL().millis());
    render();

    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppTuner::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    if (_audio_state == AudioState::Listening) {
        leave_listening();
    } else {
        GetHAL().speaker.stop();
        GetHAL().speaker.end();
    }

    // Free while both peripherals are down, so nothing can still be reading
    // a voice's blocks or a microphone frame.
    free_buffers();

    // Hand the speaker back the way the rest of the firmware expects it:
    // up, and at the volume the key clicks are mixed for.
    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(audio::DEFAULT_VOLUME);

    audio::set_keyboard_sfx_enable(true);
}

void AppTuner::reset_state()
{
    _history.clear();
    _shown      = Shown();
    _accidental = note::Accidental::Sharp;
    _tap_tempo.clear();

    forget_candidate();

    _request.clear();
    _cooldown_started_ms = 0;
    _audio_state         = AudioState::Listening;
}

void AppTuner::allocate_buffers()
{
    for (size_t i = 0; i < MIC_FRAME_COUNT; ++i) {
        _frames[i] = new int16_t[MIC_FRAME_SAMPLES]();
    }
    _record_index = 0;
    _detect_index = 0;
    _mic_primed   = false;

    for (size_t i = 0; i < TONE_VOICE_COUNT; ++i) {
        _voices[i].open(TONE_CHANNEL_FIRST + (int)i);
    }
}

void AppTuner::free_buffers()
{
    for (size_t i = 0; i < MIC_FRAME_COUNT; ++i) {
        delete[] _frames[i];
        _frames[i] = nullptr;
    }
    for (ToneVoice& voice : _voices) {
        voice.close();
    }
}

/* -------------------------------------------------------------------------- */
/*                            Audio state machine                             */
/* -------------------------------------------------------------------------- */

void AppTuner::enter_listening()
{
    // The speaker has to go down before the mic can come up: they share the
    // I2S peripheral. Raising the speaker volume while it is off is what the
    // record app does too -- the mic path reads back cleaner for it.
    GetHAL().speaker.end();
    GetHAL().speaker.setVolume(UINT8_MAX);

    auto config               = GetHAL().mic.config();
    config.magnification      = MIC_MAGNIFICATION;
    config.noise_filter_level = MIC_NOISE_FILTER_LEVEL;
    GetHAL().mic.config(config);
    GetHAL().mic.begin();

    _record_index = 0;
    _detect_index = 0;
    _mic_primed   = false;
    _audio_state  = AudioState::Listening;
}

void AppTuner::leave_listening()
{
    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();
}

ToneVoice* AppTuner::idle_voice()
{
    for (ToneVoice& voice : _voices) {
        if (!voice.sounding()) {
            return &voice;
        }
    }
    return nullptr;
}

bool AppTuner::any_voice_sounding() const
{
    for (const ToneVoice& voice : _voices) {
        if (voice.sounding()) {
            return true;
        }
    }
    return false;
}

void AppTuner::start_tone(uint32_t now)
{
    ToneVoice* voice = idle_voice();
    if (voice == nullptr) {
        // Every voice is still ringing. Dropping the press is the only
        // option left: a voice can be reused only once the mixer is done
        // with the blocks it already holds, and queueing a note behind them
        // would start it from whatever amplitude the old one was cut at.
        _request.clear();
        return;
    }

    // How loud a note is lives in its samples, so the master volume is
    // pinned out of the way.
    GetHAL().speaker.setVolume(TONE_MASTER_VOLUME);

    // Generate at the speaker's own rate so the mixer copies the samples
    // straight through instead of resampling them.
    voice->start(_request.note, _request.key_code, now, GetHAL().speaker.config().sample_rate);
    if (_request.released) {
        voice->release();
    }
    _request.clear();
    _audio_state = AudioState::Sounding;

    // Hand over the first blocks now rather than a frame later, so the note
    // starts on the key press instead of after it.
    voice->feed();
}

void AppTuner::update_voices(uint32_t now)
{
    for (ToneVoice& voice : _voices) {
        if (!voice.sounding()) {
            continue;
        }
        // MAX_PLAY_MS catches a release event that never arrived.
        if ((now - voice.started_ms()) > MAX_PLAY_MS) {
            voice.release();
        }
        voice.feed();
    }
}

void AppTuner::release_voices_for_key(uint8_t key_code)
{
    for (ToneVoice& voice : _voices) {
        if (voice.sounding() && voice.key_code() == key_code) {
            voice.release();
        }
    }
}

void AppTuner::update_audio(uint32_t now)
{
    switch (_audio_state) {
        case AudioState::Listening:
            if (_request.pending()) {
                // First note of a run: swap the peripheral over.
                leave_listening();
                GetHAL().speaker.begin();
                start_tone(now);
            } else if (GetHAL().mic.isEnabled()) {
                detect_pitch();
            }
            break;

        case AudioState::Sounding:
            if (_request.pending()) {
                start_tone(now);
            }
            update_voices(now);
            if (!any_voice_sounding()) {
                // Leave the speaker up: a follow-up note within COOLDOWN_MS
                // then needs no peripheral switch.
                _cooldown_started_ms = now;
                _audio_state         = AudioState::Cooldown;
            }
            break;

        case AudioState::Cooldown:
            if (_request.pending()) {
                start_tone(now);
            } else if ((now - _cooldown_started_ms) >= COOLDOWN_MS) {
                // Nothing followed: hand the I2S back to the mic. Whatever
                // the detector was tracking heard our own tone, so drop it.
                //
                // This end() thumps. It is the amplifier powering down, not
                // the waveform -- the last sample of a note is already zero
                // -- and there is no fix from this side: the mic and the
                // speaker share the I2S peripheral, so listening means
                // tearing the speaker down. What the cooldown buys is that a
                // run of notes only pays for it once, at the end.
                GetHAL().speaker.end();
                forget_candidate();
                enter_listening();
            }
            break;
    }
}

/* -------------------------------------------------------------------------- */
/*                              Pitch detection                               */
/* -------------------------------------------------------------------------- */

bool AppTuner::queue_frame()
{
    if (!GetHAL().mic.record(_frames[_record_index], MIC_FRAME_SAMPLES, MIC_SAMPLE_RATE_HZ)) {
        return false;
    }
    _record_index = (_record_index + 1) % MIC_FRAME_COUNT;
    return true;
}

void AppTuner::detect_pitch()
{
    // Prime the driver with every frame but one, so they are already filling
    // before we read the first. Without this the first frame we looked at
    // would still be being written.
    if (!_mic_primed) {
        for (size_t i = 0; i < MIC_FRAME_COUNT - 1; ++i) {
            queue_frame();
        }
        _mic_primed = true;
        return;
    }

    // record() blocks until the slot it needs is free, so by the time it
    // returns, the frame at _detect_index is fully written and the driver no
    // longer refers to it.
    if (!queue_frame()) {
        return;
    }

    PitchDetector::Result result = _detector.detect(_frames[_detect_index], MIC_FRAME_SAMPLES);
    _detect_index                = (_detect_index + 1) % MIC_FRAME_COUNT;

    int heard = note::from_frequency(result.frequency_hz);
    if (heard < DETECT_NOTE_MIN || heard > DETECT_NOTE_MAX) {
        heard = note::NONE;
    }

    if (heard == note::NONE) {
        // The note on screen is not cleared by silence -- only a new pitch
        // or a key press replaces it. Silence just means the next sound has
        // to stabilise on its own rather than continuing this run.
        if (++_silent_frames >= DETECT_SILENCE_FRAMES) {
            forget_candidate();
        }
        return;
    }

    _silent_frames = 0;
    if (heard == _candidate_note && result.confidence > DETECT_MIN_CONFIDENCE) {
        ++_candidate_frames;
    } else {
        _candidate_note   = heard;
        _candidate_frames = 1;
    }

    if (_candidate_frames >= DETECT_STABLE_FRAMES) {
        show(heard, result.frequency_hz, result.confidence, false);
        _history.push(heard, NoteHistory::Source::Detected);
    }
}

void AppTuner::forget_candidate()
{
    _candidate_note   = note::NONE;
    _candidate_frames = 0;
    _silent_frames    = 0;
}

/* -------------------------------------------------------------------------- */
/*                               Key handling                                 */
/* -------------------------------------------------------------------------- */

void AppTuner::handle_key_event(const Keyboard::KeyEvent_t& event)
{
    if (event.isModifier) {
        // Aa and Fn are the accidental modifiers for playing, so which one
        // the player reaches for says which spelling they are thinking in.
        // Follow it for the whole display. The keyboard reports Aa as
        // KEY_LEFTSHIFT and the Fn key as a modifier with no key code at
        // all -- see Keyboard::convertToKeyEvent().
        if (event.state) {
            if (event.keyCode == KEY_LEFTSHIFT) {
                _accidental = note::Accidental::Sharp;
            } else if (event.keyCode == KEY_NONE) {
                _accidental = note::Accidental::Flat;
            }
        }
        return;
    }

    if (!event.state) {
        // Release. Releasing a voice touches no hardware -- it moves the
        // envelope into its release ramp and nothing else -- so it can
        // happen right here, where starting a note cannot.
        release_voices_for_key(event.keyCode);

        // The note may not have started yet if the key was tapped and let go
        // inside one frame. Remember, so it still plays and still releases.
        if (_request.pending() && event.keyCode == _request.key_code) {
            _request.released = true;
        }
        return;
    }

    // SPACE is not a piano key, so it is free to tap out a tempo. The press
    // is what counts; the matching release says nothing about the beat.
    if (event.keyCode == KEY_SPACE) {
        _tap_tempo.tap(GetHAL().millis());
        return;
    }

    // Press. One request is carried per frame, which is far more often than
    // fingers can produce presses; onRunning() picks a voice for it, and may
    // have to switch the I2S peripheral over first.
    if (_request.pending()) {
        return;
    }

    int midi = key_to_note(static_cast<KeScanCode_t>(event.keyCode), accidental_semitones());
    if (midi == note::NONE) {
        return;
    }

    _request.note     = midi;
    _request.key_code = event.keyCode;

    // A played note is exact, so it goes up at full confidence and takes
    // over the display from whatever was last heard.
    _history.push(midi, NoteHistory::Source::Played);
    show(midi, note::to_frequency(midi), 1.0f, true);
}

/* -------------------------------------------------------------------------- */
/*                                 Rendering                                  */
/* -------------------------------------------------------------------------- */

void AppTuner::show(int midi, float frequency_hz, float confidence, bool played)
{
    _shown.note         = midi;
    _shown.frequency_hz = frequency_hz;
    _shown.confidence   = confidence;
    _shown.played       = played;
}

void AppTuner::render()
{
    view::Model model;
    model.note         = _shown.note;
    model.frequency_hz = _shown.frequency_hz;
    model.confidence   = _shown.confidence;
    model.played       = _shown.played;
    model.history      = &_history;
    model.accidental   = _accidental;
    model.bpm          = _tap_tempo.bpm();

    view::render(model);
}
