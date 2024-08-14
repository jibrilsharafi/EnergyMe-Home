#ifndef HTML_H
#define HTML_H

#include <Arduino.h>

const char configuration_html[] PROGMEM = R"rawliteral(
    #include "html/configuration.html"
)rawliteral";

const char calibration_html[] PROGMEM = R"rawliteral(
    #include "html/calibration.html"
)rawliteral";

const char channel_html[] PROGMEM = R"rawliteral(
    #include "html/channel.html"
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
    #include "html/index.html"
)rawliteral";

const char info_html[] PROGMEM = R"rawliteral(
    #include "html/info.html"
)rawliteral";

const char update_failed_html[] PROGMEM = R"rawliteral(
    #include "html/update_failed.html"
)rawliteral";

const char update_successful_html[] PROGMEM = R"rawliteral(
    #include "html/update_successful.html"
)rawliteral";

const char update_html[] PROGMEM = R"rawliteral(
    #include "html/update.html"
)rawliteral";

#endif