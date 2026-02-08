/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright (C) 2025 Jibril Sharafi */

/**
 * EnergyMe Chart Helpers
 * Chart.js configuration and management utilities
 */

// ============================================================================
// CHART CONFIGURATION
// ============================================================================
const ChartConfig = {
    // Perceptually-optimized colors for maximum distinction
    COLORS: [
        'rgba(255,67,54,0.8)',    // Red
        'rgba(251,188,4,0.8)',    // Amber
        'rgba(76,175,80,0.8)',    // Green
        'rgba(0,188,212,0.8)',    // Cyan
        'rgba(63,81,181,0.8)',    // Indigo
        'rgba(156,39,176,0.8)',   // Purple
        'rgba(233,30,99,0.8)',    // Pink
        'rgba(255,152,0,0.8)',    // Orange
        'rgba(76,110,245,0.8)',   // Royal Blue
        'rgba(129,199,132,0.8)', // Light Green
        'rgba(77,182,172,0.8)',   // Teal
        'rgba(179,157,219,0.8)', // Light Purple
        'rgba(255,110,64,0.8)',   // Deep Orange
        'rgba(66,165,245,0.8)',   // Light Blue
        'rgba(244,67,54,0.8)',    // Bright Red
        'rgba(255,213,79,0.8)',   // Light Amber
        'rgba(102,255,102,0.8)', // Bright Green
        'rgba(0,229,255,0.8)'     // Bright Cyan
    ],

    MONTH_NAMES: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'],

    // Chart container configuration
    CHART_CONFIGS: {
        consumption: {
            id: 'consumption-chart',
            canvasId: 'consumption-chart-canvas',
            downloadId: 'download-csv',
            loadingEmoji: '📊',
            loadingText: 'Crunching the numbers...',
            noDataText: 'No energy data available.<br><br>Energy tracking files will appear here once data collection begins.'
        },
        balance: {
            id: 'balance-chart',
            canvasId: 'balance-chart-canvas',
            downloadId: 'download-balance-csv',
            loadingEmoji: '🔋',
            loadingText: 'Calculating energy flows...',
            noDataText: 'No energy data available.'
        }
    }
};

