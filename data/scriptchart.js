// =============================================================================
// Smoker Controller - Chart JavaScript (Chart.js v4)
// =============================================================================

// =============================================================================
// CONFIGURATION
// =============================================================================
const SETPOINT_MIN = 160;
const SETPOINT_MAX = 450;
const INITIAL_RECONNECT_DELAY_MS = 1000;
const MAX_RECONNECT_DELAY_MS = 30000;
const MAX_DATA_POINTS = 1200;

// =============================================================================
// STATE
// =============================================================================
let websocket = null;
let reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
let reconnectTimeout = null;
let chartReady = false;
let pendingHistory = null;
let pendingState = null;

// =============================================================================
// CHART DATA
// =============================================================================
let chartLabels = [];
let meatData = [];
let pitData = [];
let setpointData = [];
let fanSpeedData = [];
let dataPointCount = 0;
let lastSmokerOffset = 0;
let lastGoodSetpoint = null;
let lastGoodFan = null;

function formatTimeWithOffset(utcSeconds, offsetSeconds) {
    const utc = Number(utcSeconds);

    // Set start time if not set
    if (startTime === null || utc < startTime) {
        startTime = utc;
    }

    if (isAPMode) {
        // COOK DURATION MODE
        let diff = utc - startTime;
        if (diff < 0) diff = 0;

        const hrs = Math.floor(diff / 3600);
        const mins = Math.floor((diff % 3600) / 60);
        const secs = diff % 60;

        return `${String(hrs).padStart(2, '0')}:${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
    }

    if (offsetSeconds !== undefined && offsetSeconds !== null) {
        lastSmokerOffset = Number(offsetSeconds);
    }

    const off = Number(lastSmokerOffset);

    // Calculate local time by adding offset to UTC seconds
    const date = new Date((utc + off) * 1000);

    let hours = date.getUTCHours();
    const ampm = hours >= 12 ? 'PM' : 'AM';
    hours = hours % 12;
    hours = hours ? hours : 12;
    const minutes = String(date.getUTCMinutes()).padStart(2, '0');

    // Debug only occasionally or on change to avoid flooding
    // console.log(`[Time] UTC:${utc} Offset:${off} -> ${hours}:${minutes} ${ampm}`);

    return `${hours}:${minutes} ${ampm}`;
}

// =============================================================================
// INITIALIZATION
// =============================================================================
window.addEventListener('load', onLoad);
window.addEventListener('beforeunload', onUnload);
window.addEventListener('pagehide', onUnload);

function onUnload() {
    // Close WebSocket cleanly when navigating away
    if (websocket) {
        console.log('[WebSocket] Closing connection on page unload');
        websocket.onclose = null; // Prevent reconnect
        websocket.close();
        websocket = null;
    }
    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
        reconnectTimeout = null;
    }
}

function onLoad() {
    console.log('[Chart] Initializing Chart Page...');
    initMenuDropdown();

    waitForChartJs();
    initWebSocket();
}

function waitForChartJs(attempt = 0) {
    const liveEl = document.getElementById('liveReadings');
    if (typeof Chart !== 'undefined') {
        initChart();
        if (liveEl) liveEl.innerHTML = 'Waiting for data...';
        return;
    }

    if (attempt >= 20) {
        console.error('[Chart] Chart.js failed to load');
        if (liveEl) liveEl.innerHTML = 'Chart library failed to load. Please refresh the page.';
        const loadingEl = document.getElementById('chartLoading');
        if (loadingEl) loadingEl.innerHTML = '<div style="text-align: center;"><div style="margin-bottom: 10px;">❌</div><div>Failed to load chart</div></div>';
        return;
    }

    setTimeout(() => waitForChartJs(attempt + 1), 150);
}

// =============================================================================
// WIFI STATUS UPDATE
// =============================================================================
function updateWiFiDisplay(data) {
    const indicator = document.getElementById('wifi-indicator');
    const modeEl = document.getElementById('wifi-mode');
    const ssidEl = document.getElementById('wifi-ssid');
    const signalEl = document.getElementById('wifi-signal');

    if (!indicator || !modeEl || !ssidEl || !signalEl) return;

    // Update mode and indicator color
    if (data.isAP) {
        modeEl.textContent = 'AP MODE';
        indicator.style.background = '#fbbf24'; // Yellow for AP
        indicator.style.boxShadow = '0 0 6px rgba(251,191,36,0.6)';
        ssidEl.textContent = 'Access Point Active';
        signalEl.textContent = 'n/a';
    } else {
        modeEl.textContent = 'STA MODE';
        indicator.style.background = '#10b981'; // Green for STA
        indicator.style.boxShadow = '0 0 6px rgba(16,185,129,0.6)';
        
        // SSID and signal would come from separate API call if available
        // For now, just show STA mode is active
        ssidEl.textContent = data.ssid || 'Connected';
        signalEl.textContent = data.rssi ? `${data.rssi} dBm` : '--';
    }
}

// =============================================================================
// MENU DROPDOWN
// =============================================================================
function initMenuDropdown() {
    const menu = document.querySelector('.menu');
    if (!menu) return;

    // Create backdrop element
    const backdrop = document.createElement('div');
    backdrop.className = 'menu-backdrop';
    document.body.appendChild(backdrop);

    // Toggle dropdown on click
    menu.addEventListener('click', function (e) {
        e.stopPropagation();
        const isOpen = this.classList.toggle('menu-open');
        backdrop.classList.toggle('active', isOpen);
    });

    // Close dropdown when clicking backdrop
    backdrop.addEventListener('click', function () {
        menu.classList.remove('menu-open');
        backdrop.classList.remove('active');
    });

    // Close dropdown when clicking outside
    document.addEventListener('click', function (e) {
        if (!menu.contains(e.target)) {
            menu.classList.remove('menu-open');
            backdrop.classList.remove('active');
        }
    });

    // Prevent dropdown from closing when clicking inside it
    const dropdown = menu.querySelector('.menu-dropdown');
    if (dropdown) {
        dropdown.addEventListener('click', function (e) {
            e.stopPropagation();
        });
    }

    // Close on escape key
    document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') {
            menu.classList.remove('menu-open');
            backdrop.classList.remove('active');
        }
    });
}

// =============================================================================
// WEBSOCKET FUNCTIONS
// =============================================================================
function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const gateway = `${protocol}//${window.location.hostname}/ws`;
    console.log(`[WebSocket] Connecting to ${gateway}...`);

    try {
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onerror = onError;
        websocket.onmessage = onMessage;
    } catch (error) {
        console.error('[WebSocket] Connection failed:', error);
        scheduleReconnect();
    }
}

function onOpen(event) {
    console.log('[WebSocket] Connected successfully');
    reconnectDelay = INITIAL_RECONNECT_DELAY_MS; // Reset backoff

    // Request full history first
    getHistory();
    // Then request current values
    getValues();
}

function onClose(event) {
    console.log('[WebSocket] Disconnected');
    scheduleReconnect();
}

function onError(event) {
    console.error('[WebSocket] Error occurred');
}

function scheduleReconnect() {
    // Clear any existing reconnect timeout
    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
    }

    console.log(`[WebSocket] Reconnecting in ${reconnectDelay}ms...`);

    reconnectTimeout = setTimeout(() => {
        initWebSocket();

        // Exponential backoff with max limit
        reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY_MS);
    }, reconnectDelay);
}

