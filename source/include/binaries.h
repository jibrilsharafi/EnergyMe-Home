// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

// Web server files embedded via board_build.embed_txtfiles.
// Each file exposes _start / _end symbols from the linker; _end - _start
// includes the trailing '\0' that embed_txtfiles appends, hence the -1 in
// EMBEDDED_SIZE() to recover the original file size for HTTP responses.
// --------------------------------------------------

// Styles
extern const char button_css[]         asm("_binary_css_button_css_start");
extern const char button_css_end[]     asm("_binary_css_button_css_end");
extern const char forms_css[]          asm("_binary_css_forms_css_start");
extern const char forms_css_end[]      asm("_binary_css_forms_css_end");
extern const char index_css[]          asm("_binary_css_index_css_start");
extern const char index_css_end[]      asm("_binary_css_index_css_end");
extern const char styles_css[]         asm("_binary_css_styles_css_start");
extern const char styles_css_end[]     asm("_binary_css_styles_css_end");
extern const char section_css[]        asm("_binary_css_section_css_start");
extern const char section_css_end[]    asm("_binary_css_section_css_end");
extern const char tooltip_css[]        asm("_binary_css_tooltip_css_start");
extern const char tooltip_css_end[]    asm("_binary_css_tooltip_css_end");
extern const char typography_css[]     asm("_binary_css_typography_css_start");
extern const char typography_css_end[] asm("_binary_css_typography_css_end");

// JavaScript
extern const char api_client_js[]       asm("_binary_js_api_client_js_start");
extern const char api_client_js_end[]   asm("_binary_js_api_client_js_end");
extern const char chart_helpers_js[]    asm("_binary_js_chart_helpers_js_start");
extern const char chart_helpers_js_end[]asm("_binary_js_chart_helpers_js_end");
extern const char data_helpers_js[]     asm("_binary_js_data_helpers_js_start");
extern const char data_helpers_js_end[] asm("_binary_js_data_helpers_js_end");
extern const char issues_js[]           asm("_binary_js_issues_js_start");
extern const char issues_js_end[]       asm("_binary_js_issues_js_end");
extern const char power_flow_js[]       asm("_binary_js_power_flow_js_start");
extern const char power_flow_js_end[]   asm("_binary_js_power_flow_js_end");
extern const char tooltip_js[]          asm("_binary_js_tooltip_js_start");
extern const char tooltip_js_end[]      asm("_binary_js_tooltip_js_end");

// HTML
extern const char ade7953_tester_html[]    asm("_binary_html_ade7953_tester_html_start");
extern const char ade7953_tester_html_end[]asm("_binary_html_ade7953_tester_html_end");
extern const char calibration_html[]       asm("_binary_html_calibration_html_start");
extern const char calibration_html_end[]   asm("_binary_html_calibration_html_end");
extern const char channel_html[]           asm("_binary_html_channel_html_start");
extern const char channel_html_end[]       asm("_binary_html_channel_html_end");
extern const char configuration_html[]     asm("_binary_html_configuration_html_start");
extern const char configuration_html_end[] asm("_binary_html_configuration_html_end");
extern const char index_html[]             asm("_binary_html_index_html_start");
extern const char index_html_end[]         asm("_binary_html_index_html_end");
extern const char info_html[]              asm("_binary_html_info_html_start");
extern const char info_html_end[]          asm("_binary_html_info_html_end");
extern const char integrations_html[]      asm("_binary_html_integrations_html_start");
extern const char integrations_html_end[]  asm("_binary_html_integrations_html_end");
extern const char log_html[]               asm("_binary_html_log_html_start");
extern const char log_html_end[]           asm("_binary_html_log_html_end");
extern const char swagger_ui_html[]        asm("_binary_html_swagger_html_start");
extern const char swagger_ui_html_end[]    asm("_binary_html_swagger_html_end");
extern const char update_html[]            asm("_binary_html_update_html_start");
extern const char update_html_end[]        asm("_binary_html_update_html_end");
extern const char waveform_html[]          asm("_binary_html_waveform_html_start");
extern const char waveform_html_end[]      asm("_binary_html_waveform_html_end");

// Swagger / favicon
extern const char swagger_yaml[]     asm("_binary_resources_swagger_yaml_start");
extern const char swagger_yaml_end[] asm("_binary_resources_swagger_yaml_end");
extern const char favicon_svg[]      asm("_binary_resources_favicon_svg_start");
extern const char favicon_svg_end[]  asm("_binary_resources_favicon_svg_end");

// Expands to (const uint8_t* content, size_t len) - the two args expected by
// AsyncWebServerRequest::beginResponse(int, const char*, const uint8_t*, size_t).
// Streams from flash without copying into heap (vs the const char* overload,
// which copies into a String).
#define EMBEDDED(name) \
    reinterpret_cast<const uint8_t*>(name), \
    static_cast<size_t>(name##_end - name) - 1
