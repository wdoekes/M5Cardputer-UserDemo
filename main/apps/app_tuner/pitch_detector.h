/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tuner {

/**
 * @brief Monophonic pitch detection with the YIN algorithm.
 *
 * YIN looks for the lag (`tau`) at which a frame most resembles itself, which
 * for a periodic signal is its period. See "YIN, a fundamental frequency
 * estimator for speech and music", de Cheveigne & Kawahara, 2002; the steps
 * below are numbered after the sections of that paper.
 *
 * The detector is configured with the frequency range it should search, not
 * with lag bounds, so callers state the range once and the class works out
 * how far to look. Instances hold a scratch buffer sized for that range, so
 * create one and reuse it rather than constructing per frame.
 */
class PitchDetector {
public:
    struct Result {
        // Detected pitch, or 0 when the frame held no confident pitch.
        float frequency_hz = 0.0f;
        // 0..1, how deep the winning minimum was. Near 1 for a clean tone.
        float confidence = 0.0f;

        bool found() const
        {
            return frequency_hz > 0.0f;
        }
    };

    // The paper's "absolute threshold": the first lag whose normalised
    // difference dips below it wins, which is what keeps YIN from locking
    // onto an octave above the true pitch. The paper suggests 0.10..0.15;
    // the high end is the forgiving one.
    static constexpr float DEFAULT_THRESHOLD = 0.15f;

    // Frames quieter than this RMS (on the 16-bit sample scale) are treated
    // as silence and skipped without running the search.
    static constexpr float DEFAULT_SILENCE_RMS = 250.0f;

    PitchDetector(float sample_rate_hz, float min_hz, float max_hz, float threshold = DEFAULT_THRESHOLD,
                  float silence_rms = DEFAULT_SILENCE_RMS);

    // Runs one frame. Frames shorter than twice the longest searched lag
    // cannot support the search and come back empty.
    Result detect(const int16_t* samples, size_t count);

    // Shortest and longest lag searched, in samples. Exposed for logging and
    // for sizing frames; the frequency range they correspond to is the one
    // passed to the constructor.
    int tau_min() const
    {
        return _tau_min;
    }
    int tau_max() const
    {
        return _tau_max;
    }

private:
    float _sample_rate_hz;
    float _threshold;
    float _silence_rms;
    int _tau_min;
    int _tau_max;

    // Scratch for one frame, indexed by lag. Holds the difference function
    // first and is normalised in place into the cumulative mean normalised
    // difference, since each entry only depends on lags at or below it.
    std::vector<float> _scratch;
};

}  // namespace tuner
