/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>
#include <string>
#include <vector>
#include <smooth_ui_toolkit.h>

/**
 * @brief
 *
 */
class KeyboardSelectorMenu : public smooth_ui_toolkit::SmoothSelectorMenu {
public:
    enum KeyboardType_t {
        KEYBOARD_TYPE_NONE = 0,
        KEYBOARD_TYPE_BLE,
        KEYBOARD_TYPE_USB,
    };

    void init();
    void onReadInput() override;
    void onRender() override;
    void onClick() override;

    bool isSelected() const
    {
        return _is_selected;
    }

    KeyboardType_t getSelectedType() const
    {
        return _keyboard_type;
    }

private:
    bool _is_selected             = false;
    KeyboardType_t _keyboard_type = KEYBOARD_TYPE_NONE;
};

/**
 * @brief
 *
 */
class AppKeyboard : public mooncake::AppAbility {
public:
    AppKeyboard();
    ~AppKeyboard();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum IdleState_t {
        IDLE_STATE_AWAKE = 0,
        IDLE_STATE_FADING,
        IDLE_STATE_ASLEEP,
    };

    uint32_t _info_update_time           = 0;
    bool _is_selecting                   = true;
    bool _is_keyboard_active             = false;
    KeyboardSelectorMenu* _selector_menu = nullptr;

    // Idle / display-sleep tracking (BLE mode only -- USB mode is wall-powered).
    bool _idle_logic_enabled      = false;
    IdleState_t _idle_state       = IDLE_STATE_AWAKE;
    uint32_t _last_input_time     = 0;
    uint32_t _fade_start_time     = 0;
    uint8_t _operating_brightness = 0;
    int _wake_slot_id             = -1;

    void select_keyboard_type();
    void init_ble_keyboard();
    void init_usb_keyboard();
    void update_connection_info();
    void render_keyboard_interface();
    void render_connection_status();

    void start_idle_tracking();
    void update_idle_state();
    void on_user_input();
    void wake_display();
};
