#ifndef POWER_PROFILE_H
#define POWER_PROFILE_H

// Set HAL_POWER_PROFILE_ENABLED to 0 to compile out the sampling work.
#ifndef HAL_POWER_PROFILE_ENABLED
#define HAL_POWER_PROFILE_ENABLED 1
#endif

#include <cstddef>
#include <cstdint>

#include "utils/cbuffer.h"

class PowerProfile {
public:
    struct TimedVoltage {
        uint32_t time;
        uint16_t mv;
        int16_t diff;
    };

    enum class State {
        UNKNOWN,    // Initial state
        POWERED,    // Powered by USB, not charging
        CHARGING,   // Charging
        DISCHARGING // Discharging
    };

#if HAL_POWER_PROFILE_ENABLED
    static constexpr int SMOOTH_SAMPLES = 40;
    using SmoothVoltages = CircularBuffer<TimedVoltage, SMOOTH_SAMPLES>;

    void init() {}
    void update();

    // Smoothed battery voltage (mV). (See internals for calculation.)
    uint16_t getBatVoltage() const { return _smooth_mv; }
    
    // Single fresh (burst-sampled?) reading (mV).
    uint16_t getBatVoltageInstant() const;

    // Slope of sampled voltage values, in microvolts per second.
    // Negative when discharging. Returns 0 until there are enough samples.
    float getBatVoltageSlope_uVps() const { return _voltage_slope; }

    // Returns the currently detected charge/discharge/powered state.
    State getState() const { return _state; }

    // Get history of our samples. This is for debugging/introspection only.
    const SmoothVoltages& getBatVoltageHistory() const {
        return _smooth_samples;
    }

    void resetBatVoltageHistory() {
        _state = State::UNKNOWN;
        if (_smooth_samples.size() > 1) {
            _smooth_samples.prune(_smooth_samples.size() - 1);
        }
    }

    static float calculateSlope(SmoothVoltages const& samples);

private:
    // Cadence between ring entries.
    static constexpr uint32_t RAW_SAMPLE_INTERVAL_MS = 500;
    static constexpr uint32_t SMOOTH_SAMPLE_INTERVAL_MS = 15'000;

    // Every 500 ms, for 31 items, so 15 second buffer.
    CircularBuffer<uint16_t, 31> _raw_samples;
    uint32_t _last_raw_time = 0;

    // Record a smoothed sample every time the buffer average changes.
    // So, at most one change every RAW_SAMPLE_INTERVAL_MS, but usually
    // a lot less frequent. By recording the time when it changes, we
    // get a better slope.
    SmoothVoltages _smooth_samples;
    uint16_t _smooth_mv = 0;
    State _state = State::UNKNOWN;
    float _voltage_slope = 0.0f;

#else  // !HAL_POWER_PROFILE_ENABLED
    using SmoothVoltages = CircularBuffer<TimedVoltage, 1>;

    void init() {}
    void update() {}
    uint16_t getBatVoltage() const { return getBatVoltageInstant(); }
    uint16_t getBatVoltageInstant() const;
    float getBatVoltageSlope_uVps() const { return 0; }
    State getState() const { return State::UNKNOWN; }

    const SmoothVoltages getBatVoltageHistory() const {
        return SmoothVoltages();
    }
    void resetBatVoltageHistory() {}
#endif  // !HAL_POWER_PROFILE_ENABLED
};

#endif  // POWER_PROFILE_H
