#ifndef MODBUSTCP_H
#define MODBUSTCP_H

#include <WiFi.h>
#include <ModbusServerTCPasync.h>

#include "constants.h"
#include "global.h"

extern AdvancedLogger logger;

class ModbusTcp{
public:
    ModbusTcp();
    void begin(int port = 502);
    void setVoltage(float voltage);
    void setCurrent(float current);
    void setPower(float power);
    void setEnergy(float energy);

private:
    ModbusServerTCPasync _mbServer;
    static const uint16_t VOLTAGE_REG = 0;
    static const uint16_t CURRENT_REG = 1;
    static const uint16_t POWER_REG = 2;
    static const uint16_t ENERGY_REG = 3;
    static const uint16_t NUM_REGS = 4;

    uint16_t holdingRegisters[NUM_REGS];
    
    static ModbusMessage handleReadHoldingRegisters(ModbusMessage request);
    static uint16_t floatToUint16(float value);
    static int16_t floatToInt16(float value);
};

#endif // MODBUS_TCP_H