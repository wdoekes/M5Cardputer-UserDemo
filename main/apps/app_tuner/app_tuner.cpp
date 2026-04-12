// FIXME: copyright?
#include "app_tuner.h"
#include "assets/tuner_big.h"
#include "assets/tuner_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace mooncake;

/* -------------------------------------------------------------------------- */
/*                              YIN pitch detection                           */
/* -------------------------------------------------------------------------- */

namespace {

// tau range covers ~57 Hz (A1-ish) up to ~1333 Hz (E6-ish). The user-facing
// range is clamped to C2..C7 in MIDI; the wider tau search just makes the
// detector robust at the edges of that band, and going down to ~57 Hz lets
// us catch low male singing voices comfortably.
constexpr int   YIN_TAU_MIN   = 12;
constexpr int   YIN_TAU_MAX   = 280;
constexpr float YIN_THRESHOLD = 0.15f;
constexpr float YIN_RMS_GATE  = 250.0f;  // skip detection below this 16-bit RMS

// Returns detected frequency in Hz, or -1 if no confident pitch found.
// `confidence_out` is set to 1 - cmnd[tau] (higher = stronger).
float yin_detect(const int16_t* buf, int len, float sample_rate, float& confidence_out)
{
    confidence_out = 0.0f;

    // Silence gate via RMS (no need for full YIN if the room is quiet).
    double sumsq = 0.0;
    for (int i = 0; i < len; ++i) {
        double s = (double)buf[i];
        sumsq += s * s;
    }
    float rms = std::sqrt(sumsq / len);
    if (rms < YIN_RMS_GATE) {
        return -1.0f;
    }

    static float diff[YIN_TAU_MAX + 1];
    static float cmnd[YIN_TAU_MAX + 1];

    // Step 1: difference function d(tau) = sum_j (x[j] - x[j+tau])^2
    diff[0] = 0.0f;
    for (int tau = 1; tau <= YIN_TAU_MAX; ++tau) {
        float sum = 0.0f;
        int   N   = len - tau;
        for (int j = 0; j < N; ++j) {
            float d = (float)buf[j] - (float)buf[j + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }

    // Step 2: cumulative mean normalized difference
    cmnd[0]       = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= YIN_TAU_MAX; ++tau) {
        running += diff[tau];
        cmnd[tau] = (running > 0.0f) ? (diff[tau] * tau / running) : 1.0f;
    }

    // Step 3: absolute threshold -- first tau in range where cmnd dips below
    // threshold; descend into the local minimum.
    int tau_est = -1;
    for (int tau = YIN_TAU_MIN; tau <= YIN_TAU_MAX; ++tau) {
        if (cmnd[tau] < YIN_THRESHOLD) {
            while (tau + 1 <= YIN_TAU_MAX && cmnd[tau + 1] < cmnd[tau]) {
                ++tau;
            }
            tau_est = tau;
            break;
        }
    }
    if (tau_est < 0) {
        return -1.0f;
    }

    // Step 4: parabolic interpolation around the minimum for sub-sample tau.
    float better_tau = (float)tau_est;
    if (tau_est > YIN_TAU_MIN && tau_est < YIN_TAU_MAX) {
        float s0    = cmnd[tau_est - 1];
        float s1    = cmnd[tau_est];
        float s2    = cmnd[tau_est + 1];
        float denom = s0 + s2 - 2.0f * s1;
        if (denom != 0.0f) {
            better_tau = (float)tau_est + (s0 - s2) / (2.0f * denom);
        }
    }

    confidence_out = 1.0f - cmnd[tau_est];
    return sample_rate / better_tau;
}

int freq_to_midi(float freq)
{
    if (freq <= 0.0f) return -1;
    return (int)std::lround(69.0f + 12.0f * std::log2(freq / 440.0f));
}

float midi_to_freq(int midi)
{
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}

// The on-board speaker gets harsh/loud as the pitch climbs, so taper the
// drive level off above C4. 60 = ~200, 96 = ~60.
uint8_t volume_for_midi(int midi)
{
    int v = 200 - std::max(0, midi - 60) * 4;
    if (v < 60)  v = 60;
    if (v > 220) v = 220;
    return (uint8_t)v;
}

// Pack 8-bit RGB into a 16-bit RGB565 colour, matching the TFT_* macros.
constexpr uint16_t rgb565(int r, int g, int b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Linearly scale each channel of an RGB565 colour by `intensity` in [0,1].
// Used by the history strip to fade older notes down to ~50% brightness so
// the eye is drawn to the newest entry first.
uint16_t scale_rgb565(uint16_t c, float intensity)
{
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5) & 0x3F;
    int b = c & 0x1F;
    r = (int)(r * intensity + 0.5f);
    g = (int)(g * intensity + 0.5f);
    b = (int)(b * intensity + 0.5f);
    if (r < 0) { r = 0; }
    if (g < 0) { g = 0; }
    if (b < 0) { b = 0; }
    if (r > 0x1F) { r = 0x1F; }
    if (g > 0x3F) { g = 0x3F; }
    if (b > 0x1F) { b = 0x1F; }
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Map a sung pitch to a tuning-feedback colour. The detected note is
// quantised to the nearest semitone (`midi`); the true `freq` may sit up to
// 50 cents above or below the centre frequency for that semitone before YIN
// would have resolved to a different note. Render that offset as:
//   on key:    cyan
//   sharp:     cyan -> pure blue at +50 cents
//   flat:      cyan -> pure red  at -50 cents
// so a singer or instrument can drift the big-note colour back to cyan as
// they pull onto the centre.
#if 0
uint16_t tuning_color(float freq, int midi)
{
    if (midi < 0 || freq <= 0.0f) {
        return TFT_CYAN;
    }
    float expected = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
    float cents    = 1200.0f * std::log2(freq / expected);
    if (cents > 50.0f)  cents = 50.0f;
    if (cents < -50.0f) cents = -50.0f;

    if (cents >= 0.0f) {
        float t = cents / 50.0f;            // 0 = cyan, 1 = blue
        int   g = (int)(255.0f * (1.0f - t));
        return rgb565(0, g, 255);
    } else {
        float t = -cents / 50.0f;           // 0 = cyan, 1 = red
        int   r = (int)(255.0f * t);
        int   gb = (int)(255.0f * (1.0f - t));
        return rgb565(r, gb, gb);
    }
}
#endif
uint16_t tuning_color(float freq, int midi)
{
    if (midi < 0 || freq <= 0.0f) {
        return TFT_CYAN;
    }

    constexpr float DEADBAND = 20.f;   // ±40% of a semitone-half = free pass
    constexpr float MAX_CENTS = 50.0f;

    float expected = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
    float cents    = 1200.0f * std::log2(freq / expected);

    float mag = std::fabs(cents);
    if (mag <= DEADBAND) {
        return TFT_CYAN;
    }
    if (mag > MAX_CENTS) mag = MAX_CENTS;

    // Remap (DEADBAND, MAX_CENTS] -> (0, 1]
    float t = (mag - DEADBAND) / (MAX_CENTS - DEADBAND);

    if (cents > 0.0f) {
        // Sharp: cyan (0,255,255) -> yellow (255,255,0) -> red (255,0,0)
        if (t < 0.5f) {
            float u = t * 2.0f;                    // 0..1 across cyan->yellow
            int r = (int)(255.0f * u);
            return rgb565(r, 255, (int)(255.0f * (1.0f - u)));
        } else {
            float u = (t - 0.5f) * 2.0f;           // 0..1 across yellow->red
            return rgb565(255, (int)(255.0f * (1.0f - u)), 0);
        }
    } else {
        // Flat: cyan (0,255,255) -> blue (0,0,255) -> purple (128,0,255)
        if (t < 0.5f) {
            float u = t * 2.0f;                    // cyan -> blue
            return rgb565(0, (int)(255.0f * (1.0f - u)), 255);
        } else {
            float u = (t - 0.5f) * 2.0f;           // blue -> purple
            return rgb565((int)(128.0f * u), 0, 255);
        }
    }
}

// Format any MIDI note (e.g. C2..C7) into `dst`. Returns dst.
const char* format_midi(int midi, char* dst, size_t dst_len)
{
    static const char* pcs[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};
    if (midi < 0) {
        std::snprintf(dst, dst_len, "--");
    } else {
        int pc     = ((midi % 12) + 12) % 12;
        int octave = midi / 12 - 1;
        std::snprintf(dst, dst_len, "%s%d", pcs[pc], octave);
    }
    return dst;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*                            Note mapping table                              */
/* -------------------------------------------------------------------------- */

// Three rows of seven naturals (C D E F G A B), one octave per row:
//   Row 1  cols 3..9: e r t y u i o   -> C6..B6
//   Row 2  cols 3..9: s d f g h j k   -> C5..B5
//   Row 3  cols 3..9: z x c v b n m   -> C4..B4
// Aa (Shift) shifts the pressed natural up a semitone (sharp); Fn shifts
// it down a semitone (flat). Every natural is fair game: E# = F, Fb = E,
// B# = C (next octave), Cb = B (previous octave).
int AppTuner::key_to_midi(KeScanCode_t code, int semitone_shift)
{
    // Pitch-class index within the row: 0=C 1=D 2=E 3=F 4=G 5=A 6=B.
    // -1 = key not on the piano.
    int pc_idx        = -1;
    int octave_base   = 0;  // MIDI value of the row's C
    switch (code) {
        // Row 3 -> C4
        case KEY_Z: pc_idx = 0; octave_base = 60; break;
        case KEY_X: pc_idx = 1; octave_base = 60; break;
        case KEY_C: pc_idx = 2; octave_base = 60; break;
        case KEY_V: pc_idx = 3; octave_base = 60; break;
        case KEY_B: pc_idx = 4; octave_base = 60; break;
        case KEY_N: pc_idx = 5; octave_base = 60; break;
        case KEY_M: pc_idx = 6; octave_base = 60; break;

        // Row 2 -> C5
        case KEY_S: pc_idx = 0; octave_base = 72; break;
        case KEY_D: pc_idx = 1; octave_base = 72; break;
        case KEY_F: pc_idx = 2; octave_base = 72; break;
        case KEY_G: pc_idx = 3; octave_base = 72; break;
        case KEY_H: pc_idx = 4; octave_base = 72; break;
        case KEY_J: pc_idx = 5; octave_base = 72; break;
        case KEY_K: pc_idx = 6; octave_base = 72; break;

        // Row 1 -> C6
        case KEY_E: pc_idx = 0; octave_base = 84; break;
        case KEY_R: pc_idx = 1; octave_base = 84; break;
        case KEY_T: pc_idx = 2; octave_base = 84; break;
        case KEY_Y: pc_idx = 3; octave_base = 84; break;
        case KEY_U: pc_idx = 4; octave_base = 84; break;
        case KEY_I: pc_idx = 5; octave_base = 84; break;
        case KEY_O: pc_idx = 6; octave_base = 84; break;

        default: return -1;
    }

    // Semitone offsets within an octave for the seven naturals C D E F G A B.
    static const int natural_semitone[7] = {0, 2, 4, 5, 7, 9, 11};
    return octave_base + natural_semitone[pc_idx] + semitone_shift;
}

/* -------------------------------------------------------------------------- */
/*                              App lifecycle                                 */
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

    _candidate_midi  = -1;
    _candidate_count = 0;
    _silence_count   = 0;

    _displayed_midi       = -1;
    _displayed_freq       = 0.0f;
    _displayed_confidence = 0.0f;

    _play_request_midi    = -1;
    _play_request_keycode = 0;
    _active_play_midi     = -1;
    _active_play_keycode  = 0;
    _play_release_pending = false;
    _audio_state          = AudioState::Listening;

    history_clear();

    audio::set_keyboard_sfx_enable(false);

    for (size_t i = 0; i < YIN_NUM_BUFS; ++i) {
        _audio_bufs[i] = new int16_t[YIN_BUF_LEN]();
    }
    _rec_idx  = 0;
    _proc_idx = 0;
    _primed   = false;

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& event) { handle_key_event(event); });

    enter_listening();
    render_interface();
}

void AppTuner::onRunning()
{
    uint32_t now = GetHAL().millis();

    switch (_audio_state) {
        case AudioState::Listening:
            // First press of a session: tear the mic down, bring the
            // speaker up, start the tone. The peripheral switch pops,
            // but the tone masks it.
            if (_play_request_midi >= 0) {
                int     midi    = _play_request_midi;
                uint8_t keycode = _play_request_keycode;
                _play_request_midi    = -1;
                _play_request_keycode = 0;

                leave_listening();
                GetHAL().speaker.begin();
                GetHAL().speaker.setVolume(volume_for_midi(midi));
                GetHAL().speaker.tone(midi_to_freq(midi), UINT32_MAX);

                _active_play_midi    = midi;
                _active_play_keycode = keycode;
                _play_start_ms       = now;
                _audio_state         = AudioState::Playing;
            } else if (GetHAL().mic.isEnabled()) {
                process_audio();
            }
            break;

        case AudioState::Playing: {
            uint32_t played_ms = now - _play_start_ms;
            bool stop_now = (_play_release_pending && played_ms >= MIN_PLAY_MS) ||
                            played_ms > MAX_PLAY_MS;
            if (stop_now) {
                // Stop the tone but leave the speaker peripheral open --
                // Cooldown holds it silent in case the user plays another
                // note within COOLDOWN_MS, in which case we skip the I2S
                // switch entirely.
                GetHAL().speaker.stop();

                _play_release_pending = false;
                _active_play_midi     = -1;
                _active_play_keycode  = 0;
                _cooldown_start_ms    = now;
                _audio_state          = AudioState::Cooldown;
            }
            break;
        }

        case AudioState::Cooldown:
            // A new press while the speaker is still warm: just retrigger
            // the tone, no I2S switch needed.
            if (_play_request_midi >= 0) {
                int     midi    = _play_request_midi;
                uint8_t keycode = _play_request_keycode;
                _play_request_midi    = -1;
                _play_request_keycode = 0;

                GetHAL().speaker.setVolume(volume_for_midi(midi));
                GetHAL().speaker.tone(midi_to_freq(midi), UINT32_MAX);

                _active_play_midi    = midi;
                _active_play_keycode = keycode;
                _play_start_ms       = now;
                _audio_state         = AudioState::Playing;
            } else if ((now - _cooldown_start_ms) >= COOLDOWN_MS) {
                // No follow-up note in time -- tear the speaker down and
                // hand the I2S back to the mic. Clear stale detection
                // state since our own tone bled in while playing.
                GetHAL().speaker.end();
                _candidate_midi  = -1;
                _candidate_count = 0;
                _silence_count   = 0;
                enter_listening();
                _audio_state = AudioState::Listening;
            }
            break;
    }

    render_interface();

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

    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(255);

    for (size_t i = 0; i < YIN_NUM_BUFS; ++i) {
        delete[] _audio_bufs[i];
        _audio_bufs[i] = nullptr;
    }

    audio::set_keyboard_sfx_enable(true);
}

/* -------------------------------------------------------------------------- */
/*                          Audio state transitions                           */
/* -------------------------------------------------------------------------- */

void AppTuner::enter_listening()
{
    // Mic and speaker share the I2S peripheral on the Cardputer; turn the
    // speaker off so we can use the mic. Speaker volume is preserved across
    // end()/begin() so it's restored when we leave the app. Caller is
    // responsible for setting _audio_state to Listening or Cooldown.
    GetHAL().speaker.end();
    GetHAL().speaker.setVolume(255);

    auto cfg = GetHAL().mic.config();
    cfg.magnification      = 128;
    cfg.noise_filter_level = 2;
    GetHAL().mic.config(cfg);
    GetHAL().mic.begin();

    _rec_idx  = 0;
    _proc_idx = 0;
    _primed   = false;
}

void AppTuner::leave_listening()
{
    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();
}

void AppTuner::process_audio()
{
    // Prime the mic queue: hand it the first two buffers so they start filling
    // before we ever try to process one. Without this, the first call below
    // would return data that's still being written.
    if (!_primed) {
        for (int i = 0; i < 2; ++i) {
            GetHAL().mic.record(_audio_bufs[_rec_idx], YIN_BUF_LEN, YIN_SAMPLERATE);
            _rec_idx = (_rec_idx + 1) % YIN_NUM_BUFS;
        }
        _primed = true;
        return;
    }

    // Queue the next buffer. mic.record() blocks until the slot it needs is
    // free, which means by the time it returns, the buffer two iterations old
    // (at _proc_idx) is fully written and no longer referenced by the driver.
    if (!GetHAL().mic.record(_audio_bufs[_rec_idx], YIN_BUF_LEN, YIN_SAMPLERATE)) {
        return;
    }
    _rec_idx = (_rec_idx + 1) % YIN_NUM_BUFS;

    float confidence = 0.0f;
    float freq       = yin_detect(_audio_bufs[_proc_idx], YIN_BUF_LEN,
                                  (float)YIN_SAMPLERATE, confidence);
    _proc_idx        = (_proc_idx + 1) % YIN_NUM_BUFS;

    int midi = -1;
    if (freq > 0.0f) {
        int m = freq_to_midi(freq);
        if (m >= DETECT_MIDI_MIN && m <= DETECT_MIDI_MAX) {
            midi = m;
        }
    }

    // Hold-time stability: a new candidate must be seen STABLE_FRAMES
    // times in a row before it replaces the displayed note. The displayed
    // note itself sticks around through silence -- it's only replaced
    // when something new is detected, or cleared when the user plays a
    // note. SILENCE_FRAMES of silence just resets candidate tracking so
    // a new sound has to re-stabilize.
    if (midi >= 0) {
        _silence_count = 0;
        if (midi == _candidate_midi && confidence > 0.9f) {
            ++_candidate_count;
        } else {
            _candidate_midi  = midi;
            _candidate_count = 1;
        }
        if (_candidate_count >= STABLE_FRAMES) {
            _displayed_midi       = midi;
            _displayed_freq       = freq;
            _displayed_confidence = confidence;
            history_push(midi, NoteSource::Detected);
        }
    } else {
        ++_silence_count;
        if (_silence_count >= SILENCE_FRAMES) {
            _candidate_midi  = -1;
            _candidate_count = 0;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                              Key handler                                   */
/* -------------------------------------------------------------------------- */

void AppTuner::handle_key_event(const Keyboard::KeyEvent_t& event)
{
    if (event.isModifier) {
        return;
    }

    if (event.state) {
        // Press: queue a play request. Allowed in Listening (cold start)
        // or Cooldown (warm retrigger -- speaker still open). Playing
        // means a previous note hasn't been released yet, so we ignore.
        if ((_audio_state != AudioState::Listening &&
             _audio_state != AudioState::Cooldown) ||
            _play_request_midi >= 0) {
            return;
        }

        auto& kb = GetHAL().keyboard;
        // Aa (Shift) = sharp (+1 st), Fn = flat (-1 st). Shift wins if both
        // are held. Pairing "shift = up" is the conventional reading; Fn,
        // sitting at the bottom-left corner, doubles as "down" because the
        // low end of a piano lives on the left.
        int semitone_shift = 0;
        if (kb.getModifierMask() & KEY_MOD_LSHIFT) {
            semitone_shift = +1;
        } else if (kb.getFnState()) {
            semitone_shift = -1;
        }

        int midi = key_to_midi(static_cast<KeScanCode_t>(event.keyCode),
                               semitone_shift);
        if (midi < 0) {
            return;
        }

        _play_request_midi    = midi;
        _play_request_keycode = event.keyCode;
        history_push(midi, NoteSource::Played);

        // The previously detected note is no longer the user's focus --
        // clear the big-note display so the screen reflects the action.
        _displayed_midi       = midi;
        _displayed_freq       = midi_to_freq(midi);
        _displayed_confidence = 1.0f;
    } else {
        // Release: if it matches the currently sounding (or just-requested)
        // note, mark it for stop. onRunning ends the tone and starts cooldown.
        if (_audio_state == AudioState::Playing &&
            event.keyCode == _active_play_keycode) {
            _play_release_pending = true;
        }
        if (_play_request_midi >= 0 &&
            event.keyCode == _play_request_keycode) {
            _play_release_pending = true;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                              Note history                                  */
/* -------------------------------------------------------------------------- */

void AppTuner::history_clear()
{
    _history_n = 0;
}

void AppTuner::history_push(int midi, NoteSource source)
{
    // Collapse consecutive duplicates regardless of source -- a held
    // detected pitch and a repeated key tap should both register once.
    if (_history_n > 0 && _history[_history_n - 1].midi == midi && _history[_history_n - 1].source == source) {
        return;
    }
    if (_history_n < HISTORY_MAX) {
        _history[_history_n++] = {midi, source};
    } else {
        for (int i = 0; i < HISTORY_MAX - 1; ++i) {
            _history[i] = _history[i + 1];
        }
        _history[HISTORY_MAX - 1] = {midi, source};
    }
}

/* -------------------------------------------------------------------------- */
/*                                 Rendering                                  */
/* -------------------------------------------------------------------------- */

// The app canvas is its own 204x109 sprite; the system bar and keyboard
// bar live in separate sprites that the HAL composes around us, so the
// canvas's (0,0) is fully visible. All layout below is canvas-local.
static constexpr int CONTENT_X_MIN = 0;
static constexpr int CONTENT_X_MAX = 204;
static constexpr int CONTENT_W     = CONTENT_X_MAX - CONTENT_X_MIN;  // 204

// History panel down the left edge: three 4-char columns of FONT_REPL (8x16).
// Each row of one column holds one entry like "C#5 "; entries flow oldest at
// the top of column 0, newest at the bottom of column 1.
static constexpr int HIST_CHAR_W   = 8;
static constexpr int HIST_ENTRY_W  = 4 * HIST_CHAR_W;          // 32 px
static constexpr int HIST_ROW_H    = 16;
static constexpr int HIST_COLS     = 3;
static constexpr int HIST_ROWS     = 6;                        // 6 * 16 = 96 px tall, fits in 109
static constexpr int HIST_FIT      = HIST_COLS * HIST_ROWS;    // 18
static constexpr int HIST_PANEL_W  = HIST_COLS * HIST_ENTRY_W; // 96

// Right-hand content region (big note / hz / piano) sits to the right of the
// history panel, centred in the remaining width.
static constexpr int RIGHT_X_MIN   = HIST_PANEL_W + 4;         // small visual gutter
static constexpr int RIGHT_X_MID   = (RIGHT_X_MIN + CONTENT_X_MAX) / 2;

// Compact one-octave piano (12 keys: 7 white + 5 black, C..B). Roughly a
// quarter of the display width so it doesn't dominate the layout.
static constexpr int PIANO_WHITES  = 7;
static constexpr int PIANO_KEY_W   = 8;                               // 7*8 = 56
static constexpr int PIANO_W       = PIANO_WHITES * PIANO_KEY_W;      // 56
static constexpr int PIANO_H       = 32;
static constexpr int PIANO_BLACK_W = 5;
static constexpr int PIANO_BLACK_H = 20;
static constexpr int PIANO_X_OFF   = RIGHT_X_MID - PIANO_W / 2;       // centred in right region
static constexpr int PIANO_Y       = 72;

// Vertical layout (canvas is 109 tall):
//   y=  0..23  big detected note (size 3, 24 tall)   -- right of history panel
//   y= 32..39  Hz/conf line (size 1, 8 tall)         -- right of history panel
//   y= 72..103 piano (32 tall)                       -- right of history panel
static constexpr int BIG_NOTE_Y = 8;
static constexpr int HZ_INFO_Y  = 40;

// Pitch class for each white-key index (0..6): C, D, E, F, G, A, B
static const int WHITE_PC[PIANO_WHITES] = {0, 2, 4, 5, 7, 9, 11};

// Black-key layout: which white-key gap they sit in (`after` = left neighbour
// white index), and their pitch class.
struct BlackKey {
    int after;
    int pc;
};
static const BlackKey BLACK_KEYS[] = {
    {0, 1},   // C# between C(0) and D(1)
    {1, 3},   // D# between D(1) and E(2)
    {3, 6},   // F# between F(3) and G(4)
    {4, 8},   // G# between G(4) and A(5)
    {5, 10},  // A# between A(5) and B(6)
};
static constexpr int NUM_BLACK = sizeof(BLACK_KEYS) / sizeof(BLACK_KEYS[0]);

void AppTuner::render_interface()
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);

    // --- History panel (most recent played + detected notes) ---
    // FONT_REPL (efontCN_16) is 8x16 monospace at size 1. Two columns down
    // the left edge: oldest at top of column 0, newest at the bottom of
    // column 1. Fixed-width 4-char entries ("C3  ", "C#3 ") so columns
    // align regardless of accidental.
    if (_history_n > 0) {
        canvas.setFont(FONT_REPL);
        canvas.setTextSize(1);

        int first = _history_n - HIST_FIT;
        if (first < 0) first = 0;
        int shown = _history_n - first;

        for (int i = 0; i < shown; ++i) {
            int col = i / HIST_ROWS;
            int row = i % HIST_ROWS;
            int x   = col * HIST_ENTRY_W;
            int y   = row * HIST_ROW_H;

            char tmp[8];
            format_midi(_history[first + i].midi, tmp, sizeof(tmp));
            char entry[16];
            std::snprintf(entry, sizeof(entry), "%-3s ", tmp);

            // Fade older entries toward 50% brightness so the newest one
            // catches the eye. i=0 is oldest, i=shown-1 is newest.
            float t = (shown > 1) ? (float)i / (float)(shown - 1) : 1.0f;
            float intensity = 0.5f + 0.5f * t;

            uint16_t base = _history[first + i].source == NoteSource::Played
                                ? (uint16_t)TFT_GREEN
                                : (uint16_t)TFT_CYAN;
            canvas.setTextColor(scale_rgb565(base, intensity), THEME_COLOR_BG);
            canvas.setCursor(x, y);
            canvas.print(entry);
        }

        // Restore the default font for the rest of the rendering.
        canvas.setFont(&fonts::Font0);
    } else {
        // Empty history -- use the panel for a quick keymap reminder until
        // the user plays or sings something. As soon as a note arrives the
        // history takes over the same area, so this fades out organically.
        canvas.setFont(FONT_REPL);
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        static const char* help_lines[] = {
            "keys:",
            "z..m C4..B4",
            "s..k C5..B5",
            "e..o C6..B6",
            "Aa : #",
            "Fn : b",
        };
        int y = 0;
        for (const char* line : help_lines) {
            canvas.setCursor(0, y);
            canvas.print(line);
            y += HIST_ROW_H;
        }
        canvas.setFont(&fonts::Font0);
    }

    bool last_played = (_history_n > 0 &&
                        _history[_history_n - 1].source == NoteSource::Played);
    // History strip uses a flat colour for both sources -- only the big note
    // gets the tuning-feedback gradient on detected pitches.
    uint16_t hist_color = last_played ? TFT_GREEN : TFT_CYAN;
    uint16_t big_color  = last_played
                              ? (uint16_t)TFT_GREEN
                              : tuning_color(_displayed_freq, _displayed_midi);

    // --- Detected/played pitch display (from YIN or keyboard) ---
    if (_displayed_midi < 0) {
        // Right region is otherwise empty before the first note. Mirror the
        // history-panel hint with a one-liner explaining what the app is
        // doing right now ("listening") and the YIN range it covers.
        canvas.setFont(FONT_REPL);
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        const char* l1 = "play or sing";
        int w1 = (int)std::strlen(l1) * 8;
        canvas.setCursor(RIGHT_X_MID - w1 / 2, 16);
        canvas.print(l1);
        const char* l2 = "ear C2..C7";
        int w2 = (int)std::strlen(l2) * 8;
        canvas.setCursor(RIGHT_X_MID - w2 / 2, 40);
        canvas.print(l2);
        canvas.setFont(&fonts::Font0);
    }
    if (_displayed_midi >= 0) {
        char name_buf[8];
        format_midi(_displayed_midi, name_buf, sizeof(name_buf));

        // Big note name, size 3: each char is 18 wide, 24 tall.
        canvas.setTextSize(3);
        canvas.setTextColor(big_color, THEME_COLOR_BG);
        int tw = (int)std::strlen(name_buf) * 18;
        int tx = RIGHT_X_MID - tw / 2;
        canvas.setCursor(tx, BIG_NOTE_Y);
        canvas.print(name_buf);

        // Hz/conf info clearly below the big name (size 1, 8 px tall).
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        char info_buf[32];
        std::snprintf(info_buf, sizeof(info_buf), "%.1f Hz", _displayed_freq);
        canvas.setCursor(RIGHT_X_MID - 4 * 6 /* width */, HZ_INFO_Y);
        canvas.print(info_buf);
        std::snprintf(info_buf, sizeof(info_buf), "conf %.2f", _displayed_confidence);
        canvas.setCursor(RIGHT_X_MID - 4 * 6 /* width */, HZ_INFO_Y + 14);
        canvas.print(info_buf);
    }

    // --- One-octave piano keyboard ---
    // Highlight follows the YIN-detected pitch class (mod 12 keys), ignoring
    // octave -- so any C2..C7 lights up the C key on screen.
    int hi_pc = (_displayed_midi >= 0) ? (((_displayed_midi % 12) + 12) % 12) : -1;

    for (int i = 0; i < PIANO_WHITES; i++) {
        int x       = PIANO_X_OFF + i * PIANO_KEY_W;
        bool active = (hi_pc == WHITE_PC[i]);
        canvas.fillRect(x + 1, PIANO_Y, PIANO_KEY_W - 2, PIANO_H - 1,
                        active ? hist_color : TFT_WHITE);
        canvas.drawRect(x, PIANO_Y, PIANO_KEY_W, PIANO_H, TFT_BLACK);
    }

    for (int i = 0; i < NUM_BLACK; i++) {
        int after   = BLACK_KEYS[i].after;
        int pc      = BLACK_KEYS[i].pc;
        int cx      = PIANO_X_OFF + (after + 1) * PIANO_KEY_W - PIANO_BLACK_W / 2;
        bool active = (hi_pc == pc);
        canvas.fillRect(cx, PIANO_Y, PIANO_BLACK_W, PIANO_BLACK_H,
                        active ? hist_color : TFT_BLACK);
        if (active) {
            canvas.drawRect(cx, PIANO_Y, PIANO_BLACK_W, PIANO_BLACK_H, TFT_BLACK);
        }
    }

    GetHAL().pushCanvas();
}
