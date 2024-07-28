#ifndef MODBUSTCP_H
#define MODBUSTCP_H

#include <WiFi.h>
#include <ModbusServerTCPasync.h>

#include "constants.h"
#include "global.h"
#include "ade7953.h"
#include "customtime.h"

extern AdvancedLogger logger;
extern Ade7953 ade7953;
extern CustomTime customTime;

class ModbusTcp{
public:
    ModbusTcp(int port, int serverId, int maxClients, int timeout);

    void begin();

private:
    ModbusServerTCPasync _mbServer;
    
    uint16_t _getRegisterValue(uint16_t address);
    uint16_t _getFloatBits(float value, bool high);

    bool _isValidRegister(uint16_t address);
    
    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request);

    int _port;
    int _serverId;
    int _maxClients;
    int _timeout;

    int _lowerLimitChannelRegisters;
    int _stepChannelRegisters;
    int _upperLimitChannelRegisters;
};

#endif // MODBUS_TCP_H