#pragma once

#include <Arduino.h>
#include <AdvancedLogger.h>

#include "constants.h"
#include "structs.h"

extern AdvancedLogger logger;
extern char DEVICE_ID[DEVICE_ID_BUFFER_SIZE]; // Device ID (MAC address in lowercase hex without colons)
extern Statistics statistics;
extern MainFlags mainFlags;