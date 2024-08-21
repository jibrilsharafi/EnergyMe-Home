#ifndef BINARIES_H
#define BINARIES_H

#include <Arduino.h>

extern const char button_css[] asm("_binary_css_button_css_start");
extern const char styles_css[] asm("_binary_css_styles_css_start");
extern const char section_css[] asm("_binary_css_section_css_start");
extern const char typography_css[] asm("_binary_css_typography_css_start");

extern const char calibration_html[] asm("_binary_html_calibration_html_start");
extern const char channel_html[] asm("_binary_html_channel_html_start");
extern const char configuration_html[] asm("_binary_html_configuration_html_start");
extern const char index_html[] asm("_binary_html_index_html_start");
extern const char info_html[] asm("_binary_html_info_html_start");
extern const char update_html[] asm("_binary_html_update_html_start");

extern const char favicon_ico[] asm("_binary_resources_favicon_ico_start");

extern const char default_calibration_json[] asm("_binary_config_calibration_json_start");
extern const char default_channel_json[] asm("_binary_config_channel_json_start");

#endif