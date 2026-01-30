/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright (C) 2025 Jibril Sharafi */

/**
 * EnergyMe Data Helpers
 * CSV parsing, caching, and data processing utilities
 */

// ============================================================================
// CHANNEL CACHE - Cached channel classifications for efficient repeated lookups
// ============================================================================
const ChannelCache = {
    _grid: null,
    _production: null,
    _battery: null,
    _excludeSet: null,
    _groupMapping: null,
    _channelData: null,

    // Set the channel data source
    setChannelData(data) {
        this._channelData = data;
        this.invalidate();
    },

    invalidate() {
        this._grid = null;
        this._production = null;
        this._battery = null;
        this._excludeSet = null;
        this._groupMapping = null;
    },

    get grid() {
        if (this._grid === null) {
            this._grid = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.isGrid === true)
                : [];
        }
        return this._grid;
    },

    get production() {
        if (this._production === null) {
            this._production = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.isProduction === true)
                : [];
        }
        return this._production;
    },

    get battery() {
        if (this._battery === null) {
            this._battery = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.isBattery === true)
                : [];
        }
        return this._battery;
    },

    get excludeFromOther() {
        if (this._excludeSet === null) {
            const gridIndices = this.grid.map(ch => String(ch.index));
            const productionIndices = this.production.map(ch => String(ch.index));
            const batteryIndices = this.battery.map(ch => String(ch.index));
            this._excludeSet = new Set([...gridIndices, ...productionIndices, ...batteryIndices]);
        }
        return this._excludeSet;
    },

    get primaryGridIndex() {
        return this.grid.length > 0 ? this.grid[0].index : 0;
    },

    get hasGrid() {
        return this.grid.length > 0;
    },

    get groupMapping() {
        if (this._groupMapping === null) {
            this._groupMapping = this._buildGroupMapping();
        }
        return this._groupMapping;
    },

    _buildGroupMapping() {
        const channelToGroup = {};
        const groups = {};

        if (!this._channelData || !Array.isArray(this._channelData)) {
            return { channelToGroup, groups };
        }

        const gridIndices = this.grid.map(ch => ch.index);

        this._channelData.forEach(ch => {
            if (gridIndices.includes(ch.index)) return;

            const groupLabel = ch.groupLabel || ch.label || `Channel ${ch.index}`;
            channelToGroup[ch.index] = groupLabel;

            if (!groups[groupLabel]) {
                groups[groupLabel] = { channels: [] };
            }
            groups[groupLabel].channels.push(ch.index);
        });

        return { channelToGroup, groups };
    }
};

// ============================================================================
// CIRCULAR BUFFER - O(1) push operations for sparkline history
// ============================================================================
class CircularBuffer {
    constructor(maxSize) {
        this.maxSize = maxSize;
        this.buffer = new Array(maxSize);
        this.head = 0;
        this.count = 0;
    }

    push(value) {
        this.buffer[this.head] = value;
        this.head = (this.head + 1) % this.maxSize;
        if (this.count < this.maxSize) this.count++;
    }

    toArray() {
        if (this.count === 0) return [];
        const result = new Array(this.count);
        const start = this.count < this.maxSize ? 0 : this.head;
        for (let i = 0; i < this.count; i++) {
            result[i] = this.buffer[(start + i) % this.maxSize];
        }
        return result;
    }

    get length() { return this.count; }

    clear() {
        this.head = 0;
        this.count = 0;
    }
}

