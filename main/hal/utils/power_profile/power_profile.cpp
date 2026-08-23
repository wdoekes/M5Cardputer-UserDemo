#include "power_profile.h"
#include <M5Unified.hpp>

#if HAL_POWER_PROFILE_ENABLED

#include <algorithm>

namespace {
    // If the voltage jumps up and down, we're not doing any load. Use
    // this to deduce that there is no battery (charging), but we're
    // attached to a power source.
    constexpr int POWERED_VOLTAGE_DIFF_MV = 20;
    // When we deduce that we're POWERED, set this value as voltage.
    constexpr int POWERED_VOLTAGE_DEFAULT_MV = 4250;

    // Get N burst samples and take the median.
    constexpr int INSTANT_SAMPLE_COUNT = 11;
}

template <typename Range, std::size_t MaxN = 64>
auto median_stack(const Range& r)
{
    using T = std::decay_t<decltype(*r.begin())>;
    std::array<T, MaxN> tmp{};
    std::size_t n = 0;
    for (const auto& v : r) { if (n < MaxN) tmp[n++] = v; }
    if (n == 0) return T{};
    auto mid = tmp.begin() + n / 2;
    std::nth_element(tmp.begin(), mid, tmp.begin() + n);
    return *mid;
}

void PowerProfile::update()
{
    uint32_t now = m5gfx::millis();

    if ((now - _last_raw_time) < RAW_SAMPLE_INTERVAL_MS) {
        return;
    }
    _last_raw_time = now;

    uint16_t raw = getBatVoltageInstant();

    if (_raw_samples.size() == 0) {
        _raw_samples.push(raw);
        return;
    }

    uint16_t prev = _raw_samples.latest();

    // If difference between this measurement and the last was large,
    // we're connected to power and not doing any load on the battery.
    int16_t diff = std::clamp(
        static_cast<int>(raw) - static_cast<int>(prev), -32767, 32767);
    if (diff < -POWERED_VOLTAGE_DIFF_MV || POWERED_VOLTAGE_DIFF_MV < diff) {
        _raw_samples.clear();
        _raw_samples.push(raw);
        _smooth_samples.clear();
        _smooth_mv = POWERED_VOLTAGE_DEFAULT_MV;
        _state = State::POWERED;
        _voltage_slope = 0.0f;
        return;
    }

    // Add this sample.
    _raw_samples.push(raw);

    // If we have only a few measurements, we have none.
    if (_raw_samples.size() <= 6) {
        return;
    }

    // Take median of raw samples.
    uint16_t new_smooth_mv = median_stack(_raw_samples);

    // Is this the first sample after starting/getting power?
    // We do NOT write _smooth_samples until we get an actual
    // transition (based off _smooth_mv).
    if (_smooth_mv == 0 || _smooth_mv == POWERED_VOLTAGE_DEFAULT_MV) {
        _smooth_samples.clear();
        _smooth_mv = new_smooth_mv;
        _state = State::UNKNOWN;
        _voltage_slope = 0.0f;
        return;
    }

    // No change? We're done.
    if (_smooth_mv == new_smooth_mv) {
        return;
    }
    
    // Set new values. This includes a transition.
    diff = std::clamp(
        static_cast<int>(new_smooth_mv) - static_cast<int>(_smooth_mv),
        -32767, 32767);
    _smooth_mv = new_smooth_mv;
    _smooth_samples.push({
        .time = now, .mv = new_smooth_mv, .diff = diff});

    // Check the last N transitions to see in which direction we're moving.
    // When charging, we'll likely see it quickly (because of easy
    // recent changes). When discharging, it might take a while to
    // notice.
    const int n_trans = 3;
    int pos = 0;
    int neg = 0;
    for (auto const& smooth_sample : _smooth_samples.tail(n_trans)) {
        // There is always a diff, otherwise it wouldn't be in the samples.
        // (And we're never pruning mid-buffer.)
        if (smooth_sample.diff > 0) {
            pos += 1;
        } else {
            neg += 1;
        }
    }
    if (neg == 0) {
        if (_state != State::CHARGING) {
            // Prune all but the last N.
            _smooth_samples.keep(n_trans);
            _state = State::CHARGING;
            _voltage_slope = 0.0f;
        }
    } else if (pos == 0) {
        if (_state != State::DISCHARGING) {
            // Prune all but the last N.
            _smooth_samples.keep(n_trans);
            _state = State::DISCHARGING;
            _voltage_slope = 0.0f;
        }
    }

    // We can calculate a slope:
    // - if the sample range is large (5+ minutes);
    // - or if we have plenty of samples (9+ samples).
    uint32_t smooth_sample_range = (
        _smooth_samples.latest().time - _smooth_samples.oldest().time);
    if (!(smooth_sample_range >= 300'000 || _smooth_samples.size() >= 9)) {
        return;
    }

    // We don't need that many samples if we cover a large range.
    // Keep 1800+ seconds, but not more.
    int prune = 0;
    for (auto const& smooth_mv : _smooth_samples) {
        if ((now - smooth_mv.time) > 1'800'000) {
            prune += 1;
        } else {
            // Prune one fewer, so we have at least 1800 seconds.
            // This also ensures we have at least 2 samples left.
            // (Because the latest sample we just added was not 1800
            // seconds ago.)
            prune -= 1;
            break;
        }
    }
    if (prune > 0) {
        _smooth_samples.prune(prune);
    }

    _voltage_slope = calculateSlope(_smooth_samples);
}

// Theil-Sen estimator
float PowerProfile::calculateSlope(SmoothVoltages const& samples)
{
    constexpr size_t max_room = (
        samples.capacity() * (samples.capacity() - 1) / 2);
    std::array<float, max_room> slopes;
    const size_t n = samples.size();
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            uint32_t dt_ms = samples[j].time - samples[i].time;
            if (dt_ms > 0) {
                // uV/s = mV/ms * 1e6
                float dv_mv = static_cast<float>(
                    static_cast<int32_t>(samples[j].mv) -
                    static_cast<int32_t>(samples[i].mv));
                slopes[k++] = dv_mv * 1e6f / static_cast<float>(dt_ms);
            }
        }
    }
    size_t half_k = k / 2;
    std::nth_element(
        slopes.begin(), slopes.begin() + half_k, slopes.begin() + k);
    return slopes[half_k];
}

#endif  // HAL_POWER_PROFILE_ENABLED

uint16_t PowerProfile::getBatVoltageInstant() const
{
#if HAL_POWER_PROFILE_ENABLED
    // Yes, this is actually needed. Otherwise the voltage skips back and forth:
    // 11 entries, seen during charging:
    // 3988 3988 3992 3992 3992 3988 3988 3992 4010 3988 3988 3988
    uint16_t buf[INSTANT_SAMPLE_COUNT];
    for (int i = 0; i < INSTANT_SAMPLE_COUNT; ++i) {
        buf[i] = M5.Power.getBatteryVoltage();
    }
    std::nth_element(
        buf, buf + INSTANT_SAMPLE_COUNT / 2, buf + INSTANT_SAMPLE_COUNT);
    return buf[INSTANT_SAMPLE_COUNT / 2];
#else  // !HAL_POWER_PROFILE_ENABLED
    return M5.Power.getBatteryVoltage();
#endif  // !HAL_POWER_PROFILE_ENABLED
}

