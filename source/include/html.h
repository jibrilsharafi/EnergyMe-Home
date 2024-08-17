#ifndef HTML_H
#define HTML_H

#include <Arduino.h>

extern const char calibration_html[] asm("_binary_html_calibration_html_start");
extern const char channel_html[] asm("_binary_html_channel_html_start");
extern const char configuration_html[] asm("_binary_html_configuration_html_start");
extern const char index_html[] asm("_binary_html_index_html_start");
extern const char info_html[] asm("_binary_html_info_html_start");
extern const char update_failed_html[] asm("_binary_html_update_failed_html_start");
extern const char update_successful_html[] asm("_binary_html_update_successful_html_start");
extern const char update_html[] asm("_binary_html_update_html_start");

#endif