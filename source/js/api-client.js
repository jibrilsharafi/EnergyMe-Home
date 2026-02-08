/* SPDX-License-Identifier: GPL-3.0-or-later
    Copyright (C) 2025 Jibril Sharafi */

/**
 * EnergyMe API Client
 * Unified client for all API calls with consistent authentication and error handling
 */
class EnergyMeAPI {
    constructor() {
        this.baseUrl = '';  // Same origin
        this.defaultHeaders = {
            'Content-Type': 'application/json',
            'Connection': 'close'  // Prevent keep-alive to reduce ESP32 load
        };
        this.getTimeoutMs = 3000;
        this.otherTimeoutMs = 5000;
    }

    /**
     * Internal fetch with timeout
     */
    async _fetchWithTimeout(url, config, timeoutMs) {
        const controller = new AbortController();
        const id = setTimeout(() => controller.abort(), timeoutMs);
        config.signal = controller.signal;
        try {
            const response = await fetch(url, config);
            clearTimeout(id);
            return response;
        } catch (error) {
            clearTimeout(id);
            throw error;
        }
    }

    /**
     * Make an API call (digest auth handled automatically by browser)
     * @param {string} endpoint - API endpoint (with or without /api/v1/ prefix)
     * @param {Object} options - Fetch options
     * @param {number} timeoutMs - Timeout in milliseconds
     * @returns {Promise<Response>} - Fetch response
     */
    async apiCall(endpoint, options = {}, timeoutMs = this.otherTimeoutMs) {
        // Normalize endpoint to use /api/v1/ prefix if not already present
        let url = endpoint;
        if (!endpoint.startsWith('/api/v1/')) {
            url = `/api/v1/${endpoint.replace(/^\//, '')}`;
        }

        const config = {
            ...options,
            headers: {
                ...this.defaultHeaders,
                ...options.headers
            }
        };

        try {
            const response = await this._fetchWithTimeout(url, config, timeoutMs);
            
            // For digest auth, 401 means credentials are required (browser will prompt)
            if (response.status === 401) {
                throw new Error('Authentication required - please refresh the page');
            }
            
            return response;
        } catch (error) {
            console.error(`API call failed for ${url}:`, error);
            throw error;
        }
    }

    /**
     * GET request helper
     * @param {string} endpoint - API endpoint
     * @param {Object} options - Additional options
     * @param {string} options.responseType - Response type: 'json' (default), 'text', 'blob'
     * @returns {Promise<any>} - Parsed response
     */
    async get(endpoint, options = {}) {
        const { responseType = 'json' } = options;
        const response = await this.apiCall(endpoint, { method: 'GET' }, this.getTimeoutMs);
        
        if (!response.ok) {
            throw new Error(`GET ${endpoint} failed: ${response.status}`);
        }
        
        switch (responseType) {
            case 'text':
                return response.text();
            case 'blob':
                return response.blob();
            case 'json':
            default:
                return response.json();
        }
    }

