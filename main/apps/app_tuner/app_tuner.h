/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "note.h"
#include "note_history.h"
#include "pitch_detector.h"
#include "tuner_config.h"

#include <mooncake.h>
#include <hal/hal.h>

#include <cstdint>

/**
 * @brief Piano and chromatic tuner in one app.
 *
 * Three rows of seven naturals play one octave each, starting at
 * tuner::PLAY_LOWEST_OCTAVE:
 *   z..m  -> C4..B4
 *   s..k  -> C5..B5
 *   e..o  -> C6..B6
 * Holding Aa (Shift) sharpens the pressed natural, Fn flattens it. A note
 * sounds for as long as its key is held.
 *
 * Between key presses the app listens on the microphone, names the pitch it
 * hears and shows how far off centre it is as an arrow beside the note.
 * Played and heard notes both accumulate in the history panel.
 */
class AppTuner : public mooncake::AppAbility {
public:
    AppTuner();
    ~AppTuner();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    /**
     * The microphone and the speaker share one I2S peripheral, so only one
     * of the two can be up at a time and every switch between them costs
     * time. The app works around that with a four-state machine driven
     * entirely from onRunning(); the key handler only records what it wants
     * to happen and leaves the transitions alone.
     */
    enum class AudioState {
        Listening,  // mic up, running pitch detection frame by frame
        Playing,    // speaker up, tone ramping in and holding while its key is down
        Releasing,  // key is up, tone still sounding but fading out
        Cooldown,   // speaker up but silent, so a follow-up note needs no switch
    };

    // What the view is currently showing.
    struct Shown {
        int note           = note::NONE;
        float frequency_hz = 0.0f;
        float confidence   = 0.0f;
        bool played        = false;  // struck on the keyboard rather than heard
    };

    // A key press recorded by the key handler for onRunning() to act on.
    struct PlayRequest {
        int note         = note::NONE;
        uint8_t key_code = 0;

        bool pending() const
        {
            return note != note::NONE;
        }
        void clear()
        {
            note     = note::NONE;
            key_code = 0;
        }
    };

    int _key_event_slot_id = -1;

    // Microphone frames, rotated between the driver and the detector: while
    // the driver fills the others we run detection over the one at
    // _detect_index. Allocated for as long as the app is open.
    int16_t* _frames[tuner::MIC_FRAME_COUNT] = {};
    size_t _record_index                     = 0;
    size_t _detect_index                     = 0;
    bool _mic_primed                         = false;

    tuner::PitchDetector _detector{(float)tuner::MIC_SAMPLE_RATE_HZ, note::lower_edge_hz(tuner::DETECT_NOTE_MIN),
                                   note::upper_edge_hz(tuner::DETECT_NOTE_MAX)};
    tuner::NoteHistory _history;
    Shown _shown;

    // Note being watched for stability, and how long it has held up.
    int _candidate_note   = note::NONE;
    int _candidate_frames = 0;
    int _silent_frames    = 0;

    AudioState _audio_state = AudioState::Listening;
    PlayRequest _request;
    uint8_t _sounding_key_code    = 0;
    bool _release_pending         = false;
    uint32_t _play_started_ms     = 0;
    uint32_t _cooldown_started_ms = 0;

    // Amplitude envelope of the sounding note. _envelope is the fraction of
    // _peak_volume currently being driven; it ramps up while Playing and
    // down from _release_envelope while Releasing.
    uint8_t _peak_volume         = 0;
    float _envelope              = 0.0f;
    float _release_envelope      = 0.0f;
    uint32_t _release_started_ms = 0;

    void reset_state();
    void allocate_frames();
    void free_frames();

    void enter_listening();
    void leave_listening();
    void start_tone(uint32_t now);
    void set_envelope(float level);
    void begin_release(uint32_t now);
    void update_audio(uint32_t now);

    bool queue_frame();
    void detect_pitch();
    void forget_candidate();

    void handle_key_event(const Keyboard::KeyEvent_t& event);
    void show(int midi, float frequency_hz, float confidence, bool played);
    void render();
};
