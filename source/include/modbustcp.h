#pragma once

#include <ModbusServerTCPasync.h>
#include <AdvancedLogger.h>

#include "ade7953.h"
#include "customtime.h"
#include "constants.h"
#include "globals.h"

#define MODBUS_TCP_SERVER_ID 1 // The Modbus TCP server ID
#define MODBUS_TCP_MAX_CLIENTS 3 // The maximum number of clients that can connect to the Modbus TCP server
#define MODBUS_TCP_TIMEOUT (10 * 1000) // The timeout for the Modbus TCP server

// Register address mapping
#define START_REGISTERS_METER_VALUES 100 // Before this, data that is not related to energy values (like time)
#define LOWER_LIMIT_CHANNEL_REGISTERS 1000
#define STEP_CHANNEL_REGISTERS 100
#define UPPER_LIMIT_CHANNEL_REGISTERS (LOWER_LIMIT_CHANNEL_REGISTERS + (CHANNEL_COUNT) * STEP_CHANNEL_REGISTERS)

namespace ModbusTcp
{
    void begin();
    void stop();
}