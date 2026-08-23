/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

// Live readout of Profile state: smoothed/instant mV, regression slope,
// battery percent, uptime. Useful for power-saving experiments where the
// status-bar Fn overlay is too small to see all the numbers at once.
class AppProfile : public mooncake::AppAbility {
public:
    AppProfile();
    ~AppProfile() = default;

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    uint32_t _last_render_ms = 0;
    int _key_event_slot_id   = -1;
    void render();
};
