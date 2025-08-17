#include "modbustcp.h"

namespace ModbusTcp
{
    // Static state variables
    static ModbusServerTCPasync _mbServer;

    // Private function declarations
    static uint16_t _getRegisterValue(uint32_t address);
    static uint16_t _getFloatBits(float value, bool high);
    static bool _isValidRegister(uint32_t address);
    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request);

    void begin()
    {
        LOG_DEBUG("Initializing Modbus TCP");
        
        _mbServer.registerWorker(MODBUS_TCP_SERVER_ID, READ_HOLD_REGISTER, &_handleReadHoldingRegisters);
        _mbServer.start(MODBUS_TCP_PORT, MODBUS_TCP_MAX_CLIENTS, MODBUS_TCP_TIMEOUT);
        
        LOG_DEBUG("Modbus TCP initialized");
    }

    void stop()
    {
        LOG_DEBUG("Stopping Modbus TCP server");
        _mbServer.stop();
        LOG_INFO("Modbus TCP server stopped");
    }

    static ModbusMessage _handleReadHoldingRegisters(ModbusMessage request)
    {
        if (request.getFunctionCode() != READ_HOLD_REGISTER)
        {
            LOG_WARNING("Invalid function code: %d", request.getFunctionCode());
            statistics.modbusRequestsError++;
            ModbusMessage errorResponse;
            errorResponse.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_FUNCTION);
            return errorResponse;
        }

        // Get 16-bit values from request (big-endian format)
        uint16_t startAddress, registerCount;
        request.get(2, startAddress);   // Start address (2 bytes)
        request.get(4, registerCount);    // Register count (2 bytes)

        // Validate register count (Modbus standard allows max 125 registers)
        if (registerCount == 0 || registerCount > 125)
        {
            LOG_WARNING("Invalid register count: %u - returning ILLEGAL_DATA_VALUE", registerCount);
            statistics.modbusRequestsError++;
            ModbusMessage errorResponse;
            errorResponse.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
            return errorResponse;
        }
        
        // Check if the entire range is valid before processing
        for (uint32_t i = 0; i < registerCount; i++)
        {
            if (!_isValidRegister(startAddress + i))
            {
                statistics.modbusRequestsError++;
                ModbusMessage errorResponse;
                errorResponse.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
                return errorResponse;
            }
        }
        
        // Calculate the byte count (2 bytes per register)
        uint8_t byteCount = (uint8_t)(registerCount * 2);
        
        // Create the response
        ModbusMessage response;
        response.add(request.getServerID());
        response.add(request.getFunctionCode());
        response.add(byteCount);

        // Add register values to response
        for (uint32_t i = 0; i < registerCount; i++)
        {
            response.add(_getRegisterValue(startAddress + i));
        }
        
        statistics.modbusRequests++;
        LOG_VERBOSE("Modbus TCP request handled: Server ID: %d, Function Code: %d, Start Address: %u, Register Count: %u", 
                    request.getServerID(), request.getFunctionCode(), startAddress, registerCount);
        return response;
    }

    // Helper function to split float into high or low 16 bits
    static uint16_t _getFloatBits(float value, bool high)
    {
        uint32_t intValue = 0;
        memcpy(&intValue, &value, sizeof(uint32_t));
        if (high) return (uint16_t)(intValue >> 16);
        return (uint16_t)(intValue & 0xFFFF);
    }

    static uint16_t _getRegisterValue(uint32_t address)
    {
        // The address is calculated as 1000 + 100 * channel + offset
        // All the registers here are float 32 bits, so we need to split them into two

        if (address < START_REGISTERS_METER_VALUES) {
            switch (address)
            {
                // General registers - 64-bit values split into 4x16-bit registers each
                // Unix timestamp (seconds)
                case 0: return (CustomTime::getUnixTime() >> 48) & 0xFFFF;  // Bits 63-48
                case 1: return (CustomTime::getUnixTime() >> 32) & 0xFFFF;  // Bits 47-32
                case 2: return (CustomTime::getUnixTime() >> 16) & 0xFFFF;  // Bits 31-16
                case 3: return CustomTime::getUnixTime() & 0xFFFF;          // Bits 15-0
                
                // System uptime (milliseconds)
                case 4: return (millis64() >> 48) & 0xFFFF;  // Bits 63-48
                case 5: return (millis64() >> 32) & 0xFFFF;  // Bits 47-32
                case 6: return (millis64() >> 16) & 0xFFFF;  // Bits 31-16
                case 7: return millis64() & 0xFFFF;          // Bits 15-0 

                // Default case to handle unexpected addresses
                default: return 0;
            }
        } else if (address >= START_REGISTERS_METER_VALUES && address < LOWER_LIMIT_CHANNEL_REGISTERS) {
            // Handle special registers for voltage and grid frequency
            // These are not channel-specific, so we handle them separately

            switch (address)
            {   
                // Voltage
                case 100:   {
                                MeterValues meterValuesZeroChannel;
                                if (!Ade7953::getMeterValues(meterValuesZeroChannel, 0)) {
                                    LOG_WARNING("Failed to get meter values for channel 0. Returning default 0");
                                    return 0;
                                }
                                return _getFloatBits(meterValuesZeroChannel.voltage, true);
                            }
                case 101: {
                                MeterValues meterValuesZeroChannel;
                                if (!Ade7953::getMeterValues(meterValuesZeroChannel, 0)) {
                                    LOG_WARNING("Failed to get meter values for channel 0. Returning default 0");
                                    return 0;
                                }
                                return _getFloatBits(meterValuesZeroChannel.voltage, false);
                            }

                // Grid frequency
                case 102: return _getFloatBits(Ade7953::getGridFrequency(), true);
                case 103: return _getFloatBits(Ade7953::getGridFrequency(), false);

                // Aggregated values
                // With channel 0
                case 200: return _getFloatBits(Ade7953::getAggregatedActivePower(), true);
                case 201: return _getFloatBits(Ade7953::getAggregatedActivePower(), false);
                case 202: return _getFloatBits(Ade7953::getAggregatedReactivePower(), true);
                case 203: return _getFloatBits(Ade7953::getAggregatedReactivePower(), false);
                case 204: return _getFloatBits(Ade7953::getAggregatedApparentPower(), true);
                case 205: return _getFloatBits(Ade7953::getAggregatedApparentPower(), false);
                case 206: return _getFloatBits(Ade7953::getAggregatedPowerFactor(), true);
                case 207: return _getFloatBits(Ade7953::getAggregatedPowerFactor(), false);

                // Without channel 0
                case 210: return _getFloatBits(Ade7953::getAggregatedActivePower(false), true);
                case 211: return _getFloatBits(Ade7953::getAggregatedActivePower(false), false);
                case 212: return _getFloatBits(Ade7953::getAggregatedReactivePower(false), true);
                case 213: return _getFloatBits(Ade7953::getAggregatedReactivePower(false), false);
                case 214: return _getFloatBits(Ade7953::getAggregatedApparentPower(false), true);
                case 215: return _getFloatBits(Ade7953::getAggregatedApparentPower(false), false);
                case 216: return _getFloatBits(Ade7953::getAggregatedPowerFactor(false), true);
                case 217: return _getFloatBits(Ade7953::getAggregatedPowerFactor(false), false);

                // Default case to handle unexpected addresses
                default: return 0;
            }
        } else if (address >= LOWER_LIMIT_CHANNEL_REGISTERS && address < UPPER_LIMIT_CHANNEL_REGISTERS) {
            // Handle channel-specific registers, and thus we need to calculate the channel and offset
            // to avoid manual mapping of all registers
            int32_t realAddress = address - LOWER_LIMIT_CHANNEL_REGISTERS;
            uint8_t channel = STEP_CHANNEL_REGISTERS ? (uint8_t)(realAddress / STEP_CHANNEL_REGISTERS) : 0;
            int32_t offset = realAddress % STEP_CHANNEL_REGISTERS;

            MeterValues meterValues;
            if (!Ade7953::getMeterValues(meterValues, channel)) {
                LOG_WARNING("Failed to get meter values for channel %d. Returning default 0", channel);
                return 0;
            }

            switch (offset)
            {
                case 0: return _getFloatBits(meterValues.current, true);
                case 1: return _getFloatBits(meterValues.current, false);
                case 2: return _getFloatBits(meterValues.activePower, true);
                case 3: return _getFloatBits(meterValues.activePower, false);
                case 4: return _getFloatBits(meterValues.reactivePower, true);
                case 5: return _getFloatBits(meterValues.reactivePower, false);
                case 6: return _getFloatBits(meterValues.apparentPower, true);
                case 7: return _getFloatBits(meterValues.apparentPower, false);
                case 8: return _getFloatBits(meterValues.powerFactor, true);
                case 9: return _getFloatBits(meterValues.powerFactor, false);
                case 10: return _getFloatBits(meterValues.activeEnergyImported, true);
                case 11: return _getFloatBits(meterValues.activeEnergyImported, false);
                case 12: return _getFloatBits(meterValues.activeEnergyExported, true);
                case 13: return _getFloatBits(meterValues.activeEnergyExported, false);
                case 14: return _getFloatBits(meterValues.reactiveEnergyImported, true);
                case 15: return _getFloatBits(meterValues.reactiveEnergyImported, false);
                case 16: return _getFloatBits(meterValues.reactiveEnergyExported, true);
                case 17: return _getFloatBits(meterValues.reactiveEnergyExported, false);
                case 18: return _getFloatBits(meterValues.apparentEnergy, true);
                case 19: return _getFloatBits(meterValues.apparentEnergy, false);

                // Default case to handle unexpected addresses
                default: return 0;
            }
        }

        return 0; // If the address is out of range or invalid
    }

    static bool _isValidRegister(uint32_t address) // Currently unused
    {
        // Define valid ranges
        return (
            (address <= 7) ||  // General registers (64-bit values)
            (address >= 100 && address <= 103) ||  // Voltage and grid frequency
            (address >= 200 && address <= 207) ||  // Aggregated values with channel 0
            (address >= 210 && address <= 217) ||  // Aggregated values without channel 0
            (address >= LOWER_LIMIT_CHANNEL_REGISTERS && address < UPPER_LIMIT_CHANNEL_REGISTERS)  // Channel registers
        );
    }
} // namespace ModbusTcp