    /**
     * POST request helper
     * @param {string} endpoint - API endpoint
     * @param {any} data - Request body data
     * @returns {Promise<any>} - Parsed JSON response
     */
    async post(endpoint, data = null) {
        const options = {
            method: 'POST',
            body: data ? JSON.stringify(data) : null
        };

        const response = await this.apiCall(endpoint, options, this.otherTimeoutMs);
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`POST ${endpoint} failed: ${response.status} - ${errorText}`);
        }
        
        return response.json().catch(() => ({})); // Some endpoints return empty response
    }

    /**
     * PUT request helper
     * @param {string} endpoint - API endpoint
     * @param {any} data - Request body data
     * @returns {Promise<any>} - Parsed JSON response
     */
    async put(endpoint, data) {
        const response = await this.apiCall(endpoint, {
            method: 'PUT',
            body: JSON.stringify(data)
        }, this.otherTimeoutMs);
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`PUT ${endpoint} failed: ${response.status} - ${errorText}`);
        }
        
        return response.json().catch(() => ({}));
    }

    /**
     * PATCH request helper
     * @param {string} endpoint - API endpoint
     * @param {any} data - Request body data
     * @returns {Promise<any>} - Parsed JSON response
     */
    async patch(endpoint, data) {
        const response = await this.apiCall(endpoint, {
            method: 'PATCH',
            body: JSON.stringify(data)
        }, this.otherTimeoutMs);
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`PATCH ${endpoint} failed: ${response.status} - ${errorText}`);
        }
        
        return response.json().catch(() => ({}));
    }

    // API helper methods for common operations
    
    /**
     * Get meter values
     */
    async getMeterValues() {
        return this.get('ade7953/meter-values');
    }

    /**
     * Get channel configuration
     */
    async getChannelConfig(index = null) {
        const endpoint = index !== null ? `ade7953/channel?index=${index}` : 'ade7953/channel';
        return this.get(endpoint);
    }

    /**
     * Set channel configuration
     */
    async setChannelConfig(channelData) {
        return this.put('ade7953/channel', channelData);
    }

    /**
     * Patch channel configuration (partial update)
     * Note: channelData must include 'index' field
     */
    async patchChannelConfig(channelData) {
        return this.patch('ade7953/channel', channelData);
    }

    /**
     * Get log levels
     */
    async getLogLevels() {
        return this.get('logs/level');
    }

    /**
     * Set log levels
     */
    async setLogLevels(data) {
        return this.put('logs/level', data);
    }

    /**
     * Get system info
     */
    async getSystemInfo() {
        return this.get('system/info');
    }

    /**
     * Restart system
     */
    async restartSystem() {
        return this.post('system/restart');
    }

    /**
     * Factory reset
     */
    async factoryReset() {
        return this.post('system/factory-reset');
    }

    /**
     * Get system time
     */
    async getSystemTime() {
        return this.get('system/time');
    }

    /**
     * Set system time (for devices without internet connectivity)
     * @param {number} unixSeconds - Unix timestamp in seconds
     */
    async setSystemTime(unixSeconds) {
        return this.post('system/time', { unixTime: unixSeconds });
    }

    /**
     * Get custom MQTT config
     */
    async getCustomMqttConfig() {
        return this.get('custom-mqtt/config');
    }

    /**
     * Set custom MQTT config
     */
    async setCustomMqttConfig(config) {
        return this.put('custom-mqtt/config', config);
    }

    /**
     * Get InfluxDB config
     */
    async getInfluxDbConfig() {
        return this.get('influxdb/config');
    }

    /**
     * Set InfluxDB config
     */
    async setInfluxDbConfig(config) {
        return this.put('influxdb/config', config);
    }

    /**
     * Get LED brightness
     */
    async getLedBrightness() {
        return this.get('led/brightness');
    }

    /**
     * Set LED brightness
     */
    async setLedBrightness(brightness) {
        return this.put('led/brightness', { brightness });
    }

    /**
     * Get ADE7953 configuration
     */
    async getAde7953Config() {
        return this.get('ade7953/config');
    }

    /**
     * Set ADE7953 configuration
     */
    async setAde7953Config(config) {
        return this.put('ade7953/config', config);
    }

    /**
     * Get grid frequency
     */
    async getGridFrequency() {
        return this.get('ade7953/grid-frequency');
    }

    /**
     * Reset energy values
     */
    async resetEnergyValues() {
        return this.post('ade7953/energy/reset');
    }

    /**
     * Clear historical data for a specific channel
     * @param {number} channelIndex - The channel index to clear history for
     */
    async clearChannelHistory(channelIndex) {
        return this.post(`ade7953/energy/clear-history?index=${channelIndex}`);
    }

    /**
     * Get firmware update info
     */
    async getFirmwareUpdateInfo() {
        return this.get('firmware/update-info');
    }

    /**
     * Get crash info
     */
    async getCrashInfo() {
        return this.get('crash/info');
    }

    /**
     * Clear crash dump
     */
    async clearCrashDump() {
        return this.post('crash/clear');
    }

    /**
     * Get list of files
     * @param {string} folder - Optional folder path to filter files
     */
    async getFileList(folder = null) {
        const endpoint = folder ? `list-files?folder=${encodeURIComponent(folder)}` : 'list-files';
        return this.get(endpoint);
    }

    /**
     * Reset WiFi configuration
     */
    async resetWifi() {
        return this.post('network/wifi/reset');
    }

    /**
     * Set new WiFi credentials
     * @param {string} ssid - The WiFi network name
     * @param {string} password - The WiFi password
     */
    async setWifiCredentials(ssid, password) {
        return this.post('network/wifi/credentials', { ssid, password });
    }

    /**
     * Get health status
     */
    async getHealth() {
        return this.get('health');
    }

    /**
     * Get authentication status
     */
    async getAuthStatus() {
        return this.get('auth/status');
    }

    /**
     * Change password
     */
    async changePassword(currentPassword, newPassword) {
        return this.post('auth/change-password', {
            currentPassword,
            newPassword
        });
    }

    /**
     * Reset password to default
     */
    async resetPassword() {
        return this.post('auth/reset-password');
    }

    /**
     * Get OTA status
     */
    async getOtaStatus() {
        return this.get('ota/status');
    }

    /**
     * Rollback firmware
     */
    async rollbackFirmware() {
        return this.post('ota/rollback');
    }

    /**
     * Get system statistics
     */
    async getSystemStatistics() {
        return this.get('system/statistics');
    }

    /**
     * Get system secrets status
     */
    async getSystemSecrets() {
        return this.get('system/secrets');
    }

    /**
     * Get logs
     */
    async getLogs() {
        return this.get('logs', { responseType: 'text' });
    }

    /**
     * Clear logs
     */
    async clearLogs() {
        return this.post('logs/clear');
    }

    /**
     * Get custom MQTT status
     */
    async getCustomMqttStatus() {
        return this.get('custom-mqtt/status');
    }

    /**
     * Reset custom MQTT config
     */
    async resetCustomMqttConfig() {
        return this.post('custom-mqtt/config/reset');
    }

    /**
     * Get MQTT cloud services status
     */
    async getMqttCloudServices() {
        return this.get('mqtt/cloud-services');
    }

    /**
     * Set MQTT cloud services status
     */
    async setMqttCloudServices(enabled) {
        return this.put('mqtt/cloud-services', { enabled });
    }

    /**
     * Get InfluxDB status
     */
    async getInfluxDbStatus() {
        return this.get('influxdb/status');
    }

    /**
     * Reset InfluxDB config
     */
    async resetInfluxDbConfig() {
        return this.post('influxdb/config/reset');
    }

    /**
     * Reset ADE7953 configuration
     */
    async resetAde7953Config() {
        return this.post('ade7953/config/reset');
    }

    /**
     * Get ADE7953 sample time
     */
    async getAde7953SampleTime() {
        return this.get('ade7953/sample-time');
    }

    /**
     * Set ADE7953 sample time
     */
    async setAde7953SampleTime(sampleTime) {
        return this.put('ade7953/sample-time', { sampleTime });
    }

    /**
     * Reset channel configuration
     */
    async resetChannelConfig(index) {
        return this.post(`ade7953/channel/reset?index=${index}`);
    }

    /**
     * Read ADE7953 register
     */
    async readAde7953Register(address, bits = 32, signed = false) {
        return this.get(`ade7953/register?address=${address}&bits=${bits}&signed=${signed}`);
    }

    /**
     * Write ADE7953 register
     */
    async writeAde7953Register(address, value, bits = 32, signed = false) {
        return this.put('ade7953/register', { address, value, bits, signed });
    }

    /**
     * Set energy values
     */
    async setEnergyValues(
        index, 
        activeEnergyImported, 
        activeEnergyExported,
        reactiveEnergyImported,
        reactiveEnergyExported,
        apparentEnergy
    ) {
        return this.put('ade7953/energy', {
            index,
            activeEnergyImported,
            activeEnergyExported,
            reactiveEnergyImported,
            reactiveEnergyExported,
            apparentEnergy
        });
    }

    /**
     * Get crash dump
     */
    async getCrashDump(offset = 0, size = 1024) {
        return this.get(`crash/dump?offset=${offset}&size=${size}`);
    }

    /**
     * Get specific file content
     */
    async getFile(filepath) {
        return this.get(`files/${encodeURIComponent(filepath)}`);
    }

    /**
     * Get specific file content as text
     */
    async getFileAsText(filepath) {
        return this.get(`files/${encodeURIComponent(filepath)}`, { responseType: 'text' });
    }

    /**
     * Upload a file to LittleFS
     * @param {string} filepath - Target file path on device
     * @param {File|Blob} file - File to upload
     */
    async uploadFile(filepath, file) {
        const formData = new FormData();
        formData.append('file', file, filepath);
        
        const response = await this.apiCall(`files/${encodeURIComponent(filepath)}`, {
            method: 'POST',
            body: formData,
            headers: {} // Let browser set Content-Type with boundary
        }, this.otherTimeoutMs);
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`File upload failed: ${response.status} - ${errorText}`);
        }
        
        return response.json().catch(() => ({}));
    }

    /**
     * Delete a file from LittleFS
     * @param {string} filepath - File path to delete
     */
    async deleteFile(filepath) {
        return this.post('delete-file', { filepath });
    }

    /**
     * Get safe mode status
     */
    async getSafeModeStatus() {
        return this.get('system/safe-mode');
    }

    /**
     * Clear safe mode
     */
    async clearSafeMode() {
        return this.post('system/safe-mode/clear');
    }

    /**
     * Get UDP log destination
     */
    async getUdpLogDestination() {
        return this.get('logs-udp-destination');
    }

    /**
     * Set UDP log destination
     * @param {string} host - UDP destination host
     * @param {number} port - UDP destination port
     */
    async setUdpLogDestination(host, port) {
        return this.put('logs-udp-destination', { host, port });
    }

    /**
     * Update all channels configuration (bulk update)
     * @param {Array} channels - Array of channel configurations
     */
    async updateAllChannels(channels) {
        return this.put('ade7953/channels', { channels });
    }

    /**
     * Arm waveform capture
     * @param {number} channelIndex - Channel index for capture
     * @param {string} signal - Signal type to capture
     */
    async armWaveformCapture(channelIndex, signal) {
        return this.post('ade7953/waveform/arm', { index: channelIndex, signal });
    }

    /**
     * Get waveform capture status
     */
    async getWaveformStatus() {
        return this.get('ade7953/waveform/status');
    }

    /**
     * Get captured waveform data
     */
    async getWaveformData() {
        return this.get('ade7953/waveform/data');
    }
}

// Create global instance
window.energyApi = new EnergyMeAPI();

// Backward compatibility aliases
window.authAPI = window.energyApi;  // For existing code using authAPI
window.authManager = { apiCall: (endpoint, options) => window.energyApi.apiCall(endpoint, options) };
