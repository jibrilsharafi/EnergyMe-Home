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

    void _bufferMeterData();
    void _uploadBufferedData();
    String _formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp);

    bool _validateJsonConfiguration(JsonDocument &jsonDocument);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    InfluxDbConfiguration &_influxDbConfiguration;
    CustomTime &_customTime;

    struct BufferedPoint {
        MeterValues meterValues;
        int channel;
        unsigned long long timestamp;
        
        BufferedPoint() : channel(0), timestamp(0) {}
        BufferedPoint(const MeterValues &values, int ch, unsigned long long ts) 
            : meterValues(values), channel(ch), timestamp(ts) {}
    };

    CircularBuffer<BufferedPoint, INFLUXDB_BUFFER_SIZE> _bufferMeterValues;

    bool _isSetupDone = false;
    String _baseUrl = "";

    unsigned long _lastMillisInfluxDbLoop = 0;
    unsigned long _lastMillisInfluxDbFailed = 0;
    unsigned long _influxDbConnectionAttempt = 0;
    unsigned long _nextInfluxDbConnectionAttemptMillis = 0;

    unsigned long _lastMillisMeterPublish = 0;
};