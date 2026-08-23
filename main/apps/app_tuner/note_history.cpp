/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#include "note_history.h"

namespace tuner {

void NoteHistory::clear()
{
    _count = 0;
}

void NoteHistory::push(int midi, Source source)
{
    const Entry* last = newest();
    if (last != nullptr && last->midi == midi && last->source == source) {
        return;
    }

    if (_count == HISTORY_CAPACITY) {
        // Full: shuffle everything down one slot to make room at the end.
        // The history is a couple of dozen entries redrawn a few times a
        // second, so a copy costs less than the index arithmetic a ring
        // buffer would push into every reader.
        for (int i = 0; i < HISTORY_CAPACITY - 1; ++i) {
            _entries[i] = _entries[i + 1];
        }
        --_count;
    }

    _entries[_count++] = {midi, source};
}

}  // namespace tuner