// ============================================================================
// ARCHIVE CACHE - Multi-level caching for energy data
// ============================================================================
const ArchiveCache = {
    monthly: {},
    yearly: {},
    dailyFilesSet: new Set(),

    // localStorage cache configuration
    CONFIG: {
        PREFIX: 'energyme_csv_',
        COMPLETED_TTL_MS: 30 * 24 * 60 * 60 * 1000,  // 1 month
        CURRENT_MONTH_TTL_MS: 60 * 60 * 1000         // 1 hour
    },

    getCurrentYearMonth() {
        const now = new Date();
        return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}`;
    },

    isCompletedMonth(yearMonth) {
        return yearMonth < this.getCurrentYearMonth();
    },

    isCompletedYear(year) {
        return parseInt(year) < new Date().getFullYear();
    },

    saveToLocalCache(key, data) {
        try {
            const cacheEntry = {
                data: data,
                timestamp: Date.now()
            };
            localStorage.setItem(this.CONFIG.PREFIX + key, JSON.stringify(cacheEntry));
        } catch (e) {
            console.warn('Failed to save to localStorage:', e);
        }
    },

    loadFromLocalCache(key, isCompleted) {
        try {
            const cached = localStorage.getItem(this.CONFIG.PREFIX + key);
            if (!cached) return null;

            const entry = JSON.parse(cached);
            const age = Date.now() - entry.timestamp;
            const ttl = isCompleted ? this.CONFIG.COMPLETED_TTL_MS : this.CONFIG.CURRENT_MONTH_TTL_MS;

            if (age > ttl) {
                localStorage.removeItem(this.CONFIG.PREFIX + key);
                return null;
            }

            return entry.data;
        } catch (e) {
            return null;
        }
    },

    async loadArchiveWithCache(type, key) {
        // Try memory cache first
        if (this[type][key]) {
            return this[type][key];
        }

        // Try localStorage
        const cacheKey = `${type}_${key}`;
        const isCompleted = type === 'monthly' ? this.isCompletedMonth(key) : this.isCompletedYear(key);
        let data = this.loadFromLocalCache(cacheKey, isCompleted);

        if (data) {
            this[type][key] = data;
            return data;
        }

        // Fetch from server
        const filename = `energy/${type}/${key}.csv.gz`;
        data = await DataHelpers.decompressGzipFile(filename);
        this[type][key] = data;
        this.saveToLocalCache(cacheKey, data);
        return data;
    }
};

// ============================================================================
// DATA HELPERS - CSV parsing and utility functions
// ============================================================================
const DataHelpers = {
    /**
     * Decompress gzip file from the device
     */
    async decompressGzipFile(filename) {
        const response = await energyApi.apiCall(`files/${encodeURIComponent(filename)}`, { method: 'GET' });
        if (!response.ok) {
            throw new Error(`Failed to fetch ${filename}: ${response.status}`);
        }

        const gzipData = await response.arrayBuffer();
        const decompressed = pako.inflate(new Uint8Array(gzipData), { to: 'string' });

        return decompressed;
    },

    /**
     * Filter CSV data by a date prefix
     */
    filterCsvByPrefix(csvText, prefix) {
        const lines = csvText.trim().split('\n');
        const header = lines[0];
        const dataLines = lines.slice(1).filter(line => line.startsWith(prefix));
        if (dataLines.length === 0) return null;
        return header + '\n' + dataLines.join('\n');
    },

    /**
     * Parse CSV energy data into array of objects
     */
    parseCsvEnergyData(csvText) {
        const lines = csvText.trim().split('\n');
        const data = [];

        for (let i = 1; i < lines.length; i++) {
            const [timestamp, channel, activeImported, activeExported] = lines[i].split(',');
            data.push({
                timestamp: timestamp,
                channel: parseInt(channel),
                activeImported: parseFloat(activeImported),
                activeExported: parseFloat(activeExported)
            });
        }

        return data;
    },

    /**
     * Load daily CSV data with smart fallback
     */
    async loadDailyCsvData(date) {
        // Only try daily folder if we know the file exists there
        if (ArchiveCache.dailyFilesSet.has(date)) {
            const csvFilename = `energy/daily/${date}.csv`;
            const gzFilename = `energy/daily/${date}.csv.gz`;

            try {
                const response = await energyApi.apiCall(`files/${encodeURIComponent(csvFilename)}`, { method: 'GET' });
                if (response.ok) return await response.text();
            } catch (e) { /* continue */ }

            try {
                return await this.decompressGzipFile(gzFilename);
            } catch (e) { /* continue */ }
        }

        // Try monthly archive
        const yearMonth = date.substring(0, 7);
        try {
            const monthlyData = await ArchiveCache.loadArchiveWithCache('monthly', yearMonth);
            const filtered = this.filterCsvByPrefix(monthlyData, date);
            if (filtered) return filtered;
        } catch (e) { /* continue */ }

        // Try yearly archive
        const year = date.substring(0, 4);
        try {
            const yearlyData = await ArchiveCache.loadArchiveWithCache('yearly', year);
            const filtered = this.filterCsvByPrefix(yearlyData, date);
            if (filtered) return filtered;
        } catch (e) { /* continue */ }

        throw new Error(`No CSV data found for date ${date}`);
    },

    /**
     * Load monthly CSV data with smart fallback
     */
    async loadMonthlyCsvData(yearMonth) {
        try {
            return await ArchiveCache.loadArchiveWithCache('monthly', yearMonth);
        } catch (e) { /* continue */ }

        const year = yearMonth.substring(0, 4);
        try {
            const yearlyData = await ArchiveCache.loadArchiveWithCache('yearly', year);
            const filtered = this.filterCsvByPrefix(yearlyData, yearMonth);
            if (filtered) return filtered;
        } catch (e) { /* continue */ }

        throw new Error(`No CSV data found for month ${yearMonth}`);
    },

    /**
     * Load yearly CSV data
     */
    async loadYearlyCsvData(year) {
        return await ArchiveCache.loadArchiveWithCache('yearly', year);
    },

    /**
     * Calculate "Other" consumption for a period's data
     */
    calculateOtherConsumption(periodData, excludeFromOther, gridChannelIndex) {
        const totalConsumption = periodData[gridChannelIndex] || 0;
        let trackedLoadConsumption = 0;

        Object.keys(periodData).forEach(channel => {
            if (!excludeFromOther.has(channel)) {
                trackedLoadConsumption += periodData[channel];
            }
        });

        return totalConsumption - trackedLoadConsumption;
    },

    /**
     * Check if there are load sub-channels
     */
    hasLoadSubChannels(channels, excludeFromOther) {
        const channelKeys = Array.isArray(channels) ? channels : Object.keys(channels);
        return channelKeys.some(ch => !excludeFromOther.has(String(ch)));
    },

    /**
     * Check if grouping is enabled
     */
    isGroupingEnabled(channelData) {
        if (!channelData || !Array.isArray(channelData)) return false;
        const gridChannelIndices = ChannelCache.grid.map(ch => ch.index);
        const nonGridChannels = channelData.filter(ch => !gridChannelIndices.includes(ch.index));
        const groupLabels = nonGridChannels.map(ch => ch.groupLabel).filter(Boolean);
        return groupLabels.length !== new Set(groupLabels).size;
    },

    /**
     * Aggregate consumption data by groups
     */
    aggregateByGroup(consumptionData) {
        const { channelToGroup, groups } = ChannelCache.groupMapping;
        const aggregated = {};
        const gridChannelKeys = ChannelCache.grid.map(ch => String(ch.index));

        Object.keys(consumptionData).forEach(period => {
            aggregated[period] = {};
            const periodData = consumptionData[period];

            if (periodData['Other'] !== undefined) {
                aggregated[period]['Other'] = periodData['Other'];
            }

            Object.keys(periodData).forEach(channel => {
                if (channel === 'Other' || gridChannelKeys.includes(channel)) return;

                const channelIndex = parseInt(channel);
                const groupLabel = channelToGroup[channelIndex];

                if (groupLabel === undefined) {
                    aggregated[period][channel] = periodData[channel];
                } else if (groups[groupLabel] && groups[groupLabel].channels.length === 1) {
                    aggregated[period][channel] = periodData[channel];
                } else {
                    const groupKey = `group_${groupLabel}`;
                    if (!aggregated[period][groupKey]) {
                        aggregated[period][groupKey] = 0;
                    }
                    aggregated[period][groupKey] += periodData[channel];
                }
            });
        });

        return aggregated;
    }
};

// Export to global scope
window.ChannelCache = ChannelCache;
window.CircularBuffer = CircularBuffer;
window.ArchiveCache = ArchiveCache;
window.DataHelpers = DataHelpers;
