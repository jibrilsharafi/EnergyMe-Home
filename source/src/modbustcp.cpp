#include "modbustcp.h"

ModbusTcp* modbusTcpInstance = nullptr;

ModbusTcp::ModbusTcp(int port, int serverId, int maxClients, int timeout) {
    _port = port;
    _serverId = serverId;
    _maxClients = maxClients;
    _timeout = timeout;

    // Define the range of registers for the channels
    _lowerLimitChannelRegisters = 1000;
    _stepChannelRegisters = 100;
    _upperLimitChannelRegisters = _lowerLimitChannelRegisters + (MULTIPLEXER_CHANNEL_COUNT + 1) * _stepChannelRegisters;

    modbusTcpInstance = this;
}

void ModbusTcp::begin() {
    logger.debug("Initializing Modbus TCP", "modbusTcp::begin");
    
    _mbServer.registerWorker(_serverId, READ_HOLD_REGISTER, &ModbusTcp::_handleReadHoldingRegisters);
    _mbServer.start(_port, _maxClients, _timeout);  // Port, default server ID, timeout in ms
    
    logger.debug("Modbus TCP initialized", "modbusTcp::begin");
}

ModbusMessage ModbusTcp::_handleReadHoldingRegisters(ModbusMessage request) {
    if (!modbusTcpInstance) {
        logger.error("ModbusTcp instance not initialized yet", "ModbusTcp::_handleReadHoldingRegisters");
        return ModbusMessage(request.getServerID(), request.getFunctionCode(), SERVER_DEVICE_FAILURE);
    }
    
    if (request.getFunctionCode() != READ_HOLD_REGISTER) {
        logger.debug("Invalid function code: %d", "ModbusTcp::_handleReadHoldingRegisters", request.getFunctionCode());
        return ModbusMessage(request.getServerID(), request.getFunctionCode(), ILLEGAL_FUNCTION);
    }

    // No check on the valid address is performed as it seems that the response is not correctly handled by the client
    
    uint16_t startAddress;
    uint16_t registerCount;
    request.get(2, startAddress);
    request.get(4, registerCount);
    
    // Calculate the byte count (2 bytes per register)
    uint8_t byteCount = registerCount * 2;
    
    // Create the response
    ModbusMessage response;
    response.add(request.getServerID());
    response.add(request.getFunctionCode());
    response.add(byteCount);

    // Fix this: 
    for (uint16_t i = 0; i < registerCount; i++) {
        uint16_t value = modbusTcpInstance->_getRegisterValue(startAddress + i);
        response.add(value);
    }
    
    return response;
}

// Helper function to split float into high or low 16 bits
uint16_t ModbusTcp::_getFloatBits(float value, bool high) {
    uint32_t intValue = *reinterpret_cast<uint32_t*>(&value);
    if (high) {
        return intValue >> 16;
    } else {
        return intValue & 0xFFFF;
    }
}

uint16_t ModbusTcp::_getRegisterValue(uint16_t address) {

    // The address is calculated as 1000 + 100 * channel + offset
    // All the registers here are float 32 bits, so we need to split them into two

    if (address >= _lowerLimitChannelRegisters && address < _upperLimitChannelRegisters) {
        int realAddress = address - _lowerLimitChannelRegisters;
        int channel = realAddress / _stepChannelRegisters;
        int offset = realAddress % _stepChannelRegisters;

        switch (offset) {
            case 0: return _getFloatBits(ade7953.meterValues[channel].current, true);
            case 1: return _getFloatBits(ade7953.meterValues[channel].current, false);
            case 2: return _getFloatBits(ade7953.meterValues[channel].activePower, true);
            case 3: return _getFloatBits(ade7953.meterValues[channel].activePower, false);
            case 4: return _getFloatBits(ade7953.meterValues[channel].reactivePower, true);
            case 5: return _getFloatBits(ade7953.meterValues[channel].reactivePower, false);
            case 6: return _getFloatBits(ade7953.meterValues[channel].apparentPower, true);
            case 7: return _getFloatBits(ade7953.meterValues[channel].apparentPower, false);
            case 8: return _getFloatBits(ade7953.meterValues[channel].powerFactor, true);
            case 9: return _getFloatBits(ade7953.meterValues[channel].powerFactor, false);
            case 10: return _getFloatBits(ade7953.meterValues[channel].activeEnergy, true);
            case 11: return _getFloatBits(ade7953.meterValues[channel].activeEnergy, false);
            case 12: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergy, true);
            case 13: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergy, false);
            case 14: return _getFloatBits(ade7953.meterValues[channel].apparentEnergy, true);
            case 15: return _getFloatBits(ade7953.meterValues[channel].apparentEnergy, false);
        }
    }

    switch (address) {
        // General registers
        case 0: return customTime.getUnixTime() >> 16;
        case 1: return customTime.getUnixTime() & 0xFFFF;
        case 2: return millis() >> 16;
        case 3: return millis() & 0xFFFF;

        // Voltage
        case 100: return _getFloatBits(ade7953.meterValues[0].voltage, true);
        case 101: return _getFloatBits(ade7953.meterValues[0].voltage, false);

        // Aggregated values
        // With channel 0
        case 200: return _getFloatBits(ade7953.getAggregatedActivePower(), true);
        case 201: return _getFloatBits(ade7953.getAggregatedActivePower(), false);
        case 202: return _getFloatBits(ade7953.getAggregatedReactivePower(), true);
        case 203: return _getFloatBits(ade7953.getAggregatedReactivePower(), false);
        case 204: return _getFloatBits(ade7953.getAggregatedApparentPower(), true);
        case 205: return _getFloatBits(ade7953.getAggregatedApparentPower(), false);
        case 206: return _getFloatBits(ade7953.getAggregatedPowerFactor(), true);
        case 207: return _getFloatBits(ade7953.getAggregatedPowerFactor(), false);

        // Without channel 0
        case 210: return _getFloatBits(ade7953.getAggregatedActivePower(false), true);
        case 211: return _getFloatBits(ade7953.getAggregatedActivePower(false), false);
        case 212: return _getFloatBits(ade7953.getAggregatedReactivePower(false), true);
        case 213: return _getFloatBits(ade7953.getAggregatedReactivePower(false), false);
        case 214: return _getFloatBits(ade7953.getAggregatedApparentPower(false), true);
        case 215: return _getFloatBits(ade7953.getAggregatedApparentPower(false), false);
        case 216: return _getFloatBits(ade7953.getAggregatedPowerFactor(false), true);
        case 217: return _getFloatBits(ade7953.getAggregatedPowerFactor(false), false);

        // Default case to handle unexpected addresses
        default: return (uint16_t)0;
    }
}

bool ModbusTcp::_isValidRegister(uint16_t address) { // Currently unused
    // Define valid ranges
    bool isValid = (
        (address >= 0 && address <= 3) ||  // General registers
        (address >= 100 && address <= 101) ||  // Voltage
        (address >= 200 && address <= 207) ||  // Aggregated values with channel 0
        (address >= 210 && address <= 217) ||  // Aggregated values without channel 0
        (address >= _lowerLimitChannelRegisters && address < _upperLimitChannelRegisters)  // Channel registers
    );

    return isValid;
}
