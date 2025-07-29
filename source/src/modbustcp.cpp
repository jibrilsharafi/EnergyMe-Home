#include "modbustcp.h"

static const char *TAG = "modbustcp";

namespace ModbusTcp
{
    // Static state variables
    static ModbusServerTCPasync _mbServer;
    static int _lowerLimitChannelRegisters = 1000;
    static int _stepChannelRegisters = 100;
    static int _upperLimitChannelRegisters = _lowerLimitChannelRegisters + (CHANNEL_COUNT) * _stepChannelRegisters;

    // Private function declarations
    static unsigned int _getRegisterValue(unsigned int address);
    static unsigned int _getFloatBits(float value, bool high);
    static bool _isValidRegister(unsigned int address);
    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request);

    void begin()
    {
        logger.debug("Initializing Modbus TCP", TAG);
        
        _mbServer.registerWorker(MODBUS_TCP_SERVER_ID, READ_HOLD_REGISTER, &_handleReadHoldingRegisters);
        _mbServer.start(MODBUS_TCP_PORT, MODBUS_TCP_MAX_CLIENTS, MODBUS_TCP_TIMEOUT);
        
        logger.debug("Modbus TCP initialized", TAG);
    }

    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request)
    {
        if (request.getFunctionCode() != READ_HOLD_REGISTER)
        {
            logger.debug("Invalid function code: %d", TAG, request.getFunctionCode());
            statistics.modbusRequestsError++;
            return ModbusMessage(request.getServerID(), request.getFunctionCode(), ILLEGAL_FUNCTION);
        }

        // No check on the valid address is performed as it seems that the response is not correctly handled by the client
        
        unsigned int startAddress;
        unsigned int registerCount;
        request.get(2, startAddress);
        request.get(4, registerCount);
        
        // Calculate the byte count (2 bytes per register)
        unsigned char byteCount = registerCount * 2;
        
        // Create the response
        ModbusMessage response;
        response.add(request.getServerID());
        response.add(request.getFunctionCode());
        response.add(byteCount);

        // Add register values to response
        for (unsigned int i = 0; i < registerCount; i++)
        {
            unsigned int value = _getRegisterValue(startAddress + i);
            response.add(value);
        }
        
        statistics.modbusRequests++;
        return response;
    }

    // Helper function to split float into high or low 16 bits
    static unsigned int _getFloatBits(float value, bool high)
    {
        unsigned long intValue = *reinterpret_cast<unsigned long*>(&value);
        if (high)
        {
            return intValue >> 16;
        }
        else
        {
            return intValue & 0xFFFF;
        }
    }

    static unsigned int _getRegisterValue(unsigned int address)
    {
        // The address is calculated as 1000 + 100 * channel + offset
        // All the registers here are float 32 bits, so we need to split them into two

        if (address >= _lowerLimitChannelRegisters && address < _upperLimitChannelRegisters)
        {
            int realAddress = address - _lowerLimitChannelRegisters;
            int channel = realAddress / _stepChannelRegisters;
            int offset = realAddress % _stepChannelRegisters;

            switch (offset)
            {
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
                case 10: return _getFloatBits(ade7953.meterValues[channel].activeEnergyImported, true);
                case 11: return _getFloatBits(ade7953.meterValues[channel].activeEnergyImported, false);
                case 12: return _getFloatBits(ade7953.meterValues[channel].activeEnergyExported, true);
                case 13: return _getFloatBits(ade7953.meterValues[channel].activeEnergyExported, false);
                case 14: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergyImported, true);
                case 15: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergyImported, false);
                case 16: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergyExported, true);
                case 17: return _getFloatBits(ade7953.meterValues[channel].reactiveEnergyExported, false);
                case 18: return _getFloatBits(ade7953.meterValues[channel].apparentEnergy, true);
                case 19: return _getFloatBits(ade7953.meterValues[channel].apparentEnergy, false);
            }
        }

        switch (address)
        {
            // General registers
            case 0: return CustomTime::getUnixTime() >> 16;
            case 1: return CustomTime::getUnixTime() & 0xFFFF;
            case 2: return millis64() >> 16;
            case 3: return millis64() & 0xFFFF;  
            
            // Voltage
            case 100: return _getFloatBits(ade7953.meterValues[CHANNEL_0].voltage, true);
            case 101: return _getFloatBits(ade7953.meterValues[CHANNEL_0].voltage, false);

            // Grid frequency
            case 102: return _getFloatBits(ade7953.getGridFrequency(), true);
            case 103: return _getFloatBits(ade7953.getGridFrequency(), false);

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
            default: return (unsigned int)0;
        }
    }

    static bool _isValidRegister(unsigned int address) // Currently unused
    {
        // Define valid ranges
        bool isValid = (
            (address >= 0 && address <= 3) ||  // General registers
            (address >= 100 && address <= 103) ||  // Voltage and grid frequency
            (address >= 200 && address <= 207) ||  // Aggregated values with channel 0
            (address >= 210 && address <= 217) ||  // Aggregated values without channel 0
            (address >= _lowerLimitChannelRegisters && address < _upperLimitChannelRegisters)  // Channel registers
        );

        return isValid;
    }

} // namespace ModbusTcp
