/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright (C) 2025 Jibril Sharafi */

/**
 * EnergyMe Power Flow Helpers
 * Energy flow diagram logic and animations
 */

// ============================================================================
// POWER FLOW CONFIGURATION
// ============================================================================
const PowerFlowConfig = {
    MAX_HISTORY: 60,
    SPARKLINE_ALPHA: 0.7,
    SPARKLINE_CANVAS_WIDTH: 120,
    SPARKLINE_CANVAS_HEIGHT: 40,

    FLOW_SPEED_THRESHOLDS: [
        { watts: 10, duration: '2s' },
        { watts: 100, duration: '1.5s' },
        { watts: 500, duration: '1s' },
        { watts: 1000, duration: '0.7s' },
        { watts: 2000, duration: '0.5s' },
        { watts: 5000, duration: '0.4s' },
        { watts: Infinity, duration: '0.3s' }
    ],

    LOAD_COLOR_THRESHOLDS: [
        { ratio: 0.3, color: '#4CAF50' },
        { ratio: 0.6, color: '#FF9800' },
        { ratio: Infinity, color: '#f44336' }
    ],

    ACTIVE_LINE_THRESHOLD: 10,
    OTHER_HYSTERESIS_THRESHOLD: 10,
    OTHER_HYSTERESIS_COUNT: 3,
    OTHER_LOAD_COLOR: '#888',
    TOOLTIP_HEIGHT: 50,
    TOOLTIP_X_OFFSET: 60,

    NODE_COLORS: {
        solar: '#FF9800',
        grid: '#2196F3',
        battery: '#9C27B0',
        home: '#4CAF50'
    }
};

// ============================================================================
// POWER FLOW STATE
// ============================================================================
const PowerFlowState = {
    flowHistory: {
        solar: null,
        grid: null,
        battery: null,
        home: null
    },
    filteredPowers: { solar: null, grid: null, battery: null, home: null },
    loadHistory: {},
    loadFilteredPowers: {},
    otherLoadCounter: 0,
    otherLoadShown: false,
    previousLoadKeys: null,

    init() {
        this.flowHistory = {
            solar: new CircularBuffer(PowerFlowConfig.MAX_HISTORY),
            grid: new CircularBuffer(PowerFlowConfig.MAX_HISTORY),
            battery: new CircularBuffer(PowerFlowConfig.MAX_HISTORY),
            home: new CircularBuffer(PowerFlowConfig.MAX_HISTORY)
        };
    }
};