function getValues() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("getValues");
        console.log('[WebSocket] Requested current values');
    }
}

function getHistory() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("getHistory");
        console.log('[WebSocket] Requested temperature history');
    }
}

// =============================================================================
// MESSAGE HANDLING
// =============================================================================
// Track last timezone to detect changes
let lastReceivedTimezone = '';
let isAPMode = false;
let startTime = null;

function onMessage(event) {
    try {
        const data = JSON.parse(event.data);
        // console.log('[Chart] Received:', data.type || 'state');

        if (data.type === 'history') {
            // Reset start time if history loaded (for duration calc)
            if (data.data && data.data.length > 0) {
                startTime = data.data[0].t;
            }
            if (Array.isArray(data.data)) {
                if (!chartReady || !fanChart) {
                    pendingHistory = { data: data.data, offset: data.offset };
                    return;
                }
                loadHistory(data.data, data.offset);
            }
        } else {
            // It's a state packet (contains boxValues and timezone)

            // Check AP Mode status
            if (data.isAP !== undefined) {
                const prevAP = isAPMode;
                isAPMode = data.isAP;
                updateWiFiDisplay(data);

                // If AP mode changed, reload history to fix X-axis labels (Time <-> Duration)
                if (prevAP !== isAPMode) {
                    console.log('[Chart] AP Mode changed to:', isAPMode);
                    // Update title immediately
                    updateChartTitle(isAPMode ? null : data.timezone);
                    getHistory();
                }
            }

            // Handle Timezone
            if (data.timezone) {
                // Only act if timezone actually changed (and isn't the first empty check)
                if (lastReceivedTimezone && data.timezone !== lastReceivedTimezone) {
                    console.log('[Chart] Timezone changed to:', data.timezone);
                    if (!isAPMode) {
                        updateChartTitle(data.timezone);
                        getHistory();
                    }
                } else if (!lastReceivedTimezone) {
                    // First time receiving timezone
                    lastReceivedTimezone = data.timezone;
                    if (!isAPMode) updateChartTitle(data.timezone);
                }
                lastReceivedTimezone = data.timezone;
            }

            // Ensure title is correct (redundant check)
            if (isAPMode) {
                updateChartTitle(null);
            }

            // Only process data points when temperature data is present
            if (data.boxValue0 !== undefined || data.boxValue1 !== undefined) {
                if (!chartReady || !fanChart) {
                    pendingState = data;
                    return;
                }
                addDataPoint(data);
            }
        }

        // Update chart display
        if (fanChart) fanChart.update();
    } catch (error) {
        console.error('[Chart] Failed to parse message:', error);
    }
}

