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

namespace ModbusTcp
{
    void begin();
    void stop();
}