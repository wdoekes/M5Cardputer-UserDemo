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

#include <lwip/sockets.h>

/**
 * @brief
 *
 */
class AppWifiKbd : public mooncake::AppAbility {
public:
    AppWifiKbd();
    ~AppWifiKbd();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _key_event_raw_slot_id  = -1;
    int _key_event_slot_id  = -1;
    int _sock = -1;
    struct sockaddr_in _dest_addr;
    const char *_last_letter = NULL;
    int _last_key_row = -1;
    int _last_key_col = -1;

    void send_key_udp(int key_code, char const *key_name, int row, int col, bool pressed);

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_interface();

    /*
    enum State_t {
        STATE_INIT = 0,
        STATE_WAIT_SSID,
        STATE_WAIT_PASSWORD,
        STATE_CONNECTING,
        STATE_SUCCESS,
        STATE_FAILED
    };

    static constexpr size_t INPUT_BUFFER_SIZE     = 128;
    static constexpr uint32_t CURSOR_BLINK_PERIOD = 500;

    uint32_t _time_count         = 0;
    uint32_t _cursor_update_time = 0;
    bool _cursor_state           = false;
    std::string _input_buffer;
    State_t _current_state = STATE_INIT;
    std::string _wifi_ssid;
    std::string _wifi_password;
    bool _connection_result = false;

    void render_input_prompt();
    void render_current_input_line();
    void handle_enter_key();
    void handle_backspace();
    void update_cursor();
    void process_state_machine();
    void show_ssid_prompt();
    void show_password_prompt();
    void show_connection_status();
    void connect_to_wifi();
    void show_connection_result();
    void load_saved_wifi_settings();
    void save_wifi_settings();
    void auto_fill_saved_ssid();
    void auto_fill_saved_password();
    */
};
