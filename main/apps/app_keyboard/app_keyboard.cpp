/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_keyboard.h"
#include "assets/keyboard_big.h"
#include "assets/keyboard_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

namespace {
// Reduce power usage after kIdleTimeoutMs of no key activity.
// Screen fades out over kFadeDurationMs.
constexpr uint32_t kIdleTimeoutMs     = 86'400'000; // temp 1d // 120'000;  // temp 2 min while characterising idle draw
constexpr uint32_t kFadeDurationMs    = 5000;
constexpr uint8_t kFallbackBrightness = 255;
}  // namespace

AppKeyboard::AppKeyboard()
{
    setAppInfo().name     = "Keyboard";
    setAppInfo().userData = new AppIcon_t(image_data_keyboard_big, image_data_keyboard_small);
}

AppKeyboard::~AppKeyboard()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppKeyboard::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _info_update_time   = 0;
    _is_selecting       = true;
    _is_keyboard_active = false;

    // Create and initialize selector menu
    _selector_menu = new KeyboardSelectorMenu();
    _selector_menu->init();
    select_keyboard_type();
}

void AppKeyboard::onRunning()
{
    if (_is_selecting && _selector_menu) {
        // Update selector menu
        _selector_menu->update();

        // Check if selection is made
        if (_selector_menu->isSelected()) {
            auto keyboard_type = _selector_menu->getSelectedType();
            _is_selecting      = false;

            // Destroy selector menu
            delete _selector_menu;
            _selector_menu = nullptr;

            // Initialize selected keyboard type
            if (keyboard_type == KeyboardSelectorMenu::KEYBOARD_TYPE_BLE) {
                init_ble_keyboard();
                start_idle_tracking();
            } else if (keyboard_type == KeyboardSelectorMenu::KEYBOARD_TYPE_USB) {
                init_usb_keyboard();
            }

            render_keyboard_interface();
            _is_keyboard_active = true;
        }
    } else if (_is_keyboard_active) {
        if (_idle_logic_enabled) {
            update_idle_state();
        }
        // Skip status redraws while the display is dimming or off -- waste of SPI traffic.
        if (_idle_state == IDLE_STATE_AWAKE) {
            update_connection_info();
        }
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppKeyboard::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    // Clean up selector menu if still exists
    if (_selector_menu) {
        delete _selector_menu;
        _selector_menu = nullptr;
    }

    if (_is_keyboard_active) {
        // The best way to release everything
        // Unfortunately, a side-effect is that in-memory counters and
        // settings all get lost
        esp_restart();
        __builtin_unreachable();
    }

    close();
}

void AppKeyboard::select_keyboard_type()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.println("[Select Keyboard Type]");
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.println("Use arrow keys to select");
    GetHAL().canvas.println("Press Enter to confirm");
    GetHAL().pushCanvas();
}

void AppKeyboard::init_ble_keyboard()
{
    mclog::tagInfo(getAppInfo().name, "initializing BLE keyboard");
    GetHAL().bleKeyboardInit();
}

void AppKeyboard::init_usb_keyboard()
{
    mclog::tagInfo(getAppInfo().name, "initializing USB keyboard");
    GetHAL().usbKeyboardInit();
}

void AppKeyboard::update_connection_info()
{
    // Update connection status every 2 seconds
    if (GetHAL().millis() - _info_update_time > 1000) {
        render_connection_status();
        _info_update_time = GetHAL().millis();
    }
}

void AppKeyboard::render_keyboard_interface()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.println("Keyboard Mode Active");

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.println("Type to send keys...");
    GetHAL().pushCanvas();
}

void AppKeyboard::start_idle_tracking()
{
    _idle_logic_enabled = true;
    _idle_state         = IDLE_STATE_AWAKE;
    _last_input_time    = GetHAL().millis();

    // Piggy-back on the keyboard's onKeyEvent signal. The HAL's BLE forwarder is
    // already a separate slot; both fire independently, so the BLE keystroke
    // forwarding is unaffected by us also listening here.
    _wake_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t&) { on_user_input(); });
}

