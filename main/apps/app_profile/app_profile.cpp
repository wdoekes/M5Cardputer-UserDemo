/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_profile.h"
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <hal.h>

//#include <esp32-hal-cpu.h>


using namespace mooncake;

AppProfile::AppProfile()
{
    setAppInfo().name = "Profile";
    // No icon; launcher renders a plain tile when userData is null.
}

void AppProfile::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _last_render_ms = 0;

    // M (re)marks a lap. Force an immediate redraw so the user sees the
    // counter reset without waiting for the 500 ms tick.
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) {
                return;
            }
            if (keyEvent.keyCode == KEY_M) {
                //GetHAL().powerProfile.lapMark();
                GetHAL().powerProfile.resetBatVoltageHistory();
                render();
                _last_render_ms = GetHAL().millis();
            }
        });

    GetHAL().display.setBrightness(32);
    //GetHAL().display.setBrightness(255);

    //WiFi.mode(WIFI_OFF); btStop();
    /*
     * - Below 80 MHz you cannot use Wi-Fi or Bluetooth — the radios need ≥80 MHz.
     * - 40/20/10 MHz also break Serial unless you reconfigure baud (UART clock is derived from APB).
     * - 80 MHz with Wi-Fi off is a sweet spot for "idle-ish" workloads.
     */
    //setCpuFrequencyMhz(10);

    
    render();
}

void AppProfile::onRunning()
{
    // Refresh twice a second. Faster than that just burns CPU and shows
    // ADC jitter; slower feels laggy when watching a load event.
    if (GetHAL().millis() - _last_render_ms > 500) {
        render();
        _last_render_ms = GetHAL().millis();
    }

    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppProfile::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    GetHAL().display.setBrightness(255);
}

void AppProfile::render()
{
    auto& canvas = GetHAL().canvas;

    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setCursor(0, 0);

    canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    switch (GetHAL().powerProfile.getState()) {
    case PowerProfile::State::UNKNOWN:
        canvas.println("[Charger] UNKNOWN");
        break;
    case PowerProfile::State::POWERED:
        canvas.println("[Charger] POWERED");
        break;
    case PowerProfile::State::CHARGING:
        canvas.println("[Charger] CHARGING");
        break;
    case PowerProfile::State::DISCHARGING:
        canvas.println("[Charger] DISCHARGING");
        break;
    }

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
    canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    canvas.println("lap: (press M to mark)");

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.println("[M] mark   [Home] exit");

    GetHAL().pushCanvas();
}
