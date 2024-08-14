#ifndef CSS_H
#define CSS_H

#include <Arduino.h>

const char button_css[] PROGMEM = R"rawliteral(
#include "css/button.css"
)rawliteral";

const char main_css[] PROGMEM = R"rawliteral(
#include "css/main.css"
)rawliteral";

const char section_css[] PROGMEM = R"rawliteral(
#include "css/section.css"
)rawliteral";

const char typography_css[] PROGMEM = R"rawliteral(
#include "css/typography.css"
)rawliteral";

const char elements_css[] PROGMEM = R"rawliteral(
#include "css/elements.css"
)rawliteral";

#endif