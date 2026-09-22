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
    _inverter: null,
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
        this._inverter = null;
        this._excludeSet = null;
        this._groupMapping = null;
    },

    get grid() {
        if (this._grid === null) {
            this._grid = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.role === 'grid')
                : [];
        }
        return this._grid;
    },

    get production() {
        if (this._production === null) {
            this._production = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.role === 'pv')
                : [];
        }
        return this._production;
    },

    get battery() {
        if (this._battery === null) {
            this._battery = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.role === 'battery')
                : [];
        }
        return this._battery;
    },

    get inverter() {
        if (this._inverter === null) {
            this._inverter = this._channelData && Array.isArray(this._channelData)
                ? this._channelData.filter(ch => ch.role === 'inverter')
                : [];
        }
        return this._inverter;
    },

    get excludeFromOther() {
        if (this._excludeSet === null) {
            const gridIndices = this.grid.map(ch => String(ch.index));
            const productionIndices = this.production.map(ch => String(ch.index));
            const batteryIndices = this.battery.map(ch => String(ch.index));
            const inverterIndices = this.inverter.map(ch => String(ch.index));
            this._excludeSet = new Set([...gridIndices, ...productionIndices, ...batteryIndices, ...inverterIndices]);
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

    // Archives the device actually has (Map of name -> size), filled by the page from the
    // folder listings. null = not listed yet: nothing is known, so the fetch is attempted.
    archiveFiles: { monthly: null, yearly: null },

    // localStorage cache configuration
    CONFIG: {
        PREFIX: 'energyme_csv_',
        COMPLETED_TTL_MS: 30 * 24 * 60 * 60 * 1000,  // 1 month
        CURRENT_MONTH_TTL_MS: 60 * 60 * 1000         // 1 hour
    },

    // Archive names are UTC months/years
    getCurrentYearMonth() {
        return new Date().toISOString().substring(0, 7);
    },

    isCompletedMonth(yearMonth) {
        return yearMonth < this.getCurrentYearMonth();
    },

    isCompletedYear(year) {
        return parseInt(year) < new Date().getUTCFullYear();
    },

    saveToLocalCache(key, data, completed, size) {
        try {
            const cacheEntry = {
                data: data,
                timestamp: Date.now(),
                completed: completed,
                size: size
            };
            localStorage.setItem(this.CONFIG.PREFIX + key, JSON.stringify(cacheEntry));
        } catch (e) {
            console.warn('Failed to save to localStorage:', e);
        }
    },

    removeFromLocalCache(key) {
        try {
            localStorage.removeItem(this.CONFIG.PREFIX + key);
        } catch (e) { /* storage unavailable */ }
    },

    // expectedSize: the listed file size, when known. A different size means the file was
    // replaced (restore from backup, a month merged in), whatever the TTL says.
    loadFromLocalCache(key, expectedSize) {
        try {
            const cached = localStorage.getItem(this.CONFIG.PREFIX + key);
            if (!cached) return null;

            const entry = JSON.parse(cached);
            if (expectedSize !== undefined && entry.size !== expectedSize) {
                localStorage.removeItem(this.CONFIG.PREFIX + key);
                return null;
            }
            const age = Date.now() - entry.timestamp;
            // Long TTL only for data saved after its period closed: a copy taken while the
            // period was current is partial however old the period is now.
            const ttl = entry.completed === true ? this.CONFIG.COMPLETED_TTL_MS : this.CONFIG.CURRENT_MONTH_TTL_MS;

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
        const cacheKey = `${type}_${key}`;

        // Known not to exist, checked before any cache: the firmware deletes a monthly
        // archive once merged into the yearly one, so a cached copy would be stale.
        const known = this.archiveFiles[type];
        if (known && !known.has(key)) {
            delete this[type][key];
            this.removeFromLocalCache(cacheKey);
            throw new Error(`No ${type} archive for ${key}`);
        }

        const size = known ? known.get(key) : undefined;
        const memory = this[type][key];
        if (memory && (size === undefined || memory.size === size)) {
            return memory.data;
        }

        let data = this.loadFromLocalCache(cacheKey, size);
        if (data) {
            this[type][key] = { data, size };
            return data;
        }

        const isCompleted = type === 'monthly' ? this.isCompletedMonth(key) : this.isCompletedYear(key);
        const filename = `energy/${type}/${key}.csv.gz`;
        data = await DataHelpers.decompressGzipFile(filename);
        this[type][key] = { data, size };
        this.saveToLocalCache(cacheKey, data, isCompleted, size);
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
     * Parse CSV energy data into array of objects, optionally only rows whose timestamp
     * starts with one of `prefixes` (e.g. the months needed out of a yearly archive)
     */
    parseCsvEnergyData(csvText, prefixes = null) {
        const lines = csvText.trim().split('\n');
        const data = [];

        for (let i = 1; i < lines.length; i++) {
            if (prefixes && !prefixes.some(prefix => lines[i].startsWith(prefix))) continue;
            const [timestamp, channel, activeImported, activeExported] = lines[i].split(',');
            const imported = parseFloat(activeImported);
            const exported = parseFloat(activeExported);

            data.push({
                timestamp: timestamp,
                channel: parseInt(channel),
                activeImported: imported,
                activeExported: exported
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
     * Other = total available energy (grid import - grid export + solar + battery/inverter) - tracked loads
     * Matches real-time power flow logic in power-flow.js and chart-helpers balance formula
     */
    calculateOtherConsumption(periodData, excludeFromOther, periodExportData = {}) {
        const gridImport = ChannelCache.grid.reduce((sum, ch) => sum + (periodData[ch.index] || 0), 0);
        const gridExport = ChannelCache.grid.reduce((sum, ch) => sum + (periodExportData[ch.index] || 0), 0);
        const solarImport = ChannelCache.production.reduce((sum, ch) => sum + (periodData[ch.index] || 0), 0);
        const hasInverter = ChannelCache.inverter.length > 0;

        let totalAvailable;
        if (hasInverter) {
            const inverterDischarge = ChannelCache.inverter.reduce((sum, ch) => sum + (periodData[ch.index] || 0), 0);
            const inverterCharge = ChannelCache.inverter.reduce((sum, ch) => sum + (periodExportData[ch.index] || 0), 0);
            totalAvailable = gridImport - gridExport + solarImport + inverterDischarge - inverterCharge;
        } else {
            const batteryDischarge = ChannelCache.battery.reduce((sum, ch) => sum + (periodData[ch.index] || 0), 0);
            const batteryCharge = ChannelCache.battery.reduce((sum, ch) => sum + (periodExportData[ch.index] || 0), 0);
            totalAvailable = gridImport - gridExport + solarImport + batteryDischarge - batteryCharge;
        }

        let trackedLoadConsumption = 0;
        Object.keys(periodData).forEach(channel => {
            if (!excludeFromOther.has(channel)) {
                trackedLoadConsumption += periodData[channel];
            }
        });

        // Not clamped to 0: a negative "Other" (tracked loads exceed available energy) is a
        // real signal - calibration mismatch, mis-assigned role, double-counting or CT
        // reversal - and the bar chart renders it below zero. See issue #169.
        return totalAvailable - trackedLoadConsumption;
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

// ============================================================================
// ENERGY AGGREGATION - cumulative readings -> per-period consumption, local time
// ============================================================================
// Readings are cumulative Wh counters stamped in UTC. Each interval between two
// consecutive readings of a channel is attributed to the local-time bucket its start
// falls in, so every interval is counted exactly once in every view: the hours of a
// day sum to that day in the monthly view, the days to the month, and so on, in any
// timezone (including DST days and :30/:45 offsets).
const EnergyAggregation = {
    MS_PER_HOUR: 3600 * 1000,
    MS_PER_DAY: 24 * 3600 * 1000,
    // Longest gap between readings whose energy is spread over its hours. Every view loads
    // at least one day around its period, so up to this length both ends of a gap are
    // visible to every view and they agree; longer gaps (device off for days) are unknown.
    MAX_SPREAD_MS: 24 * 3600 * 1000,

    pad2(n) {
        return String(n).padStart(2, '0');
    },

    localDateString(date) {
        return `${date.getFullYear()}-${this.pad2(date.getMonth() + 1)}-${this.pad2(date.getDate())}`;
    },

    /**
     * Bucket key of an interval starting at `date` for a view, or null when it lies
     * outside the selected local period. Daily keys are the local start time 'HH:MM'
     * (':00' except in half-hour-offset zones); the repeated hour of a DST fall-back
     * day shares one key, so its energy is merged, not lost.
     */
    bucketKey(date, view, period) {
        const year = String(date.getFullYear());
        const month = this.pad2(date.getMonth() + 1);
        if (view === 'daily') {
            if (this.localDateString(date) !== period) return null;
            return `${this.pad2(date.getHours())}:${this.pad2(date.getMinutes())}`;
        }
        if (view === 'monthly') return `${year}-${month}` === period ? this.pad2(date.getDate()) : null;
        if (view === 'yearly') return year === period ? month : null;
        if (view === 'total') return year;
        return null;
    },

    /**
     * Local dates holding readings from the given UTC dates (a UTC day spans two local
     * dates unless the offset is zero), never after local today.
     */
    localDatesForUtcDates(utcDates, now = new Date()) {
        const today = this.localDateString(now);
        const dates = new Set();
        utcDates.forEach(utcDate => {
            const start = Date.parse(utcDate + 'T00:00:00Z');
            [start, start + 23 * this.MS_PER_HOUR].forEach(t => {
                const local = this.localDateString(new Date(t));
                if (local <= today) dates.add(local);
            });
        });
        return [...dates].sort();
    },

    /**
     * Full local-time label of a bucket for CSV export: 'YYYY-MM-DD HH:MM', 'YYYY-MM-DD',
     * 'YYYY-MM' or 'YYYY'.
     */
    csvPeriodLabel(key, view, period) {
        if (view === 'daily') return `${period} ${key}`;
        if (view === 'monthly' || view === 'yearly') return `${period}-${key}`;
        return key;
    },

    /**
     * UTC dates ('YYYY-MM-DD') whose files must be loaded to cover a local period plus
     * the reading that closes its last interval, never past today (UTC). One day of margin
     * on each side covers every UTC offset (-12h..+14h) and gaps up to MAX_SPREAD_MS.
     */
    utcDatesForPeriod(view, period, firstYear = null, now = new Date()) {
        const today = now.toISOString().substring(0, 10);
        let start, end;
        if (view === 'daily') {
            const [y, m, d] = period.split('-').map(Number);
            start = new Date(y, m - 1, d);
            end = new Date(y, m - 1, d + 1);
        } else if (view === 'monthly') {
            const [y, m] = period.split('-').map(Number);
            start = new Date(y, m - 1, 1);
            end = new Date(y, m, 1);
        } else if (view === 'yearly') {
            const y = Number(period);
            start = new Date(y, 0, 1);
            end = new Date(y + 1, 0, 1);
        } else {
            start = new Date(Number(firstYear), 0, 1);
            end = new Date(now.getTime() + this.MS_PER_DAY);
        }
        const dates = [];
        const last = new Date(end.getTime() + this.MS_PER_DAY).toISOString().substring(0, 10);
        let cursor = new Date(start.getTime() - this.MS_PER_DAY).toISOString().substring(0, 10);
        for (let i = 0; i < 400 * 200 && cursor <= last && cursor <= today; i++) {
            dates.push(cursor);
            cursor = new Date(Date.parse(cursor + 'T00:00:00Z') + this.MS_PER_DAY).toISOString().substring(0, 10);
        }
        return dates;
    },

    /**
     * Sum per-bucket consumption (kWh) for the selected local period.
     * entries: [{timestamp, channel, activeImported, activeExported}] in Wh, any order,
     * duplicates allowed (overlapping daily files and archives).
     * Returns { imported: {key: {channel: kWh}}, exported: {...} }.
     */
    aggregate(entries, view, period) {
        const byChannel = {};
        entries.forEach(entry => {
            const t = Date.parse(entry.timestamp);
            if (isNaN(t) || isNaN(entry.activeImported)) return;
            const channel = String(entry.channel);
            if (!byChannel[channel]) byChannel[channel] = new Map();
            byChannel[channel].set(t, entry);
        });

        const imported = {};
        const exported = {};
        const add = (target, key, channel, value) => {
            if (!target[key]) target[key] = {};
            target[key][channel] = (target[key][channel] || 0) + value;
        };

        Object.keys(byChannel).forEach(channel => {
            const times = [...byChannel[channel].keys()].sort((a, b) => a - b);
            for (let i = 1; i < times.length; i++) {
                const a = byChannel[channel].get(times[i - 1]);
                const b = byChannel[channel].get(times[i]);
                // Counter reset (e.g. meter replaced or NVS cleared): no negative energy
                const importWh = Math.max(0, b.activeImported - a.activeImported);
                const exportWh = Math.max(0, (b.activeExported || 0) - (a.activeExported || 0));

                // A gap longer than one sample is spread evenly over hourly slices, so a
                // missing reading does not dump hours of energy into a single bucket.
                const span = times[i] - times[i - 1];
                if (span > this.MAX_SPREAD_MS) continue;
                const slices = Math.max(1, Math.round(span / this.MS_PER_HOUR));
                for (let s = 0; s < slices; s++) {
                    const key = this.bucketKey(new Date(times[i - 1] + s * span / slices), view, period);
                    if (key === null) continue;
                    add(imported, key, channel, importWh / slices / 1000);
                    if (exportWh > 0) add(exported, key, channel, exportWh / slices / 1000);
                }
            }
        });

        return { imported, exported };
    },

    /**
     * Add the derived 'Other' channel and drop grid/production/battery/inverter channels
     * from the displayed set, keeping the raw per-bucket values for the balance chart.
     */
    finalize({ imported, exported }) {
        const excludeFromOther = ChannelCache.excludeFromOther;
        const displayImported = {};
        const displayExported = {};
        Object.keys(imported).forEach(key => {
            const rawImport = imported[key];
            const rawExport = exported[key] || {};
            const display = { ...rawImport };
            if (ChannelCache.hasGrid && DataHelpers.hasLoadSubChannels(rawImport, excludeFromOther)) {
                display['Other'] = DataHelpers.calculateOtherConsumption(rawImport, excludeFromOther, rawExport);
            }
            const displayExport = { ...rawExport };
            excludeFromOther.forEach(ch => {
                delete display[ch];
                delete displayExport[ch];
            });
            displayImported[key] = display;
            if (Object.keys(displayExport).length > 0) displayExported[key] = displayExport;
        });
        return { imported: displayImported, exported: displayExported, rawImported: imported, rawExported: exported };
    }
};

// Export to global scope (browser) or as a module (node unit tests)
if (typeof window !== 'undefined') {
    window.ChannelCache = ChannelCache;
    window.CircularBuffer = CircularBuffer;
    window.ArchiveCache = ArchiveCache;
    window.DataHelpers = DataHelpers;
    window.EnergyAggregation = EnergyAggregation;
}
if (typeof module !== 'undefined') {
    module.exports = { ChannelCache, ArchiveCache, DataHelpers, EnergyAggregation };
}
