#include "influxdbclient.h"

static const char *TAG = "influxdbclient";

namespace InfluxDbClient
{
    // Local configuration (was global variable)
    static InfluxDbConfiguration influxDbConfiguration;

    // Task handle
    static TaskHandle_t _taskHandle = NULL;

    // Private function declarations
    static void _setDefaultConfiguration();
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    static void _influxDbTask(void *parameter);
    static void _sendData();
    static void _formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char *buffer, size_t bufferSize, bool isEnergyData);
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument);

    void begin()
    {
        logger.debug("Setting up InfluxDB client...", TAG);

        _setConfigurationFromPreferences();

        if (influxDbConfiguration.enabled)
        {
            // Start the FreeRTOS task
            if (_taskHandle == NULL)
            {
                xTaskCreate(_influxDbTask, INFLUXDB_TASK_NAME, INFLUXDB_TASK_STACK_SIZE, NULL, INFLUXDB_TASK_PRIORITY, &_taskHandle);
                if (_taskHandle != NULL)
                {
                    logger.debug("InfluxDB task started", TAG);
                }
                else
                {
                    logger.error("Failed to create InfluxDB task", TAG);
                }
            }
        }
        else
        {
            logger.debug("InfluxDB is disabled", TAG);
        }
    }

    void stop()
    {
        logger.debug("Stopping InfluxDB client...", TAG);
        
        if (_taskHandle != NULL)
        {
            vTaskDelete(_taskHandle);
            _taskHandle = NULL;
            logger.debug("InfluxDB task stopped", TAG);
        }
    }

    static void _influxDbTask(void *parameter)
    {
        logger.debug("InfluxDB task started", TAG);

        while (true)
        {
            // Wait for the configured frequency
            delay(influxDbConfiguration.frequencySeconds * 1000);

            // Check if WiFi is connected and time is synced
            if (!CustomWifi::isFullyConnected() || !CustomTime::isTimeSynched())
            {
                logger.verbose("WiFi not connected or time not synced, skipping InfluxDB send", TAG);
                continue;
            }

            // Send data
            _sendData();
        }

        logger.debug("InfluxDB task ending", TAG);
        vTaskDelete(NULL);
    }

    static void _sendData()
    {
        HTTPClient http;
        char url[SERVER_NAME_BUFFER_SIZE];
        char baseUrl[SERVER_NAME_BUFFER_SIZE];

        // Build base URL
        snprintf(baseUrl, sizeof(baseUrl), "http%s://%s:%d", 
                influxDbConfiguration.useSSL ? "s" : "", 
                influxDbConfiguration.server, 
                influxDbConfiguration.port);

        // Build endpoint URL based on version
        if (influxDbConfiguration.version == 2)
        {
            snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s",
                     baseUrl,
                     influxDbConfiguration.organization,
                     influxDbConfiguration.bucket);
        }
        else
        {
            snprintf(url, sizeof(url), "%s/write?db=%s",
                     baseUrl,
                     influxDbConfiguration.database);
        }

        http.begin(url);
        http.addHeader("Content-Type", "text/plain");

        // Set authorization header based on InfluxDB version
        if (influxDbConfiguration.version == 2)
        {
            char authHeader[AUTH_HEADER_BUFFER_SIZE];
            snprintf(authHeader, sizeof(authHeader), "Token %s", influxDbConfiguration.token);
            http.addHeader("Authorization", authHeader);
        }
        else if (influxDbConfiguration.version == 1)
        {
            char credentials[sizeof(influxDbConfiguration.username) + sizeof(influxDbConfiguration.password) + 2]; // +2 for ':' and null terminator
            snprintf(credentials, sizeof(credentials), "%s:%s", influxDbConfiguration.username, influxDbConfiguration.password);

            String encodedCredentials = base64::encode((const unsigned char*)credentials, strlen(credentials));

            char authHeader[AUTH_HEADER_BUFFER_SIZE];
            snprintf(authHeader, sizeof(authHeader), "Basic %s", encodedCredentials.c_str());
            http.addHeader("Authorization", authHeader);
        }

        // Build line protocol payload
        char payload[PAYLOAD_BUFFER_SIZE] = "";
        size_t payloadLength = 0;
        unsigned long long currentTimestamp = CustomTime::getUnixTimeMilliseconds();

        // Add data for all active channels
        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            if (ade7953.channelData[i].active)
            {
                if (ade7953.meterValues[i].lastUnixTimeMilliseconds == 0)
                {
                    logger.debug("Channel %d does not have real measurements yet, skipping", TAG, i);
                    continue;
                }

                if (!validateUnixTime(ade7953.meterValues[i].lastUnixTimeMilliseconds))
                {
                    logger.warning("Invalid unixTime for channel %d: %llu", TAG, i, ade7953.meterValues[i].lastUnixTimeMilliseconds);
                    continue;
                }

                // Add real-time data (power, voltage, current)
                char realtimeLineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
                _formatLineProtocol(ade7953.meterValues[i], i, ade7953.meterValues[i].lastUnixTimeMilliseconds, realtimeLineProtocol, sizeof(realtimeLineProtocol), false);

                if (payloadLength > 0 && payloadLength + 1 < PAYLOAD_BUFFER_SIZE)
                {
                    payload[payloadLength] = '\n';
                    payloadLength++;
                    payload[payloadLength] = '\0';
                }

                if (payloadLength + strlen(realtimeLineProtocol) < PAYLOAD_BUFFER_SIZE)
                {
                    snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", realtimeLineProtocol);
                    payloadLength += strlen(realtimeLineProtocol);
                }

                // Add energy data
                char energyLineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
                _formatLineProtocol(ade7953.meterValues[i], i, currentTimestamp, energyLineProtocol, sizeof(energyLineProtocol), true);

                if (payloadLength + 1 < PAYLOAD_BUFFER_SIZE)
                {
                    payload[payloadLength] = '\n';
                    payloadLength++;
                    payload[payloadLength] = '\0';
                }

                if (payloadLength + strlen(energyLineProtocol) < PAYLOAD_BUFFER_SIZE)
                {
                    snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", energyLineProtocol);
                    payloadLength += strlen(energyLineProtocol);
                }
            }
        }

        if (payloadLength == 0)
        {
            logger.debug("No data to send to InfluxDB", TAG);
            http.end();
            return;
        }

        // Send the data
        int httpCode = http.POST(payload);

        if (httpCode >= 200 && httpCode < 300)
        {
            logger.debug("Successfully sent data to InfluxDB (HTTP %d)", TAG, httpCode);
            statistics.influxdbUploadCount++;
        }
        else
        {
            logger.warning("Failed to send data to InfluxDB (HTTP %d)", TAG, httpCode);
            statistics.influxdbUploadCountError++;
        }

        http.end();
    }

    static void _formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char *buffer, size_t bufferSize, bool isEnergyData)
    {
        // Create sanitized label directly in a char array
        char sanitizedLabel[sizeof(ade7953.channelData[channel].label) + 20]; // Enlarge to accommodate potential replacements
        const char *originalLabel = ade7953.channelData[channel].label;

        // Sanitize the label in-place without heap allocation
        size_t labelLen = strlen(originalLabel);
        size_t writePos = 0;
        for (size_t i = 0; i < labelLen && writePos < sizeof(sanitizedLabel) - 1; i++)
        {
            char c = originalLabel[i];
            if (c == ' ' || c == ',' || c == '=' || c == ':')
            {
                sanitizedLabel[writePos++] = '_';
            }
            else
            {
                sanitizedLabel[writePos++] = c;
            }
        }
        sanitizedLabel[writePos] = '\0';

        if (isEnergyData)
        {
            // Format energy data
            snprintf(buffer, bufferSize,
                     "%s,channel=%d,label=%s,device_id=%s active_energy_imported=%.3f,active_energy_exported=%.3f,reactive_energy_imported=%.3f,reactive_energy_exported=%.3f,apparent_energy=%.3f %llu000000",
                     influxDbConfiguration.measurement,
                     channel,
                     sanitizedLabel,
                     DEVICE_ID,
                     meterValues.activeEnergyImported,
                     meterValues.activeEnergyExported,
                     meterValues.reactiveEnergyImported,
                     meterValues.reactiveEnergyExported,
                     meterValues.apparentEnergy,
                     timestamp);
        }
        else
        {
            // Format real-time data
            snprintf(buffer, bufferSize,
                     "%s,channel=%d,label=%s,device_id=%s voltage=%.2f,current=%.3f,active_power=%.2f,reactive_power=%.2f,apparent_power=%.2f,power_factor=%.3f %llu000000",
                     influxDbConfiguration.measurement,
                     channel,
                     sanitizedLabel,
                     DEVICE_ID,
                     meterValues.voltage,
                     meterValues.current,
                     meterValues.activePower,
                     meterValues.reactivePower,
                     meterValues.apparentPower,
                     meterValues.powerFactor,
                     timestamp);
        }
    }

    static void _setDefaultConfiguration()
    {
        logger.debug("Setting default InfluxDB configuration...", TAG);

        // Set defaults directly to the configuration struct
        influxDbConfiguration.enabled = INFLUXDB_ENABLED_DEFAULT;
        snprintf(influxDbConfiguration.server, sizeof(influxDbConfiguration.server), "%s", INFLUXDB_SERVER_DEFAULT);
        influxDbConfiguration.port = INFLUXDB_PORT_DEFAULT;
        influxDbConfiguration.version = INFLUXDB_VERSION_DEFAULT;
        snprintf(influxDbConfiguration.database, sizeof(influxDbConfiguration.database), "%s", INFLUXDB_DATABASE_DEFAULT);
        snprintf(influxDbConfiguration.username, sizeof(influxDbConfiguration.username), "%s", INFLUXDB_USERNAME_DEFAULT);
        snprintf(influxDbConfiguration.password, sizeof(influxDbConfiguration.password), "%s", INFLUXDB_PASSWORD_DEFAULT);
        snprintf(influxDbConfiguration.organization, sizeof(influxDbConfiguration.organization), "%s", INFLUXDB_ORGANIZATION_DEFAULT);
        snprintf(influxDbConfiguration.bucket, sizeof(influxDbConfiguration.bucket), "%s", INFLUXDB_BUCKET_DEFAULT);
        snprintf(influxDbConfiguration.token, sizeof(influxDbConfiguration.token), "%s", INFLUXDB_TOKEN_DEFAULT);
        snprintf(influxDbConfiguration.measurement, sizeof(influxDbConfiguration.measurement), "%s", INFLUXDB_MEASUREMENT_DEFAULT);
        influxDbConfiguration.frequencySeconds = INFLUXDB_FREQUENCY_DEFAULT;
        influxDbConfiguration.useSSL = INFLUXDB_USE_SSL_DEFAULT;

        // Session-specific data initialized to defaults
        snprintf(influxDbConfiguration.lastConnectionStatus, sizeof(influxDbConfiguration.lastConnectionStatus), "Not connected");
        influxDbConfiguration.lastConnectionAttemptTimestamp[0] = '\0';

        // Save the default configuration to Preferences
        _saveConfigurationToPreferences();

        logger.debug("Default InfluxDB configuration set and saved to Preferences", TAG);
    }

    bool setConfiguration(JsonDocument &jsonDocument)
    {
        logger.debug("Setting InfluxDB configuration...", TAG);

        if (!_validateJsonConfiguration(jsonDocument))
        {
            logger.error("Invalid InfluxDB configuration", TAG);
            return false;
        }

        bool wasEnabled = influxDbConfiguration.enabled;
        
        influxDbConfiguration.enabled = jsonDocument["enabled"].as<bool>();
        snprintf(influxDbConfiguration.server, sizeof(influxDbConfiguration.server), "%s", jsonDocument["server"].as<const char *>());
        influxDbConfiguration.port = jsonDocument["port"].as<int>();
        influxDbConfiguration.version = jsonDocument["version"].as<int>();
        snprintf(influxDbConfiguration.database, sizeof(influxDbConfiguration.database), "%s", jsonDocument["database"].as<const char *>());
        snprintf(influxDbConfiguration.username, sizeof(influxDbConfiguration.username), "%s", jsonDocument["username"].as<const char *>());
        snprintf(influxDbConfiguration.password, sizeof(influxDbConfiguration.password), "%s", jsonDocument["password"].as<const char *>());
        snprintf(influxDbConfiguration.organization, sizeof(influxDbConfiguration.organization), "%s", jsonDocument["organization"].as<const char *>());
        snprintf(influxDbConfiguration.bucket, sizeof(influxDbConfiguration.bucket), "%s", jsonDocument["bucket"].as<const char *>());
        snprintf(influxDbConfiguration.token, sizeof(influxDbConfiguration.token), "%s", jsonDocument["token"].as<const char *>());
        snprintf(influxDbConfiguration.measurement, sizeof(influxDbConfiguration.measurement), "%s", jsonDocument["measurement"].as<const char *>());
        influxDbConfiguration.frequencySeconds = jsonDocument["frequency"].as<int>();
        influxDbConfiguration.useSSL = jsonDocument["useSSL"].as<bool>();

        _saveConfigurationToPreferences();

        // Session-specific data
        snprintf(influxDbConfiguration.lastConnectionStatus, sizeof(influxDbConfiguration.lastConnectionStatus), "Configuration updated");
        char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
        snprintf(influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

        logger.debug("InfluxDB configuration set", TAG);

        stop(); // Stop existing task if running
        if (influxDbConfiguration.enabled)
        {
            begin(); // Start new task if enabled
        }

        return true;
    }

    static void _setConfigurationFromPreferences()
    {
        logger.debug("Setting InfluxDB configuration from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, true)) { // true = read-only
            logger.error("Failed to initialize Preferences for InfluxDB", TAG);
            _setDefaultConfiguration();
            return;
        }

        // Load configuration from Preferences (exclude session-specific data)
        influxDbConfiguration.enabled = preferences.getBool("enabled", false);
        preferences.getString("server", influxDbConfiguration.server, sizeof(influxDbConfiguration.server));
        influxDbConfiguration.port = preferences.getInt("port", 8086);
        influxDbConfiguration.version = preferences.getInt("version", 1);
        preferences.getString("database", influxDbConfiguration.database, sizeof(influxDbConfiguration.database));
        preferences.getString("username", influxDbConfiguration.username, sizeof(influxDbConfiguration.username));
        preferences.getString("password", influxDbConfiguration.password, sizeof(influxDbConfiguration.password));
        preferences.getString("organization", influxDbConfiguration.organization, sizeof(influxDbConfiguration.organization));
        preferences.getString("bucket", influxDbConfiguration.bucket, sizeof(influxDbConfiguration.bucket));
        preferences.getString("token", influxDbConfiguration.token, sizeof(influxDbConfiguration.token));
        preferences.getString("measurement", influxDbConfiguration.measurement, sizeof(influxDbConfiguration.measurement));
        influxDbConfiguration.frequencySeconds = preferences.getInt("frequency", 30);
        influxDbConfiguration.useSSL = preferences.getBool("useSSL", false);

        preferences.end();

        // Session-specific data is initialized to defaults (not loaded from Preferences)
        snprintf(influxDbConfiguration.lastConnectionStatus, sizeof(influxDbConfiguration.lastConnectionStatus), "Not connected");
        influxDbConfiguration.lastConnectionAttemptTimestamp[0] = '\0';

        logger.debug("Successfully set InfluxDB configuration from Preferences", TAG);
    }

    static void _saveConfigurationToPreferences()
    {
        logger.debug("Saving InfluxDB configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false)) { // false = read-write
            logger.error("Failed to initialize Preferences for InfluxDB", TAG);
            return;
        }

        // Save configuration to Preferences (exclude session-specific data)
        preferences.putBool("enabled", influxDbConfiguration.enabled);
        preferences.putString("server", influxDbConfiguration.server);
        preferences.putInt("port", influxDbConfiguration.port);
        preferences.putInt("version", influxDbConfiguration.version);
        preferences.putString("database", influxDbConfiguration.database);
        preferences.putString("username", influxDbConfiguration.username);
        preferences.putString("password", influxDbConfiguration.password);
        preferences.putString("organization", influxDbConfiguration.organization);
        preferences.putString("bucket", influxDbConfiguration.bucket);
        preferences.putString("token", influxDbConfiguration.token);
        preferences.putString("measurement", influxDbConfiguration.measurement);
        preferences.putInt("frequency", influxDbConfiguration.frequencySeconds);
        preferences.putBool("useSSL", influxDbConfiguration.useSSL);

        preferences.end();

        logger.debug("Successfully saved InfluxDB configuration to Preferences", TAG);
    }

    static bool _validateJsonConfiguration(JsonDocument &jsonDocument)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            logger.warning("Invalid JSON document", TAG);
            return false;
        }

        if (!jsonDocument["enabled"].is<bool>())
        {
            logger.warning("enabled field is not a boolean", TAG);
            return false;
        }
        if (!jsonDocument["server"].is<const char *>())
        {
            logger.warning("server field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["port"].is<int>())
        {
            logger.warning("port field is not an integer", TAG);
            return false;
        }
        if (!jsonDocument["version"].is<int>())
        {
            logger.warning("version field is not an integer", TAG);
            return false;
        }
        if (!jsonDocument["database"].is<const char *>())
        {
            logger.warning("database field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["username"].is<const char *>())
        {
            logger.warning("username field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["password"].is<const char *>())
        {
            logger.warning("password field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["organization"].is<const char *>())
        {
            logger.warning("organization field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["bucket"].is<const char *>())
        {
            logger.warning("bucket field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["token"].is<const char *>())
        {
            logger.warning("token field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["measurement"].is<const char *>())
        {
            logger.warning("measurement field is not a string", TAG);
            return false;
        }
        if (!jsonDocument["frequency"].is<int>())
        {
            logger.warning("frequency field is not an integer", TAG);
            return false;
        }
        // Also ensure frequency is between 1 and 3600
        if (jsonDocument["frequency"].as<int>() < 1 || jsonDocument["frequency"].as<int>() > 3600)
        {
            logger.warning("frequency field must be between 1 and 3600 seconds", TAG);
            return false;
        }
        if (!jsonDocument["useSSL"].is<bool>())
        {
            logger.warning("useSSL field is not a boolean", TAG);
            return false;
        }

        return true;
    }

} // namespace InfluxDbClient