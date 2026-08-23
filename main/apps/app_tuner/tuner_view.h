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
    // Played notes are exact, so they get no tuning arrow.
    bool played = false;

    // Recent notes for the side panel. Never null.
    const NoteHistory* history = nullptr;

    // How the black keys are spelled, everywhere on screen at once. A panel
    // mixing both spellings would read as an error rather than a choice.
    note::Accidental accidental = note::Accidental::Sharp;

    // Tapped tempo, or 0 when nothing has been tapped. It takes the second
    // info line while it has a value; see tuner_view.cpp for why it shares
    // that slot with the detector's confidence.
    float bpm = 0.0f;
};

// Draws one frame and pushes it to the display.
void render(const Model& model);

}  // namespace view

}  // namespace tuner