// ============================================================================
// CHART HELPERS
// ============================================================================
const ChartHelpers = {
    // Chart instances
    consumptionChart: null,
    balanceChart: null,

    // Sparkline configuration
    sparkline: {
        tooltip: null,
        canvas: null,
        ctx: null,
        animationId: null,
        activeNodeType: null
    },

    /**
     * Initialize sparkline elements
     */
    initSparkline() {
        this.sparkline.tooltip = document.getElementById('sparkline-tooltip');
        this.sparkline.canvas = document.getElementById('sparkline-canvas');
        if (this.sparkline.canvas) {
            this.sparkline.ctx = this.sparkline.canvas.getContext('2d');
        }
    },

    /**
     * Draw sparkline on canvas
     */
    drawSparkline(data, color) {
        const { canvas, ctx } = this.sparkline;
        if (!canvas || !ctx) return;

        const width = canvas.width;
        const height = canvas.height;
        const padding = 4;

        ctx.clearRect(0, 0, width, height);

        if (data.length < 2) {
            ctx.fillStyle = '#999';
            ctx.font = '10px sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText('Collecting data...', width / 2, height / 2 + 4);
            return;
        }

        const dataMin = Math.min(...data);
        const dataMax = Math.max(...data);
        const min = dataMin >= 0 ? 0 : dataMin;
        const max = dataMax;
        const range = max - min || 1;

        ctx.beginPath();
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.lineJoin = 'round';

        data.forEach((val, i) => {
            const x = padding + (i / (data.length - 1)) * (width - padding * 2);
            const y = height - padding - ((val - min) / range) * (height - padding * 2);
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });

        ctx.stroke();

        // Show only max label
        ctx.fillStyle = '#666';
        ctx.font = '9px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText(PowerFlowHelpers.formatPower(max), width - 2, 10);
    },

    /**
     * Show sparkline tooltip with real-time updates
     */
    showSparkline(nodeType, element, isLoad, flowHistory, loadHistory, nodeColors) {
        const { tooltip } = this.sparkline;
        if (!tooltip) return;

        this.sparkline.activeNodeType = { nodeType, isLoad, element };

        const rect = element.getBoundingClientRect();

        // Position tooltip above or below the element (using fixed positioning)
        let top = rect.top - 60; // 50px tooltip height + 10px gap
        if (top < 10) {
            top = rect.bottom + 10; // Show below if not enough space above
        }

        tooltip.style.left = (rect.left + rect.width / 2 - 60) + 'px'; // Center horizontally (120px tooltip width / 2)
        tooltip.style.top = top + 'px';
        tooltip.classList.add('visible');

        if (this.sparkline.animationId) cancelAnimationFrame(this.sparkline.animationId);

        const self = this;
        function updateSparklineFrame() {
            if (self.sparkline.activeNodeType) {
                let data, color;
                if (isLoad) {
                    const buffer = loadHistory[nodeType];
                    data = buffer ? buffer.toArray() : [];
                    color = nodeType === 'other' ? '#888' : '#4CAF50';
                } else {
                    data = flowHistory[nodeType].toArray();
                    color = nodeColors[nodeType];
                }
                self.drawSparkline(data, color);
                self.sparkline.animationId = requestAnimationFrame(updateSparklineFrame);
            }
        }
        updateSparklineFrame();
    },

    /**
     * Hide sparkline tooltip
     */
    hideSparkline() {
        if (this.sparkline.tooltip) {
            this.sparkline.tooltip.classList.remove('visible');
        }
        this.sparkline.activeNodeType = null;
        if (this.sparkline.animationId) {
            cancelAnimationFrame(this.sparkline.animationId);
            this.sparkline.animationId = null;
        }
    },

    /**
     * Get channel label
     */
    getChannelLabel(channelIndex, channelData) {
        if (channelIndex === 'Other') return 'Other';
        const channel = channelData.find(ch => ch.index === parseInt(channelIndex));
        return channel ? channel.label : `Channel ${channelIndex}`;
    },

    /**
     * Get channel color
     */
    getChannelColor(channelIndex) {
        if (channelIndex === 'Other') {
            return 'rgba(128,128,128,0.8)';
        }
        const index = parseInt(channelIndex);
        return ChartConfig.COLORS[index % ChartConfig.COLORS.length];
    },

    /**
     * Get display label for a channel or group
     */
    getDisplayLabel(key, channelData) {
        if (key === 'Other') return 'Other (Untracked)';

        if (key.startsWith('group_')) {
            return key.replace('group_', '');
        }

        return this.getChannelLabel(key, channelData);
    },

    /**
     * Get display color for a channel or group
     */
    getDisplayColor(key) {
        if (key === 'Other') return 'rgba(128,128,128,0.8)';

        if (key.startsWith('group_')) {
            const groupLabel = key.replace('group_', '');
            let hash = 0;
            for (let i = 0; i < groupLabel.length; i++) {
                hash = ((hash << 5) - hash) + groupLabel.charCodeAt(i);
                hash |= 0;
            }
            return ChartConfig.COLORS[Math.abs(hash) % ChartConfig.COLORS.length];
        }

        return this.getChannelColor(key);
    },

    /**
     * Format labels based on view type
     */
    formatLabels(periods, viewType) {
        if (viewType === 'daily') {
            return periods.map(hour => `${hour}:00`);
        } else if (viewType === 'monthly') {
            return periods.map(day => day);
        } else if (viewType === 'total') {
            return periods.map(year => year);
        } else {
            return periods.map(month => ChartConfig.MONTH_NAMES[parseInt(month) - 1] || month);
        }
    },

    /**
     * Update consumption chart
     */
    updateConsumptionChart(consumptionData, viewType, exportedData, channelData, currentChartType) {
        const groupedData = DataHelpers.isGroupingEnabled(channelData)
            ? DataHelpers.aggregateByGroup(consumptionData)
            : consumptionData;
        const groupedExportData = DataHelpers.isGroupingEnabled(channelData)
            ? DataHelpers.aggregateByGroup(exportedData)
            : exportedData;

        const periods = Object.keys(groupedData).sort();

        // Get all channels except 'Other'
        const allChannels = new Set();
        periods.forEach(period => {
            Object.keys(groupedData[period]).forEach(channel => {
                if (channel !== 'Other') allChannels.add(channel);
            });
        });

        // Sort: groups first, then individual channels
        const channels = [...allChannels].sort((a, b) => {
            const aIsGroup = a.startsWith('group_');
            const bIsGroup = b.startsWith('group_');
            if (aIsGroup && !bIsGroup) return -1;
            if (!aIsGroup && bIsGroup) return 1;
            if (aIsGroup && bIsGroup) return a.localeCompare(b);
            return parseInt(a) - parseInt(b);
        });

        if (periods.some(period => groupedData[period]['Other'])) {
            channels.push('Other');
        }

        // Create datasets
        const datasets = channels.map((channel) => {
            const label = this.getDisplayLabel(channel, channelData);
            const data = periods.map(period => {
                const value = groupedData[period][channel] || 0;
                return value.toFixed(3);
            });
            const color = this.getDisplayColor(channel);

            return {
                label: label,
                data: data,
                backgroundColor: color,
                borderColor: color.replace('0.8', '1'),
                borderWidth: 1,
                stack: 'energy'
            };
        });

        // Handle exported data
        const hasExportedData = Object.keys(groupedExportData).some(period =>
            Object.keys(groupedExportData[period] || {}).length > 0
        );

        if (hasExportedData) {
            const exportChannels = new Set();
            Object.keys(groupedExportData).forEach(period => {
                Object.keys(groupedExportData[period] || {}).forEach(channel => {
                    exportChannels.add(channel);
                });
            });

            const sortedExportChannels = [...exportChannels].sort((a, b) => {
                const aIsGroup = a.startsWith('group_');
                const bIsGroup = b.startsWith('group_');
                if (aIsGroup && !bIsGroup) return -1;
                if (!aIsGroup && bIsGroup) return 1;
                if (aIsGroup && bIsGroup) return parseInt(a.replace('group_', '')) - parseInt(b.replace('group_', ''));
                return parseInt(a) - parseInt(b);
            });

            sortedExportChannels.forEach(channel => {
                const baseLabel = this.getDisplayLabel(channel, channelData);
                const label = `${baseLabel} (Export)`;
                const data = periods.map(period => {
                    const value = (groupedExportData[period] && groupedExportData[period][channel]) || 0;
                    return (-value).toFixed(3);
                });

                const baseColor = this.getDisplayColor(channel);
                const exportColor = baseColor.replace('0.8', '0.5');

                datasets.push({
                    label: label,
                    data: data,
                    backgroundColor: exportColor,
                    borderColor: baseColor.replace('0.8', '1'),
                    borderWidth: 1,
                    borderDash: [5, 5],
                    stack: 'energy'
                });
            });
        }

        // Destroy existing chart
        if (this.consumptionChart) {
            this.consumptionChart.destroy();
        }

        const canvasElement = document.getElementById('consumption-chart-canvas');
        if (!canvasElement) {
            console.error('Canvas element not found');
            return;
        }

        // Show canvas and hide loading
        const loadingEl = document.querySelector('#consumption-chart .chart-loading');
        if (loadingEl) loadingEl.style.display = 'none';
        canvasElement.style.display = 'block';
        const downloadBtn = document.getElementById('download-csv');
        if (downloadBtn) downloadBtn.style.display = 'block';

        const labels = this.formatLabels(periods, viewType);
        const ctx = canvasElement.getContext('2d');
        const isMobile = window.innerWidth <= 768;
        const xAxisLabels = { 'daily': 'Hour', 'monthly': 'Day', 'yearly': 'Month', 'total': 'Year' };

        this.consumptionChart = new Chart(ctx, {
            type: 'bar',
            data: { labels, datasets },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: {
                        stacked: true,
                        title: { display: true, text: xAxisLabels[viewType] },
                        ticks: { font: { size: isMobile ? 10 : 12 } }
                    },
                    y: {
                        stacked: true,
                        min: 0,
                        title: { display: true, text: 'Energy (kWh)' },
                        ticks: { font: { size: isMobile ? 10 : 12 } }
                    }
                },
                plugins: {
                    legend: {
                        display: true,
                        position: 'top',
                        labels: { font: { size: isMobile ? 10 : 12 } }
                    },
                    tooltip: { mode: 'index', intersect: false }
                }
            }
        });
    },

    /**
     * Update balance chart
     */
    updateBalanceChart(hourlyData, viewType, rawImported, rawExported, hasBalanceCapability) {
        const productionChannels = ChannelCache.production;
        const batteryChannels = ChannelCache.battery;
        const gridChannels = ChannelCache.grid;

        const hasProduction = productionChannels.length > 0;
        const hasBattery = batteryChannels.length > 0;

        document.getElementById('chart-type-selector').style.display = hasBalanceCapability ? 'flex' : 'none';

        if (!hasBalanceCapability) return;

        const gridChannelIndex = gridChannels.length > 0 ? gridChannels[0].index : 0;
        const gridChannelKey = String(gridChannelIndex);
        const productionIndices = productionChannels.map(ch => String(ch.index));
        const batteryIndices = batteryChannels.map(ch => String(ch.index));

        const periods = Object.keys(hourlyData).sort();

        const balanceData = {
            grid: [],
            pv: [],
            battery: [],
            homeConsumption: []
        };

        periods.forEach(period => {
            const periodData = hourlyData[period] || {};
            const periodExport = rawExported[period] || {};

            const gridImport = periodData[gridChannelKey] || 0;
            const gridExport = periodExport[gridChannelKey] || 0;
            balanceData.grid.push(gridImport - gridExport);

            let pvProd = 0;
            productionIndices.forEach(idx => {
                pvProd += periodData[idx] || 0;
            });
            balanceData.pv.push(pvProd);

            let battCharge = 0;
            let battDischarge = 0;
            batteryIndices.forEach(idx => {
                battCharge += periodData[idx] || 0;
                battDischarge += (periodExport[idx] || 0);
            });
            balanceData.battery.push(battDischarge - battCharge);

            const home = gridImport + pvProd + battDischarge - gridExport - battCharge;
            balanceData.homeConsumption.push(Math.max(0, home));
        });

        if (this.balanceChart) {
            this.balanceChart.destroy();
            this.balanceChart = null;
        }

        const loadingEl = document.querySelector('#balance-chart .chart-loading');
        const canvasEl = document.getElementById('balance-chart-canvas');
        if (loadingEl) loadingEl.style.display = 'none';
        canvasEl.style.display = 'block';
        const downloadBtn = document.getElementById('download-balance-csv');
        if (downloadBtn) downloadBtn.style.display = 'block';

        const ctx = canvasEl.getContext('2d');
        const isMobile = window.innerWidth <= 768;
        const labels = this.formatLabels(periods, viewType);

        const datasets = [];

        if (hasProduction) {
            datasets.push({
                label: '☀️ PV',
                data: balanceData.pv,
                backgroundColor: 'rgba(255, 152, 0, 0.7)',
                stack: 'energy'
            });
        }

        datasets.push({
            label: '🔌 Grid',
            data: balanceData.grid,
            backgroundColor: 'rgba(33, 150, 243, 0.7)',
            stack: 'energy'
        });

        if (hasBattery) {
            datasets.push({
                label: '🔋 Battery',
                data: balanceData.battery,
                backgroundColor: 'rgba(156, 39, 176, 0.7)',
                stack: 'energy'
            });
        }

        datasets.push({
            label: '🏠 Home',
            data: balanceData.homeConsumption,
            type: 'line',
            borderColor: '#4CAF50',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            fill: false,
            tension: 0.3
        });

        this.balanceChart = new Chart(ctx, {
            type: 'bar',
            data: { labels, datasets },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                aspectRatio: isMobile ? 1.2 : 2,
                scales: {
                    x: {
                        stacked: true,
                        title: {
                            display: true,
                            text: viewType === 'daily' ? 'Hour' : viewType === 'monthly' ? 'Day' : viewType === 'total' ? 'Year' : 'Month'
                        },
                        ticks: { font: { size: isMobile ? 10 : 12 } }
                    },
                    y: {
                        stacked: true,
                        title: { display: true, text: 'Energy (kWh)' },
                        ticks: { font: { size: isMobile ? 10 : 12 } }
                    }
                },
                plugins: {
                    legend: {
                        position: isMobile ? 'bottom' : 'top',
                        labels: { font: { size: isMobile ? 10 : 12 } }
                    },
                    tooltip: {
                        callbacks: {
                            label: function (context) {
                                const value = context.parsed.y;
                                const sign = value >= 0 ? '+' : '';
                                return `${context.dataset.label}: ${sign}${value.toFixed(2)} kWh`;
                            }
                        }
                    }
                }
            }
        });

        return balanceData;
    },

    /**
     * Update energy KPIs
     */
    updateEnergyKPIs(rawImported, rawExported) {
        const productionChannels = ChannelCache.production;
        const batteryChannels = ChannelCache.battery;
        const gridChannels = ChannelCache.grid;
        const gridChannelIndex = gridChannels.length > 0 ? gridChannels[0].index : 0;

        const hasProduction = productionChannels.length > 0;
        const hasBattery = batteryChannels.length > 0;

        const kpisSection = document.getElementById('energy-kpis-section');
        if (!hasProduction && !hasBattery) {
            kpisSection.style.display = 'none';
            return;
        }
        kpisSection.style.display = 'flex';

        const productionIndices = productionChannels.map(ch => ch.index);
        const batteryIndices = batteryChannels.map(ch => ch.index);

        let gridImport = rawImported[gridChannelIndex] || 0;
        let gridExport = rawExported[gridChannelIndex] || 0;
        let production = 0;
        let batteryCharge = 0;
        let batteryDischarge = 0;

        productionIndices.forEach(idx => {
            production += rawImported[idx] || 0;
        });

        batteryIndices.forEach(idx => {
            batteryCharge += rawImported[idx] || 0;
            batteryDischarge += rawExported[idx] || 0;
        });

        document.getElementById('kpi-grid-import-value').textContent = gridImport.toFixed(1);
        document.getElementById('kpi-grid-export-value').textContent = gridExport.toFixed(1);
        document.getElementById('kpi-grid-export').style.display = gridExport > 0 ? 'block' : 'none';

        if (hasProduction) {
            document.getElementById('kpi-production').style.display = 'block';
            document.getElementById('kpi-production-value').textContent = production.toFixed(1);

            if (production > 0) {
                const selfConsumed = Math.max(0, production - gridExport);
                const selfConsumption = Math.min(100, (selfConsumed / production) * 100);
                document.getElementById('kpi-self-consumption').style.display = 'block';
                document.getElementById('kpi-self-consumption-value').textContent = selfConsumption.toFixed(0) + '%';
            } else {
                document.getElementById('kpi-self-consumption').style.display = 'none';
            }

            const totalConsumption = gridImport + production - gridExport + batteryDischarge - batteryCharge;
            if (totalConsumption > 0) {
                const fromOwnProduction = Math.max(0, totalConsumption - gridImport);
                const autosufficiency = Math.min(100, (fromOwnProduction / totalConsumption) * 100);
                document.getElementById('kpi-autosufficiency').style.display = 'block';
                document.getElementById('kpi-autosufficiency-value').textContent = autosufficiency.toFixed(0) + '%';
            } else {
                document.getElementById('kpi-autosufficiency').style.display = 'none';
            }
        } else {
            document.getElementById('kpi-production').style.display = 'none';
            document.getElementById('kpi-self-consumption').style.display = 'none';
            document.getElementById('kpi-autosufficiency').style.display = 'none';
        }

        if (hasBattery) {
            document.getElementById('kpi-battery-charge').style.display = 'block';
            document.getElementById('kpi-battery-charge-value').textContent = batteryCharge.toFixed(1);
            document.getElementById('kpi-battery-discharge').style.display = 'block';
            document.getElementById('kpi-battery-discharge-value').textContent = batteryDischarge.toFixed(1);
        } else {
            document.getElementById('kpi-battery-charge').style.display = 'none';
            document.getElementById('kpi-battery-discharge').style.display = 'none';
        }
    },

    /**
     * Generate chart HTML from config
     */
    generateChartHtml(config) {
        return `
            <div class="chart-loading">${config.loadingEmoji} ${config.loadingText}</div>
            <canvas id="${config.canvasId}" style="display: none;"></canvas>
            <button id="${config.downloadId}" class="tooltip" style="position: absolute; top: 2px; right: 2px; display: none;">
                <i class="fas fa-download"></i>
            </button>
        `;
    },

    /**
     * Set message in chart containers
     */
    setChartMessage(emoji, message, color = '#666') {
        Object.values(ChartConfig.CHART_CONFIGS).forEach(config => {
            const el = document.getElementById(config.id);
            if (el) {
                el.innerHTML = `<div style="text-align: center; padding: 40px; font-size: 18px; color: ${color};">${emoji} ${message}</div>`;
            }
        });
    },

    showLoadingMessage() {
        this.setChartMessage('📊', 'Loading energy data...');
    },

    showNoDataMessage() {
        Object.values(ChartConfig.CHART_CONFIGS).forEach(config => {
            const el = document.getElementById(config.id);
            if (el) {
                el.innerHTML = `<div style="text-align: center; padding: 40px; font-size: 18px; color: #666;">${config.loadingEmoji} ${config.noDataText}</div>`;
            }
        });
    },

    showErrorMessage(message) {
        this.setChartMessage('❌', message, '#d32f2f');
    },

    restoreChartCanvases(currentChartType) {
        Object.entries(ChartConfig.CHART_CONFIGS).forEach(([type, config]) => {
            const chartDiv = document.getElementById(config.id);
            if (chartDiv) {
                chartDiv.innerHTML = this.generateChartHtml(config);
                chartDiv.style.display = currentChartType === type ? 'block' : 'none';
            }
        });
    }
};

// Export to global scope
window.ChartConfig = ChartConfig;
window.ChartHelpers = ChartHelpers;
