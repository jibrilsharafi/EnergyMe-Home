#include "modbustcp.h"

ModbusTcp* instance = nullptr;

ModbusTcp::ModbusTcp() {
    instance = this;
    for (int i = 0; i < NUM_REGS; i++) {
        holdingRegisters[i] = 0;
    }
}

void ModbusTcp::begin(int port) {
	logger.debug("Initializing Modbus TCP", "modbusTcp::begin");

	int SERVER_ID = 1;
	int MAX_CLIENTS = 1;
	int TIMEOUT = 20000;
	
    _mbServer.registerWorker(SERVER_ID, READ_HOLD_REGISTER, &ModbusTcp::handleReadHoldingRegisters);
    _mbServer.start(port, MAX_CLIENTS, TIMEOUT);  // Port, default server ID, timeout in ms

	logger.debug("Modbus TCP initialized", "modbusTcp::begin");
}

void ModbusTcp::setVoltage(float voltage) {
    holdingRegisters[VOLTAGE_REG] = floatToUint16(voltage);
}

void ModbusTcp::setCurrent(float current) {
    holdingRegisters[CURRENT_REG] = floatToInt16(current);
}

void ModbusTcp::setPower(float power) {
    holdingRegisters[POWER_REG] = floatToInt16(power);
}

void ModbusTcp::setEnergy(float energy) {
    holdingRegisters[ENERGY_REG] = floatToInt16(energy);
}

ModbusMessage ModbusTcp::handleReadHoldingRegisters(ModbusMessage request) {
    if (!instance) {
		logger.info("ModbusTcp instance not initialized yet", "ModbusTcp::handleReadHoldingRegisters");
        return ModbusMessage(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    }

    uint16_t address;
    uint16_t words;
    request.get(2, address);
    request.get(4, words);

    if (address + words > NUM_REGS) {
		logger.warning("Address %d out of range (maximum: %d)", "ModbusTcp::handleReadHoldingRegisters", address, NUM_REGS);
        return ModbusMessage(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    }

    ModbusMessage response;
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(words * 2));

    for (uint16_t i = 0; i < words; i++) {
        response.add(instance->holdingRegisters[address + i]);
    }

    return response;
}

uint16_t ModbusTcp::floatToUint16(float value) {
    return static_cast<uint16_t>(value * 10.0f);
}

int16_t ModbusTcp::floatToInt16(float value) {
	return static_cast<int16_t>(value * 10.0f);
}