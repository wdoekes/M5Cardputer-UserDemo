/*
 * SPDX-FileCopyrightText: 2026 Walter Doekes
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "note.h"
#include "note_history.h"

namespace tuner {

/**
 * @brief Everything the tuner draws, and the only part that touches the HAL.
 *
 * The app hands over a snapshot of what it wants shown and the view decides
 * how; it keeps no state of its own between frames.
 */
namespace view {

struct Model {
    // Note shown large, or note::NONE before anything has been played or
    // heard, which is when the help text appears instead.
    int note = note::NONE;

    // Pitch and detection strength behind `note`. For a played note these
    // are the note's own centre frequency and full confidence.
    float frequency_hz = 0.0f;
    float confidence   = 0.0f;

    // True when `note` came from a key press rather than the microphone.
    // Played notes are exact, so they skip the in-tune colouring.
    bool played = false;

    // Recent notes for the side panel. Never null.
    const NoteHistory* history = nullptr;
};

// Draws one frame and pushes it to the display.
void render(const Model& model);

}  // namespace view

}  // namespace tuner
