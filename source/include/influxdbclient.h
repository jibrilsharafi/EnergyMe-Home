#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <CircularBuffer.hpp>
#include <HTTPClient.h>
#include <WiFi.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
#include "structs.h"
#include "utils.h"

extern AdvancedLogger logger;

namespace InfluxDbClient
{
    void begin();
    void loop();

    bool setConfiguration(JsonDocument &jsonDocument);
}