function loadHistory(historyPoints, offset) {
    console.log(`[Chart] Loading ${historyPoints.length} history points...`);

    // Clear current chart data
    chartLabels = [];
    meatData = [];
    pitData = [];
    setpointData = [];
    fanSpeedData = [];
    dataPointCount = 0;

    // Process points
    historyPoints.forEach(p => {
        // Format timestamp using smoker's offset
        const timeLabel = formatTimeWithOffset(p.t, offset);

        chartLabels.push(timeLabel);
        meatData.push(p.m || 0);
        pitData.push(p.p || 0);
        setpointData.push(p.s || 0);
        fanSpeedData.push(p.f || 0);
        dataPointCount++;
    });

    // Update chart references (since we reassigned the arrays)
    fanChart.data.labels = chartLabels;
    fanChart.data.datasets[0].data = meatData;
    fanChart.data.datasets[1].data = pitData;
    fanChart.data.datasets[2].data = setpointData;
    fanChart.data.datasets[3].data = fanSpeedData;

    fanChart.data.datasets[3].data = fanSpeedData;

    // Update live readings with the most recent data point
    if (meatData.length > 0) {
        updateLiveReadings(
            meatData[meatData.length - 1],
            pitData[pitData.length - 1],
            setpointData[setpointData.length - 1],
            fanSpeedData[fanSpeedData.length - 1]
        );
    }

    console.log('[Chart] History loaded successfully');
}

function addDataPoint(data) {
    const meatTemp = parseFloat(data.boxValue0);
    const pitTemp = parseFloat(data.boxValue1);
    if (!Number.isFinite(meatTemp) || !Number.isFinite(pitTemp)) {
        return;
    }

    dataPointCount++;

    // Add timestamp label using smoker's offset (data.o)
    const timeLabel = formatTimeWithOffset(data.t || (Date.now() / 1000), data.o);
    chartLabels.push(timeLabel);

    // Parse values (handle "No Probe" text)
    const setpointParsed = parseFloat(data.boxValue2);
    const fanParsed = parseFloat(data.boxValue3);
    if (Number.isFinite(setpointParsed)) {
        lastGoodSetpoint = setpointParsed;
    }
    if (Number.isFinite(fanParsed)) {
        lastGoodFan = fanParsed;
    }
    const setpoint = Number.isFinite(lastGoodSetpoint) ? lastGoodSetpoint : 0;
    const fanSpeed = Number.isFinite(lastGoodFan) ? lastGoodFan : 0;

    meatData.push(meatTemp);
    pitData.push(pitTemp);
    setpointData.push(setpoint);
    fanSpeedData.push(fanSpeed);

    console.log(`[Chart] Added data point ${dataPointCount}: Meat=${meatTemp}, Pit=${pitTemp}, Setpoint=${setpoint}, Fan=${fanSpeed}`);

    // Limit data points to prevent memory issues
    if (chartLabels.length > MAX_DATA_POINTS) {
        chartLabels.shift();
        meatData.shift();
        pitData.shift();
        setpointData.shift();
        fanSpeedData.shift();
    }

    updateLiveReadings(meatTemp, pitTemp, setpoint, fanSpeed);
}

