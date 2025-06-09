#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <CircularBuffer.hpp>
#include <HTTPClient.h>
#include <WiFi.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "structs.h"
#include "utils.h"

class InfluxDbClient
{
public:
    InfluxDbClient(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        InfluxDbConfiguration &influxDbConfiguration,
        CustomTime &customTime);

    void begin();
    void loop();

    bool setConfiguration(JsonDocument &jsonDocument);

private:
    void _setDefaultConfiguration();
    void _setConfigurationFromSpiffs();
    void _saveConfigurationToSpiffs();

    void _disable();

    bool _testConnection();
    bool _testCredentials();
    bool _connectInfluxDb();

    void _bufferMeterData();
    void _uploadBufferedData();
    String _formatRealtimeLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp);
    String _formatEnergyLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp);

    bool _validateJsonConfiguration(JsonDocument &jsonDocument);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    InfluxDbConfiguration &_influxDbConfiguration;
    CustomTime &_customTime;

    struct BufferedPoint {
        MeterValues meterValues;
        int channel;
        unsigned long long unixTimeMilliseconds;
        
        BufferedPoint() : channel(0), unixTimeMilliseconds(0) {}
        BufferedPoint(const MeterValues &values, int ch, unsigned long long ts) 
            : meterValues(values), channel(ch), unixTimeMilliseconds(ts) {}
    };

    CircularBuffer<BufferedPoint, INFLUXDB_BUFFER_SIZE> _bufferMeterValues;

    bool _isSetupDone = false;
    bool _isConnected = false;
    String _baseUrl;

    unsigned long _lastMillisInfluxDbLoop = 0;
    unsigned long _lastMillisInfluxDbFailed = 0;
    unsigned long _influxDbConnectionAttempt = 0;
    unsigned long _nextInfluxDbConnectionAttemptMillis = 0;

    unsigned long _lastMillisMeterPublish = MINIMUM_TIME_BEFORE_VALID_METER; // Do not publish immediately after setup
    unsigned long _lastMillisMeterBuffer = MINIMUM_TIME_BEFORE_VALID_METER; // Do not buffer immediately after setup

    String _deviceId;
};