// ============================================================================
// POWER FLOW HELPERS
// ============================================================================
const PowerFlowHelpers = {
    /**
     * Format power value with appropriate unit
     */
    formatPower(watts) {
        return watts.toFixed(0) + ' W';
    },

    /**
     * Calculate animation speed based on power
     */
    getFlowSpeed(watts) {
        const absWatts = Math.abs(watts);
        for (const threshold of PowerFlowConfig.FLOW_SPEED_THRESHOLDS) {
            if (absWatts <= threshold.watts) {
                return threshold.duration;
            }
        }
        return PowerFlowConfig.FLOW_SPEED_THRESHOLDS[PowerFlowConfig.FLOW_SPEED_THRESHOLDS.length - 1].duration;
    },

    /**
     * Get color intensity based on power
     */
    getLoadColor(watts, maxWatts) {
        const absWatts = Math.abs(watts);
        const ratio = Math.min(absWatts / Math.max(maxWatts, 1000), 1);

        for (const threshold of PowerFlowConfig.LOAD_COLOR_THRESHOLDS) {
            if (ratio < threshold.ratio) {
                return threshold.color;
            }
        }
        return PowerFlowConfig.LOAD_COLOR_THRESHOLDS[PowerFlowConfig.LOAD_COLOR_THRESHOLDS.length - 1].color;
    },

    /**
     * Add history point with exponential filtering
     */
    addToFlowHistory(nodeType, value) {
        if (PowerFlowState.filteredPowers[nodeType] === null) {
            PowerFlowState.filteredPowers[nodeType] = value;
        } else {
            PowerFlowState.filteredPowers[nodeType] = PowerFlowConfig.SPARKLINE_ALPHA * value +
                (1 - PowerFlowConfig.SPARKLINE_ALPHA) * PowerFlowState.filteredPowers[nodeType];
        }
        PowerFlowState.flowHistory[nodeType].push(PowerFlowState.filteredPowers[nodeType]);
    },

    /**
     * Add load history point with exponential filtering
     */
    addToLoadHistory(loadKey, value) {
        if (!PowerFlowState.loadHistory[loadKey]) {
            PowerFlowState.loadHistory[loadKey] = new CircularBuffer(PowerFlowConfig.MAX_HISTORY);
            PowerFlowState.loadFilteredPowers[loadKey] = null;
        }

        if (PowerFlowState.loadFilteredPowers[loadKey] === null) {
            PowerFlowState.loadFilteredPowers[loadKey] = value;
        } else {
            PowerFlowState.loadFilteredPowers[loadKey] = PowerFlowConfig.SPARKLINE_ALPHA * value +
                (1 - PowerFlowConfig.SPARKLINE_ALPHA) * PowerFlowState.loadFilteredPowers[loadKey];
        }
        PowerFlowState.loadHistory[loadKey].push(PowerFlowState.loadFilteredPowers[loadKey]);
    },

    /**
     * Setup hover events for main energy nodes
     */
    setupSparklineHovers(flowHistory, loadHistory, nodeColors) {
        const nodes = [
            { id: 'node-solar', type: 'solar' },
            { id: 'node-grid', type: 'grid' },
            { id: 'node-battery', type: 'battery' },
            { id: 'node-home', type: 'home' }
        ];

        nodes.forEach(({ id, type }) => {
            const el = document.getElementById(id);
            if (!el) return;

            el.style.cursor = 'pointer';
            el.addEventListener('mouseenter', () => ChartHelpers.showSparkline(type, el, false, flowHistory, loadHistory, nodeColors));
            el.addEventListener('mouseleave', () => ChartHelpers.hideSparkline());
        });
    },

    /**
     * Setup hover events for load nodes
     */
    setupLoadSparklineHovers(flowHistory, loadHistory, nodeColors) {
        const loadNodes = document.querySelectorAll('.load-node');
        loadNodes.forEach((node) => {
            const valueEl = node.querySelector('.load-node-value');
            if (!valueEl) return;

            const loadKey = valueEl.id.replace('load-value-', '');
            node.style.cursor = 'pointer';
            node.addEventListener('mouseenter', () => ChartHelpers.showSparkline(loadKey, node, true, flowHistory, loadHistory, nodeColors));
            node.addEventListener('mouseleave', () => ChartHelpers.hideSparkline());
        });
    },

    /**
     * Update energy flow diagram with real-time data
     */
    updateEnergyFlow(meterData, channelData) {
        if (!meterData || meterData.length === 0 || !channelData || !Array.isArray(channelData)) return;

        const gridChannels = ChannelCache.grid;
        const productionChannels = ChannelCache.production;
        const batteryChannels = ChannelCache.battery;

        const hasProduction = productionChannels.length > 0;
        const hasBattery = batteryChannels.length > 0;
        const hasGrid = gridChannels.length > 0;

        // Always show energy flow section
        const flowSection = document.getElementById('energy-flow-section');
        flowSection.classList.add('visible');

        // Remove loading message if data available
        const flowDiagram = document.getElementById('energy-flow-diagram');
        if (flowDiagram && meterData && meterData.length > 0) {
            const loadingMsg = flowDiagram.querySelector('[data-loading-msg]');
            if (loadingMsg) loadingMsg.remove();

            // Fade in SVG and nodes
            document.getElementById('energy-flow-svg').style.opacity = '1';
            document.getElementById('node-grid').style.opacity = '1';
            document.getElementById('node-home').style.opacity = '1';
            if (hasProduction) document.getElementById('node-solar').style.opacity = '1';
            if (hasBattery) document.getElementById('node-battery').style.opacity = '1';
        }

        // Show/hide nodes
        document.getElementById('node-solar').style.display = hasProduction ? 'flex' : 'none';
        document.getElementById('node-battery').style.display = hasBattery ? 'flex' : 'none';
        document.getElementById('node-grid').style.display = 'flex';
        document.getElementById('node-home').style.display = 'flex';

        // Calculate power values
        let gridPower = 0;
        gridChannels.forEach(ch => {
            const meter = meterData.find(m => m.index === ch.index);
            if (meter) gridPower += meter.data.activePower;
        });

        let solarPower = 0;
        productionChannels.forEach(ch => {
            const meter = meterData.find(m => m.index === ch.index);
            if (meter) solarPower += Math.abs(meter.data.activePower);
        });

        let batteryPower = 0;
        batteryChannels.forEach(ch => {
            const meter = meterData.find(m => m.index === ch.index);
            if (meter) batteryPower += meter.data.activePower;
        });

        const homePower = gridPower + solarPower + batteryPower;

        // Record history
        this.addToFlowHistory('solar', solarPower);
        this.addToFlowHistory('grid', gridPower);
        this.addToFlowHistory('battery', batteryPower);
        this.addToFlowHistory('home', Math.max(0, homePower));

        // Update display values
        document.getElementById('grid-power').textContent = this.formatPower(Math.abs(gridPower));
        document.getElementById('home-power').textContent = this.formatPower(Math.max(0, homePower));

        if (hasProduction) {
            document.getElementById('solar-power').textContent = this.formatPower(solarPower);
        }

        if (hasBattery) {
            document.getElementById('battery-power').textContent = this.formatPower(Math.abs(batteryPower));
        }

        // Update SVG lines
        const lineSolar = document.getElementById('line-solar');
        const lineGrid = document.getElementById('line-grid');
        const lineHome = document.getElementById('line-home');
        const lineBattery = document.getElementById('line-battery');

        lineSolar.style.display = hasProduction ? 'block' : 'none';
        if (hasProduction) {
            lineSolar.classList.toggle('active', solarPower > PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
            lineSolar.classList.toggle('inactive', solarPower <= PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
            lineSolar.classList.remove('reverse');
            lineSolar.style.setProperty('--flow-speed', this.getFlowSpeed(solarPower));
        }

        lineGrid.classList.toggle('active', Math.abs(gridPower) > PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
        lineGrid.classList.toggle('inactive', Math.abs(gridPower) <= PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
        lineGrid.classList.toggle('reverse', gridPower < -PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
        lineGrid.style.setProperty('--flow-speed', this.getFlowSpeed(gridPower));

        lineHome.classList.toggle('active', homePower > PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
        lineHome.classList.toggle('inactive', homePower <= PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
        lineHome.classList.remove('reverse');
        lineHome.style.setProperty('--flow-speed', this.getFlowSpeed(homePower));

        lineBattery.style.display = hasBattery ? 'block' : 'none';
        if (hasBattery) {
            lineBattery.classList.toggle('active', Math.abs(batteryPower) > PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
            lineBattery.classList.toggle('inactive', Math.abs(batteryPower) <= PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
            lineBattery.classList.toggle('reverse', batteryPower < -PowerFlowConfig.ACTIVE_LINE_THRESHOLD);
            lineBattery.style.setProperty('--flow-speed', this.getFlowSpeed(batteryPower));
        }

        // Update load breakdown
        this.updateLoadBreakdown(hasGrid, hasProduction, hasBattery, gridPower, solarPower, batteryPower, homePower, meterData, channelData);
    },

    /**
     * Update load breakdown section
     */
    updateLoadBreakdown(hasGrid, hasProduction, hasBattery, gridPower, solarPower, batteryPower, homePower, meterData, channelData) {
        const gridIndices = ChannelCache.grid.map(ch => ch.index);
        const productionIndices = ChannelCache.production.map(ch => ch.index);
        const batteryIndices = ChannelCache.battery.map(ch => ch.index);
        const specialIndices = new Set([...gridIndices, ...productionIndices, ...batteryIndices]);

        const loadChannels = meterData.filter(ch => !specialIndices.has(ch.index));

        // Count shared group labels
        const groupLabelCounts = {};
        loadChannels.forEach(ch => {
            const groupLabel = channelData[ch.index]?.groupLabel;
            if (groupLabel) {
                groupLabelCounts[groupLabel] = (groupLabelCounts[groupLabel] || 0) + 1;
            }
        });

        // Aggregate loads
        const aggregatedLoads = [];
        const processedGroups = new Set();
        let trackedLoadPower = 0;

        loadChannels.forEach(ch => {
            const groupLabel = channelData[ch.index]?.groupLabel;
            const isSharedGroup = groupLabel && groupLabelCounts[groupLabel] > 1;

            if (isSharedGroup) {
                if (!processedGroups.has(groupLabel)) {
                    let groupPower = 0;
                    const groupIndices = [];
                    loadChannels.forEach(c => {
                        if (channelData[c.index]?.groupLabel === groupLabel) {
                            groupPower += c.data.activePower;
                            groupIndices.push(c.index);
                        }
                    });
                    aggregatedLoads.push({
                        key: 'group-' + groupIndices.join('-'),
                        label: groupLabel,
                        icon: channelData[ch.index]?.icon || '⚡',
                        power: groupPower
                    });
                    trackedLoadPower += groupPower;
                    processedGroups.add(groupLabel);
                }
            } else {
                aggregatedLoads.push({
                    key: 'ch-' + ch.index,
                    label: ch.label,
                    icon: channelData[ch.index]?.icon || '⚡',
                    power: ch.data.activePower
                });
                trackedLoadPower += ch.data.activePower;
            }
        });

        // Calculate "Other"
        const otherPower = Math.max(0, homePower - trackedLoadPower);
        const hasLoads = aggregatedLoads.length > 0;

        if (hasGrid && hasLoads) {
            if (otherPower > PowerFlowConfig.OTHER_HYSTERESIS_THRESHOLD) {
                PowerFlowState.otherLoadCounter++;
                if (PowerFlowState.otherLoadCounter >= PowerFlowConfig.OTHER_HYSTERESIS_COUNT && !PowerFlowState.otherLoadShown) {
                    PowerFlowState.otherLoadShown = true;
                }
            } else {
                PowerFlowState.otherLoadCounter = 0;
            }

            if (PowerFlowState.otherLoadShown) {
                aggregatedLoads.push({
                    key: 'other',
                    label: 'Other',
                    icon: '',
                    power: otherPower,
                    isOther: true
                });
            }
        }

        const loadsContainer = document.getElementById('loads-container');
        const diagram = document.getElementById('energy-flow-diagram');
        const svg = document.getElementById('energy-flow-svg');
        const loadLinesGroup = document.getElementById('load-lines');

        if (!hasLoads) {
            loadsContainer.style.display = 'none';
            diagram.classList.remove('with-loads');
            svg.setAttribute('viewBox', '0 0 300 300');
            loadLinesGroup.innerHTML = '';
            PowerFlowState.previousLoadKeys = null;
            return;
        }

        loadsContainer.style.display = 'flex';
        diagram.classList.add('with-loads');
        svg.setAttribute('viewBox', '0 0 600 300');

        const currentLoadKeys = aggregatedLoads.map(l => l.key).join(',');
        const shouldRebuild = currentLoadKeys !== PowerFlowState.previousLoadKeys;
        const maxPower = Math.max(...aggregatedLoads.map(l => Math.abs(l.power)), 1);

        if (shouldRebuild) {
            PowerFlowState.previousLoadKeys = currentLoadKeys;

            let loadsHtml = '';
            aggregatedLoads.forEach((load) => {
                const color = load.isOther ? PowerFlowConfig.OTHER_LOAD_COLOR : this.getLoadColor(load.power, maxPower);
                loadsHtml += '<div class="load-node' + (load.isOther ? ' other-load' : '') + '" style="border-left-color: ' + color + ';">';
                loadsHtml += '<div class="load-node-icon" style="display: none;">' + load.icon + '</div>';
                loadsHtml += '<div class="load-node-info">';
                loadsHtml += '<div class="load-node-label">' + load.label + '</div>';
                loadsHtml += '<div class="load-node-value" id="load-value-' + load.key + '">' + this.formatPower(load.power) + '</div>';
                loadsHtml += '</div></div>';
                this.addToLoadHistory(load.key, load.power);
            });
            loadsContainer.innerHTML = loadsHtml;
            this.setupLoadSparklineHovers(PowerFlowState.flowHistory, PowerFlowState.loadHistory, PowerFlowConfig.NODE_COLORS);

            let linesHtml = '';
            const loadCount = aggregatedLoads.length;
            const loadSpacing = Math.min(40, 280 / Math.max(loadCount, 1));
            const startY = 150 - ((loadCount - 1) * loadSpacing) / 2;

            aggregatedLoads.forEach((load, idx) => {
                const loadY = startY + idx * loadSpacing;
                const color = load.isOther ? PowerFlowConfig.OTHER_LOAD_COLOR : this.getLoadColor(load.power, maxPower);
                const speed = this.getFlowSpeed(load.power);
                const isActive = Math.abs(load.power) > PowerFlowConfig.ACTIVE_LINE_THRESHOLD;

                linesHtml += '<path id="load-line-' + load.key + '" ';
                linesHtml += 'class="flow-line' + (isActive ? ' active' : ' inactive') + '" ';
                linesHtml += 'style="stroke: ' + color + '; --flow-speed: ' + speed + ';" ';
                linesHtml += 'd="M 265 150 C 300 150, 310 ' + loadY + ', 340 ' + loadY + '" />';
            });
            loadLinesGroup.innerHTML = linesHtml;
        } else {
            aggregatedLoads.forEach((load) => {
                const valueEl = document.getElementById('load-value-' + load.key);
                if (valueEl) {
                    valueEl.textContent = this.formatPower(load.power);
                }

                const lineEl = document.getElementById('load-line-' + load.key);
                if (lineEl) {
                    const color = load.isOther ? PowerFlowConfig.OTHER_LOAD_COLOR : this.getLoadColor(load.power, maxPower);
                    const speed = this.getFlowSpeed(load.power);
                    const isActive = Math.abs(load.power) > PowerFlowConfig.ACTIVE_LINE_THRESHOLD;

                    lineEl.style.stroke = color;
                    lineEl.style.setProperty('--flow-speed', speed);
                    lineEl.classList.toggle('active', isActive);
                    lineEl.classList.toggle('inactive', !isActive);
                }
                this.addToLoadHistory(load.key, load.power);
            });
        }
    }
};

// Export to global scope
window.PowerFlowConfig = PowerFlowConfig;
window.PowerFlowState = PowerFlowState;
window.PowerFlowHelpers = PowerFlowHelpers;
