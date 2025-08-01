#pragma once

#include <AdvancedLogger.h>

#include "ade7953.h"
#include "constants.h"
#include "structs.h"

extern AdvancedLogger logger;
extern Statistics statistics;

extern char DEVICE_ID[DEVICE_ID_BUFFER_SIZE]; // Device ID (MAC address in lowercase hex without colons)