void AppKeyboard::on_user_input()
{
    _last_input_time = GetHAL().millis();
    if (_idle_state != IDLE_STATE_AWAKE) {
        wake_display();
        // Re-render so the user sees current state on the freshly-lit panel.
        render_keyboard_interface();
        render_connection_status();
    }
}

void AppKeyboard::wake_display()
{
    if (_idle_state == IDLE_STATE_ASLEEP) {
        GetHAL().display.wakeup();
    }
    // wakeup() restores whatever brightness was set before sleep() -- which in
    // our case is 0 because we faded down before sleeping. So restore explicitly.
    GetHAL().display.setBrightness(_operating_brightness);
    _idle_state = IDLE_STATE_AWAKE;
}

void AppKeyboard::update_idle_state()
{
    uint32_t now     = GetHAL().millis();
    uint32_t elapsed = now - _last_input_time;

    switch (_idle_state) {
        case IDLE_STATE_AWAKE:
            if (elapsed >= kIdleTimeoutMs) {
                // Capture brightness now so wake can restore the same value
                // (works even if a future settings UI changes the operating value).
                _operating_brightness = GetHAL().display.getBrightness();
                if (_operating_brightness == 0) {
                    _operating_brightness = kFallbackBrightness;
                }
                _fade_start_time = now;
                _idle_state      = IDLE_STATE_FADING;
            }
            break;

        case IDLE_STATE_FADING: {
            uint32_t fade_elapsed = now - _fade_start_time;
            if (fade_elapsed >= kFadeDurationMs) {
                GetHAL().display.setBrightness(0);
                GetHAL().display.sleep();  // panel-side sleep on top of backlight=0
                _idle_state = IDLE_STATE_ASLEEP;
            } else {
                uint32_t b = static_cast<uint32_t>(_operating_brightness) *
                             (kFadeDurationMs - fade_elapsed) / kFadeDurationMs;
                GetHAL().display.setBrightness(static_cast<uint8_t>(b));
            }
            break;
        }

        case IDLE_STATE_ASLEEP:
            // Wait for a key event to wake us via on_user_input().
            break;
    }
}

void AppKeyboard::render_connection_status()
{
    // Clear status area
    GetHAL().canvas.fillRect(0, 32, GetHAL().canvas.width(), 20, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 32);

    // Check both BLE and USB connections
    bool ble_connected = GetHAL().bleKeyboardIsConnected();
    bool usb_connected = GetHAL().usbKeyboardIsConnected();

    if (ble_connected) {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.println("BLE: Connected");
    } else if (usb_connected) {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.println("USB: Connected");
    } else {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("Disconnected");
    }

    auto canvas = GetHAL().canvas;
    // Slope is dV/dt across Profile's ~60s ring of burst-sampled readings.
    // Reference points (2025-11): around -7 uV/s with screen asleep,
    // around -37 uV/s with screen at full brightness.
    // KEEP? DROP? We'll want to down down the brightness...
    auto history = GetHAL().powerProfile.getBatVoltageHistory();
    auto hold = history.oldest();
    auto hnew = history.latest();
    int32_t diff_mv = (
        static_cast<int32_t>(hnew.mv) - static_cast<int32_t>(hold.mv));
    uint32_t diff_s = (hnew.time - hold.time) / 1000;
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.printf("mV  : %hu [%hu] %hu\n", GetHAL().powerProfile.getBatVoltage(),
                  GetHAL().powerProfile.getBatVoltageInstant(), history.size());
    canvas.printf("uV/s: %.1f [%ld/%lu]\n", GetHAL().powerProfile.getBatVoltageSlope_uVps(), diff_mv, diff_s);
    if (history.size()) {
        canvas.printf("dbg : %hu-%hu %lu-%lu\n", hold.mv, hnew.mv, hold.time / 1000, hnew.time / 1000);
    }

    GetHAL().pushCanvas();
}
