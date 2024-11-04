#pragma once

#include <WiFi.h>
#include <ModbusServerTCPasync.h>
#include <AdvancedLogger.h>

#include "ade7953.h"
#include "customtime.h"
#include "constants.h"

class ModbusTcp
{
public:
    ModbusTcp(
        int port,
        int serverId,
        int maxClients,
        int timeout,
        AdvancedLogger &logger,
        Ade7953 &ade7953,
        CustomTime &customTime);

    void begin();

private:
    ModbusServerTCPasync _mbServer;

    uint16_t _getRegisterValue(uint16_t address);
    uint16_t _getFloatBits(float value, bool high);

    bool _isValidRegister(uint16_t address);

    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request); // Must resturn a ModbusMessage as requested by registerWorker() method

    int _port;
    int _serverId;
    int _maxClients;
    int _timeout;

    AdvancedLogger &_logger;
    Ade7953 &_ade7953;
    CustomTime &_customTime;

    int _lowerLimitChannelRegisters;
    int _stepChannelRegisters;
    int _upperLimitChannelRegisters;
};