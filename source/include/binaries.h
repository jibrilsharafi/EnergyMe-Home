#pragma once

#include <Arduino.h>

// Web server files
// --------------------------------------------------
// Styles
extern const char button_css[] asm("_binary_css_button_css_start");
extern const char styles_css[] asm("_binary_css_styles_css_start");
extern const char section_css[] asm("_binary_css_section_css_start");
extern const char typography_css[] asm("_binary_css_typography_css_start");

// HTML
extern const char calibration_html[] asm("_binary_html_calibration_html_start");
extern const char channel_html[] asm("_binary_html_channel_html_start");
extern const char configuration_html[] asm("_binary_html_configuration_html_start");
extern const char index_html[] asm("_binary_html_index_html_start");
extern const char info_html[] asm("_binary_html_info_html_start");
extern const char log_html[] asm("_binary_html_log_html_start");
extern const char swagger_ui_html[] asm("_binary_html_swagger_html_start");
extern const char update_html[] asm("_binary_html_update_html_start");

// Swagger UI
extern const char swagger_yaml[] asm("_binary_resources_swagger_yaml_start");
extern const char favicon_svg[] asm("_binary_resources_favicon_svg_start");


// Default configuration files
// --------------------------------------------------
extern const char default_config_calibration_json[] asm("_binary_config_calibration_json_start");
extern const char default_config_channel_json[] asm("_binary_config_channel_json_start");


// AWS IoT Core secrets
// --------------------------------------------------
extern const char aws_iot_core_cert_ca[] asm("_binary_secrets_ca_pem_start");
extern const char aws_iot_core_cert_certclaim[] asm("_binary_secrets_certclaim_pem_start");
extern const char aws_iot_core_cert_privateclaim[] asm("_binary_secrets_privateclaim_pem_start");
extern const char aws_iot_core_endpoint[] asm("_binary_secrets_endpoint_txt_start");
extern const char aws_iot_core_rulemeter[] asm("_binary_secrets_rulemeter_txt_start");

extern const char preshared_encryption_key[] asm("_binary_secrets_encryptionkey_txt_start");