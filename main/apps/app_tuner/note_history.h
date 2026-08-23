/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "tuner_config.h"

namespace tuner {

/**
 * @brief The most recent notes played or heard, oldest first.
 *
 * Holds tuner::HISTORY_CAPACITY entries; pushing past that drops the oldest.
 * Consecutive pushes of the same note from the same source collapse into one,
 * so a held key or a sustained sung note takes a single slot instead of
 * flooding the panel.
 */
class NoteHistory {
public:
    enum class Source {
        Played,    // struck on the keyboard
        Detected,  // heard through the microphone
    };

    struct Entry {
        int midi      = note::NONE;
        Source source = Source::Detected;
    };

    void clear();
    void push(int midi, Source source);

    int size() const
    {
        return _count;
    }

    bool empty() const
    {
        return _count == 0;
    }

    // Index 0 is the oldest entry held, size() - 1 the newest.
    const Entry& operator[](int index) const
    {
        return _entries[index];
    }

    // Newest entry, or nullptr while the history is empty.
    const Entry* newest() const
    {
        return _count > 0 ? &_entries[_count - 1] : nullptr;
    }

private:
    Entry _entries[HISTORY_CAPACITY];
    int _count = 0;
};

}  // namespace tuner
