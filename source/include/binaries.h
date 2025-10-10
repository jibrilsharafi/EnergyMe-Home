#pragma once

#include <Arduino.h>

// Web server files
// --------------------------------------------------
// Styles
extern const char button_css[] asm("_binary_css_button_css_start");
extern const char styles_css[] asm("_binary_css_styles_css_start");
extern const char section_css[] asm("_binary_css_section_css_start");
extern const char typography_css[] asm("_binary_css_typography_css_start");

// JavaScript
extern const char api_client_js[] asm("_binary_js_api_client_js_start");

// HTML
extern const char ade7953_tester_html[] asm("_binary_html_ade7953_tester_html_start");
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

// AWS IoT Core secrets
// --------------------------------------------------
#ifdef HAS_SECRETS
extern const char aws_iot_core_cert_certclaim[] asm("_binary_secrets_certclaim_pem_start");
extern const char aws_iot_core_cert_privateclaim[] asm("_binary_secrets_privateclaim_pem_start");
extern const char preshared_encryption_key[] asm("_binary_secrets_encryptionkey_txt_start");
#else
// Empty placeholders when secrets are not available
extern const char aws_iot_core_cert_certclaim[];
extern const char aws_iot_core_cert_privateclaim[];
extern const char preshared_encryption_key[];
#endif