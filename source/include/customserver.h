#ifndef CUSTOMSERVER_H
#define CUSTOMSERVER_H

#include "customwifi.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include <Ticker.h>

#include "ArduinoJson.h"

#include "ade7953.h"
#include "constants.h"
#include "led.h"
#include "utils.h"
#include "global.h"
#include "mqtt.h"
#include "html.h"
#include "css.h"

extern AdvancedLogger logger;
extern Led led;
extern Ade7953 ade7953;

void setupServer();

void _setHtmlPages();
void _setOta();
void _setRestApi();
void _setOtherEndpoints();
    
void _serverLog(const char* message, const char* function, LogLevel logLevel, AsyncWebServerRequest *request);

void _handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void _onUpdateSuccessful(AsyncWebServerRequest *request);
void _onUpdateFailed(AsyncWebServerRequest *request, const char* reason);

void _updateJsonFirmware(const char* status, const char* reason);

#endif