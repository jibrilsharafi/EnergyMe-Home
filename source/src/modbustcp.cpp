// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "modbustcp.h"

namespace ModbusTcp // TODO: make the modbus functionality and registers more visible and accessible for the user
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
        LOG_DEBUG("Modbus TCP server stopped");
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
                                return _getFloatBits(roundToDecimals(meterValuesZeroChannel.voltage, VOLTAGE_DECIMALS), true);
                            }
                case 101: {
                                MeterValues meterValuesZeroChannel;
                                if (!Ade7953::getMeterValues(meterValuesZeroChannel, 0)) {
                                    LOG_WARNING("Failed to get meter values for channel 0. Returning default 0");
                                    return 0;
                                }
                                return _getFloatBits(roundToDecimals(meterValuesZeroChannel.voltage, VOLTAGE_DECIMALS), false);
                            }

                // Grid frequency
                case 102: return _getFloatBits(roundToDecimals(Ade7953::getGridFrequency(), GRID_FREQUENCY_DECIMALS), true);
                case 103: return _getFloatBits(roundToDecimals(Ade7953::getGridFrequency(), GRID_FREQUENCY_DECIMALS), false);

                // Role-based aggregated values
                // Grid aggregated (200-217)
                case 200: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), true);
                case 201: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), false);
                case 202: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), true);
                case 203: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), false);
                case 204: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), true);
                case 205: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_GRID), POWER_DECIMALS), false);
                case 206: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_GRID), POWER_FACTOR_DECIMALS), true);
                case 207: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_GRID), POWER_FACTOR_DECIMALS), false);
                case 208: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), true);
                case 209: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), false);
                case 210: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), true);
                case 211: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), false);
                case 212: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), true);
                case 213: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), false);
                case 214: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), true);
                case 215: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), false);
                case 216: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), true);
                case 217: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_GRID), ENERGY_DECIMALS), false);

                // Load aggregated (300-317)
                case 300: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), true);
                case 301: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), false);
                case 302: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), true);
                case 303: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), false);
                case 304: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), true);
                case 305: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_LOAD), POWER_DECIMALS), false);
                case 306: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_LOAD), POWER_FACTOR_DECIMALS), true);
                case 307: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_LOAD), POWER_FACTOR_DECIMALS), false);
                case 308: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), true);
                case 309: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), false);
                case 310: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), true);
                case 311: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), false);
                case 312: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), true);
                case 313: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), false);
                case 314: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), true);
                case 315: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), false);
                case 316: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), true);
                case 317: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_LOAD), ENERGY_DECIMALS), false);

                // PV aggregated (400-417)
                case 400: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), true);
                case 401: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), false);
                case 402: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), true);
                case 403: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), false);
                case 404: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), true);
                case 405: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_PV), POWER_DECIMALS), false);
                case 406: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_PV), POWER_FACTOR_DECIMALS), true);
                case 407: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_PV), POWER_FACTOR_DECIMALS), false);
                case 408: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), true);
                case 409: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), false);
                case 410: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), true);
                case 411: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), false);
                case 412: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), true);
                case 413: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), false);
                case 414: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), true);
                case 415: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), false);
                case 416: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), true);
                case 417: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_PV), ENERGY_DECIMALS), false);

                // Battery aggregated (500-517)
                case 500: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), true);
                case 501: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), false);
                case 502: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), true);
                case 503: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), false);
                case 504: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), true);
                case 505: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_BATTERY), POWER_DECIMALS), false);
                case 506: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_BATTERY), POWER_FACTOR_DECIMALS), true);
                case 507: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_BATTERY), POWER_FACTOR_DECIMALS), false);
                case 508: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), true);
                case 509: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), false);
                case 510: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), true);
                case 511: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), false);
                case 512: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), true);
                case 513: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), false);
                case 514: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), true);
                case 515: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), false);
                case 516: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), true);
                case 517: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_BATTERY), ENERGY_DECIMALS), false);

                // Inverter aggregated (600-617)
                case 600: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), true);
                case 601: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActivePowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), false);
                case 602: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), true);
                case 603: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactivePowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), false);
                case 604: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), true);
                case 605: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentPowerByRole(CHANNEL_ROLE_INVERTER), POWER_DECIMALS), false);
                case 606: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_INVERTER), POWER_FACTOR_DECIMALS), true);
                case 607: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedPowerFactorByRole(CHANNEL_ROLE_INVERTER), POWER_FACTOR_DECIMALS), false);
                case 608: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), true);
                case 609: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyImportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), false);
                case 610: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), true);
                case 611: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedActiveEnergyExportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), false);
                case 612: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), true);
                case 613: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyImportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), false);
                case 614: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), true);
                case 615: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedReactiveEnergyExportedByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), false);
                case 616: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), true);
                case 617: return _getFloatBits(roundToDecimals(Ade7953::getAggregatedApparentEnergyByRole(CHANNEL_ROLE_INVERTER), ENERGY_DECIMALS), false);

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
                case 0: return _getFloatBits(roundToDecimals(meterValues.current, CURRENT_DECIMALS), true);
                case 1: return _getFloatBits(roundToDecimals(meterValues.current, CURRENT_DECIMALS), false);
                case 2: return _getFloatBits(roundToDecimals(meterValues.activePower, POWER_DECIMALS), true);
                case 3: return _getFloatBits(roundToDecimals(meterValues.activePower, POWER_DECIMALS), false);
                case 4: return _getFloatBits(roundToDecimals(meterValues.reactivePower, POWER_DECIMALS), true);
                case 5: return _getFloatBits(roundToDecimals(meterValues.reactivePower, POWER_DECIMALS), false);
                case 6: return _getFloatBits(roundToDecimals(meterValues.apparentPower, POWER_DECIMALS), true);
                case 7: return _getFloatBits(roundToDecimals(meterValues.apparentPower, POWER_DECIMALS), false);
                case 8: return _getFloatBits(roundToDecimals(meterValues.powerFactor, POWER_FACTOR_DECIMALS), true);
                case 9: return _getFloatBits(roundToDecimals(meterValues.powerFactor, POWER_FACTOR_DECIMALS), false);
                // NOTE: We cannot send directly double values over Modbus (not standard), so we cast them to float after rounding, accepting to lose some precision
                case 10: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.activeEnergyImported, ENERGY_DECIMALS)), true);
                case 11: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.activeEnergyImported, ENERGY_DECIMALS)), false);
                case 12: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.activeEnergyExported, ENERGY_DECIMALS)), true);
                case 13: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.activeEnergyExported, ENERGY_DECIMALS)), false);
                case 14: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.reactiveEnergyImported, ENERGY_DECIMALS)), true);
                case 15: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.reactiveEnergyImported, ENERGY_DECIMALS)), false);
                case 16: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.reactiveEnergyExported, ENERGY_DECIMALS)), true);
                case 17: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.reactiveEnergyExported, ENERGY_DECIMALS)), false);
                case 18: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.apparentEnergy, ENERGY_DECIMALS)), true);
                case 19: return _getFloatBits(static_cast<float>(roundToDecimals(meterValues.apparentEnergy, ENERGY_DECIMALS)), false);

                // Default case to handle unexpected addresses
                default: return 0;
            }
        }

        return 0; // If the address is out of range or invalid
    }

    static bool _isValidRegister(uint32_t address) // Currently unused
    {
        // General registers: 0-7
        if (address >= 0 && address <= 7) return true;

        // Meter values: 100-103
        if (address >= 100 && address <= 103) return true;

        // Role-based aggregated values: 200-217, 300-317, 400-417, 500-517, 600-617
        if ((address >= 200 && address <= 217) ||  // Grid
            (address >= 300 && address <= 317) ||  // Load
            (address >= 400 && address <= 417) ||  // PV
            (address >= 500 && address <= 517) ||  // Battery
            (address >= 600 && address <= 617)) {  // Inverter
            return true;
        }

        // Per-channel values: 1000 + (channel * 100) + offset
        // 17 channels (0-16), each with 20 registers (0-19)
        if (address >= LOWER_LIMIT_CHANNEL_REGISTERS && address < UPPER_LIMIT_CHANNEL_REGISTERS) {
            uint16_t channelOffset = address - LOWER_LIMIT_CHANNEL_REGISTERS;
            uint8_t channel = channelOffset / STEP_CHANNEL_REGISTERS;
            uint8_t registerOffset = channelOffset % STEP_CHANNEL_REGISTERS;
            return channel < CHANNEL_COUNT && registerOffset < 20;
        }

        return false;
    }
} // namespace ModbusTcp
