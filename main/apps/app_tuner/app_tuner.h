// FIXME: copyright?
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>
#include <string>

/**
 * @brief Piano/tuner app with built-in pitch detection.
 *
 * Three rows of seven naturals (C D E F G A B), one octave per row:
 *   z..m  -> C4..B4
 *   s..k  -> C5..B5
 *   e..o  -> C6..B6
 *   Hold Aa (Shift) for a sharp (+1 semitone), Fn for a flat (-1 semitone).
 *
 * Note duration follows the physical key: the tone plays for as long as
 * the key is held.
 *
 * Listens to the microphone in between key presses and identifies the
 * heard pitch via YIN.
 */
class AppTuner : public mooncake::AppAbility {
public:
    AppTuner();
    ~AppTuner();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _key_event_slot_id = -1;

    // --- Mic listening / pitch detection ---
    // 1024 samples at 16 kHz = 64 ms per frame, gives a few periods of
    // headroom even for the lowest detected pitches.
    static constexpr size_t YIN_BUF_LEN    = 1024;
    static constexpr size_t YIN_NUM_BUFS   = 3;     // >=3 so we can safely read while two fill
    static constexpr size_t YIN_SAMPLERATE = 16000;

    // Display range: detection extends down past the played range so male
    // singing voices (bass/baritone) register.
    static constexpr int DETECT_MIDI_MIN = 36;  // C2 (~65 Hz)
    static constexpr int DETECT_MIDI_MAX = 96;  // C7

    int16_t* _audio_bufs[YIN_NUM_BUFS] = {nullptr, nullptr, nullptr};
    size_t   _rec_idx                  = 0;        // next buffer to queue to mic
    size_t   _proc_idx                 = 0;        // next buffer to feed to YIN
    bool     _primed                   = false;    // initial queue priming done

    // Two-stage stability: a candidate must be seen STABLE_FRAMES in a row
    // before it overwrites _displayed_*; silence for SILENCE_FRAMES resets
    // candidate tracking. The displayed note itself persists until a new
    // pitch is detected or a key is played.
    static constexpr int STABLE_FRAMES  = 2;
    static constexpr int SILENCE_FRAMES = 5;

    int   _candidate_midi  = -1;
    int   _candidate_count = 0;
    int   _silence_count   = 0;

    int   _displayed_midi       = -1;
    float _displayed_freq       = 0.0f;
    float _displayed_confidence = 0.0f;

    // Audio path is shared between mic and speaker on the Cardputer's I2S
    // peripheral, so we run a small state machine. All transitions happen
    // in onRunning -- the key handler only sets request flags. LISTENING
    // fills audio buffers and runs YIN. PLAYING holds a continuous tone
    // for as long as the triggering key is held. COOLDOWN keeps the
    // speaker quiet for COOLDOWN_MS after stop() before tearing down the
    // speaker and re-enabling the mic, so the I2S transition doesn't pop.
    enum class AudioState { Listening, Playing, Cooldown };
    AudioState _audio_state = AudioState::Listening;

    static constexpr uint32_t COOLDOWN_MS = 250;
    static constexpr uint32_t MAX_PLAY_MS = 5000;  // safety cap on stuck keys
    // Minimum tone duration -- keys released faster than this still play out
    // to MIN_PLAY_MS to give the user something audible and avoid the click
    // that a near-instant tone()->stop() produces on the I2S amp. Modeled on
    // the keyboard SFX tunes (~80 ms perceived envelope before tear-down).
    static constexpr uint32_t MIN_PLAY_MS = 150; // bs.. is 0.15 in de code!

    // Pending request from key handler -> onRunning.
    int      _play_request_midi    = -1;
    uint8_t  _play_request_keycode = 0;

    // Currently sounding note (Playing state).
    int      _active_play_midi     = -1;
    uint8_t  _active_play_keycode  = 0;
    uint32_t _play_start_ms        = 0;
    bool     _play_release_pending = false;

    // When we entered Cooldown.
    uint32_t _cooldown_start_ms    = 0;

    void enter_listening();
    void leave_listening();
    void process_audio();

    void handle_key_event(const Keyboard::KeyEvent_t& event);
    void render_interface();

    // Returns MIDI note for a key code, or -1 if not a piano key.
    // `semitone_shift` is added to the natural's MIDI: +1 for Aa/Shift
    // (sharp), -1 for Fn (flat), 0 for plain.
    static int key_to_midi(KeScanCode_t code, int semitone_shift);

    // --- Played/detected note history (rendered as a colored string) ---
    enum class NoteSource { Played, Detected };
    struct NoteHistoryEntry {
        int        midi;
        NoteSource source;
    };
    static constexpr int HISTORY_ROWS = 6;
    static constexpr int HISTORY_COLS = 3;
    static constexpr int HISTORY_MAX = HISTORY_ROWS * HISTORY_COLS;
    NoteHistoryEntry _history[HISTORY_MAX] = {};
    int              _history_n            = 0;

    void history_push(int midi, NoteSource source);
    void history_clear();
};