function updateLiveReadings(meat, pit, setpoint, fan) {
    const el = document.getElementById('liveReadings');
    if (el) {
        // Professional plain text look: Label (Gray) Value (Color) in one line
        const sep = '&nbsp;&nbsp;&nbsp;&nbsp;'; // Wide spacing
        el.innerHTML =
            `Meat Temp: <span style="color:#f97316; font-weight:bold">${meat.toFixed(1)}°F</span>${sep}` +
            `Pit Temp: <span style="color:#3b82f6; font-weight:bold">${pit.toFixed(1)}°F</span>${sep}` +
            `Setpoint: <span style="color:#10b981; font-weight:bold">${setpoint}°F</span>${sep}` +
            `Fan: <span style="color:#94a3b8; font-weight:bold">${fan}%</span>`;
    }
}

function updateChartTitle(timezoneStr) {
    if (!fanChart) return;

    if (isAPMode) {
        fanChart.options.scales.x.title.text = "Cook Duration";
        fanChart.update('none');
        return;
    }

    if (!timezoneStr) return;

    // Extract abbreviation (e.g. "CST" from "CST6CDT...")
    // Simple regex to grab the first word characters
    const match = timezoneStr.match(/^([A-Z]+)/);
    const abbr = match ? match[1] : '';

    if (abbr) {
        fanChart.options.scales.x.title.text = `Time (${abbr})`;
        fanChart.update('none'); // Update without animation
    }
}

// =============================================================================
// CHART SETUP (Chart.js v4)
// =============================================================================
let fanChart = null;

function initChart() {
    const ctx = document.getElementById('fanChart').getContext('2d');

    fanChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: chartLabels,
            datasets: [
                {
                    label: 'Meat Temp',
                    data: meatData,
                    borderColor: '#f97316',
                    backgroundColor: 'rgba(249, 115, 22, 0.1)',
                    fill: true,
                    tension: 0.4
                },
                {
                    label: 'Pit Temp',
                    data: pitData,
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    fill: true,
                    tension: 0.4
                },
                {
                    label: 'Setpoint',
                    data: setpointData,
                    borderColor: '#10b981',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    fill: false,
                    borderDash: [5, 5],
                    tension: 0.4
                },
                {
                    label: 'Fan Speed',
                    data: fanSpeedData,
                    borderColor: '#94a3b8',
                    backgroundColor: 'rgba(148, 163, 184, 0.1)',
                    fill: false,
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: 'index',
                intersect: false
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    labels: {
                        color: '#f1f5f9',
                        font: {
                            size: 12
                        },
                        padding: 15
                    }
                },
                tooltip: {
                    backgroundColor: 'rgba(30, 41, 59, 0.9)',
                    titleColor: '#f1f5f9',
                    bodyColor: '#f1f5f9',
                    borderColor: '#334155',
                    borderWidth: 1,
                    padding: 12,
                    displayColors: true
                }
            },
            scales: {
                x: {
                    grid: {
                        color: '#334155',
                        drawBorder: false
                    },
                    ticks: {
                        color: '#94a3b8',
                        maxRotation: 45,
                        minRotation: 0
                    },
                    title: {
                        display: true,
                        text: 'Time',
                        color: '#94a3b8',
                        font: {
                            size: 13,
                            weight: '600'
                        }
                    }
                },
                y: {
                    beginAtZero: false,
                    suggestedMin: 0,
                    suggestedMax: 500,
                    grid: {
                        color: '#334155',
                        drawBorder: false
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    title: {
                        display: true,
                        text: 'Temperature (°F) / Fan (%)',
                        color: '#94a3b8',
                        font: {
                            size: 13,
                            weight: '600'
                        }
                    }
                }
            },
            animation: {
                duration: 300
            }
        }
    });

    console.log('[Chart] Chart initialized successfully');
    chartReady = true;
    if (pendingHistory) {
        loadHistory(pendingHistory.data, pendingHistory.offset);
        pendingHistory = null;
    }
    if (pendingState) {
        addDataPoint(pendingState);
        pendingState = null;
    }
    
    // Hide loading indicator and show chart
    const loadingEl = document.getElementById('chartLoading');
    const chartEl = document.getElementById('fanChart');
    if (loadingEl) loadingEl.style.display = 'none';
    if (chartEl) chartEl.style.display = 'block';
}
