// =============================================================================
// Smoker Controller - Main JavaScript (Modernized)
// =============================================================================

// =============================================================================
// CONFIGURATION
// =============================================================================
const SETPOINT_MIN = 160;
const SETPOINT_MAX = 450;
const INITIAL_RECONNECT_DELAY_MS = 1000;
const MAX_RECONNECT_DELAY_MS = 30000;

// =============================================================================
// STATE
// =============================================================================
let websocket = null;
let lastSetpoint = "";
let reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
let reconnectTimeout = null;
let selectedTimezone = "";
const lastUserInteractions = {}; // Tracks timestamp of last user interaction per input ID
const dirtyInputs = new Set();
let lastKeepWarmEnabled = false;
let lastMeatDoneSetpoint = null;
let lastMeatTempValue = null;
let lastDoneAlarmEnabled = false;
let doneAlarmTriggered = false;
let otaReconnectBlocked = false;
let hasReceivedValues = false;
let valuesRetryTimer = null;
let otaInfoTimer = null;
let fanAuto = true;

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
    console.log('[App] Initializing Smoker Controller...');
    initMenuDropdown();
    loadCachedState(); // Show last known values immediately
    initWebSocket();
    initInputTracking();
    initFanModeButton();
}

// Load cached state from localStorage for instant display
function loadCachedState() {
    try {
        const cached = localStorage.getItem('smokerLastState');
        if (cached) {
            const state = JSON.parse(cached);
            console.log('[App] Loading cached state:', state);
            
            // Update display with cached values
            if (state.meatTemp !== undefined) {
                updateDisplayValue('boxValue0', 'box0', state.meatTemp);
            }
            if (state.pitTemp !== undefined) {
                updateDisplayValue('boxValue1', 'box1', state.pitTemp);
            }
            if (state.fanSpeed !== undefined) {
                updateDisplayValue('boxValue3', 'box3', state.fanSpeed);
            }
            if (state.setpoint !== undefined) {
                const lbl = document.getElementById('lblSetpoint');
                if (lbl) lbl.textContent = `(${state.setpoint})`;
            }
        }
    } catch (e) {
        console.log('[App] No cached state available');
    }
}

// Reload page when user clicks OK on reboot modal
function reloadPage() {
    window.location.reload();
}

// Show OTA reboot modal
function showOtaRebootModal(newVersion) {
    const modal = document.getElementById('otaRebootModal');
    const versionEl = document.getElementById('otaNewVersion');
    if (modal) {
        if (versionEl && newVersion) {
            versionEl.textContent = `New Version: ${newVersion}`;
        }
        modal.style.display = 'flex';
    }
}

function initInputTracking() {
    const trackedInputs = ['pitOffsetInput', 'meatOffsetInput', 'box2', 'box8', 'box9', 'kpInput', 'kiInput', 'kdInput'];
    trackedInputs.forEach(id => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('input', () => markInputDirty(id));
            el.addEventListener('focus', () => recordInteraction(id));
        }
    });
}

function recordInteraction(id) {
    lastUserInteractions[id] = Date.now();
}

function markInputDirty(id) {
    dirtyInputs.add(id);
    recordInteraction(id);
}

function normalizeComparableValue(value) {
    if (value === undefined || value === null) {
        return '';
    }

    const text = String(value).trim();
    if (text === '') {
        return '';
    }

    const numericValue = Number(text);
    if (!Number.isNaN(numericValue)) {
        return String(numericValue);
    }

    return text;
}

function valuesMatchForInput(element, incomingValue) {
    return normalizeComparableValue(element.value) === normalizeComparableValue(incomingValue);
}

function isRecentlyInteracted(id) {
    const lastTime = lastUserInteractions[id] || 0;
    return (Date.now() - lastTime) < 3000; // 3 second grace period
}

function shouldApplyIncomingValue(inputId, element, incomingValue) {
    if (!element) {
        return false;
    }

    const matchesIncomingValue = valuesMatchForInput(element, incomingValue);

    if (matchesIncomingValue) {
        dirtyInputs.delete(inputId);
        return false;
    }

    if (dirtyInputs.has(inputId)) {
        return false;
    }

    return !isRecentlyInteracted(inputId) && document.activeElement !== element;
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
// FAN MODE TOGGLE
// =============================================================================
function initFanModeButton() {
    const btn = document.getElementById('fanModeBtn');
    if (!btn) return;
    btn.addEventListener('click', toggleFanMode);
    updateFanModeButton(fanAuto);
}

function toggleFanMode() {
    if (!websocket || websocket.readyState !== WebSocket.OPEN) {
        console.warn('[WebSocket] Not connected, cannot toggle fan mode');
        return;
    }
    const nextAuto = !fanAuto;
    const mode = nextAuto ? 'auto' : 'manual';
    websocket.send(`FanMode:${mode}`);
    console.log(`[WebSocket] Sent fan mode: ${mode}`);
    updateFanModeButton(nextAuto);
}

function updateFanModeButton(isAuto) {
    fanAuto = !!isAuto;
    const btn = document.getElementById('fanModeBtn');
    if (!btn) return;
    btn.classList.toggle('fan-mode-btn--off', !fanAuto);
    btn.textContent = fanAuto ? 'AUTO' : 'OFF';
    btn.setAttribute('aria-pressed', (!fanAuto).toString());
    btn.title = fanAuto ? 'Click to turn fan OFF' : 'Click to return to AUTO';
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
    getValues();
    requestOTAInfo();
    startValuesRetry();
}

function onClose(event) {
    console.log('[WebSocket] Disconnected');
    if (otaReconnectBlocked) {
        setTimeout(() => {
            otaReconnectBlocked = false;
            initWebSocket();
        }, 8000);
        return;
    }
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

// =============================================================================
// OTA UPDATE HANDLING
// =============================================================================
function requestOTAInfo() {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send('getOTAInfo');
    }
}

function startOtaInfoPolling() {
    if (otaInfoTimer) return;
    otaInfoTimer = setInterval(() => {
        requestOTAInfo();
    }, 1000);
}

function stopOtaInfoPolling() {
    if (!otaInfoTimer) return;
    clearInterval(otaInfoTimer);
    otaInfoTimer = null;
}

function checkForOTAUpdates() {
    const checkBtn = document.getElementById('otaCheckBtn');
    const updateBtn = document.getElementById('otaUpdateBtn');
    const status = document.getElementById('otaStatus');
    const bar = document.getElementById('otaProgressBar');
    const progress = document.getElementById('otaProgress');
    if (checkBtn) checkBtn.disabled = true;
    if (updateBtn) updateBtn.disabled = true;
    if (status) status.textContent = 'Checking for updates...';
    if (bar) bar.style.display = 'block';
    if (progress) progress.style.width = '0%';
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send('checkOTAUpdates');
    }
    startOtaInfoPolling();
}

function startOTAUpdate() {
    const checkBtn = document.getElementById('otaCheckBtn');
    const updateBtn = document.getElementById('otaUpdateBtn');
    const status = document.getElementById('otaStatus');
    const bar = document.getElementById('otaProgressBar');
    const progress = document.getElementById('otaProgress');
    if (checkBtn) checkBtn.disabled = true;
    if (updateBtn) updateBtn.disabled = true;
    if (status) status.textContent = 'Starting update...';
    if (bar) bar.style.display = 'block';
    if (progress) progress.style.width = '0%';
    otaReconnectBlocked = true;
    setTimeout(() => {
        otaReconnectBlocked = false;
    }, 90000);
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send('startOTAUpdate');
    }
    startOtaInfoPolling();
}

function handleOTAStatus(data) {
    const status = document.getElementById('otaStatus');
    const bar = document.getElementById('otaProgressBar');
    const progress = document.getElementById('otaProgress');
    const progressDetails = document.getElementById('otaProgressDetails');
    const checkBtn = document.getElementById('otaCheckBtn');
    const updateBtn = document.getElementById('otaUpdateBtn');
    const currentVer = document.getElementById('otaCurrentVersion');
    const availableVer = document.getElementById('otaAvailableVersion');
    const littlefsProgress = document.getElementById('otaLittlefsProgress');
    const firmwareProgress = document.getElementById('otaFirmwareProgress');

    if (currentVer && data.otaCurrentVersion) {
        currentVer.textContent = data.otaCurrentVersion;
    }
    if (availableVer && data.otaAvailableVersion) {
        availableVer.textContent = data.otaAvailableVersion;
    }

    if (littlefsProgress && data.otaLittlefsProgress !== undefined) {
        littlefsProgress.textContent = `${data.otaLittlefsProgress}%`;
    }

    if (firmwareProgress && data.otaFirmwareProgress !== undefined) {
        firmwareProgress.textContent = `${data.otaFirmwareProgress}%`;
    }

    if (!status || !bar || !progress || !checkBtn || !updateBtn) return;

    if (data.otaStatus === 'idle') {
        status.textContent = 'Ready to check for updates.';
        bar.style.display = 'none';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '0%';
        checkBtn.disabled = false;
        updateBtn.disabled = true;
        otaReconnectBlocked = false;
        stopOtaInfoPolling();
    } else if (data.otaStatus === 'checking') {
        status.textContent = 'Checking for updates...';
        bar.style.display = 'block';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '0%';
        checkBtn.disabled = true;
        updateBtn.disabled = true;
        startOtaInfoPolling();
    } else if (data.otaStatus === 'update_available') {
        status.textContent = 'Update available. Ready to install.';
        bar.style.display = 'none';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '0%';
        checkBtn.disabled = false;
        updateBtn.disabled = false;
        otaReconnectBlocked = false;
        stopOtaInfoPolling();
    } else if (data.otaStatus === 'no_update') {
        status.textContent = 'No update available. You are up to date.';
        if (availableVer && !data.otaAvailableVersion) {
            availableVer.textContent = 'Up to date';
        }
        bar.style.display = 'none';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '0%';
        checkBtn.disabled = false;
        updateBtn.disabled = true;
        otaReconnectBlocked = false;
        stopOtaInfoPolling();
    } else if (data.otaStatus === 'downloading') {
        if (progressDetails) progressDetails.style.display = 'flex';
        if (data.otaPhase === 'littlefs') {
            status.textContent = `Downloading firmware... (${data.otaLittlefsProgress || 0}%)`;
            progress.style.width = `${data.otaLittlefsProgress || 0}%`;
        } else if (data.otaPhase === 'firmware') {
            status.textContent = `Downloading firmware... (${data.otaFirmwareProgress || 0}%)`;
            progress.style.width = `${data.otaFirmwareProgress || 0}%`;
        } else {
            status.textContent = `Downloading update... (${data.otaProgress || 0}%)`;
            progress.style.width = `${data.otaProgress || 0}%`;
        }
        bar.style.display = 'block';
        checkBtn.disabled = true;
        updateBtn.disabled = true;
        startOtaInfoPolling();
    } else if (data.otaStatus === 'installing') {
        if (progressDetails) progressDetails.style.display = 'flex';
        if (data.otaPhase === 'littlefs') {
            status.textContent = `Installing firmware... (${data.otaLittlefsProgress || 0}%)`;
            progress.style.width = `${data.otaLittlefsProgress || 0}%`;
        } else if (data.otaPhase === 'firmware') {
            status.textContent = `Installing firmware... (${data.otaFirmwareProgress || 0}%)`;
            progress.style.width = `${data.otaFirmwareProgress || 0}%`;
        } else {
            status.textContent = `Installing update... (${data.otaProgress || 0}%)`;
            progress.style.width = `${data.otaProgress || 0}%`;
        }
        bar.style.display = 'block';
        checkBtn.disabled = true;
        updateBtn.disabled = true;
        startOtaInfoPolling();
    } else if (data.otaStatus === 'success') {
        status.textContent = 'Update successful! Rebooting...';
        bar.style.display = 'block';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '100%';
        checkBtn.disabled = false;
        updateBtn.disabled = true;
        otaReconnectBlocked = false;
        stopOtaInfoPolling();
        // Show reboot modal
        showOtaRebootModal(data.otaAvailableVersion || data.otaCurrentVersion);
    } else if (data.otaStatus === 'failed') {
        status.textContent = `Update failed: ${data.otaError || 'Unknown error'}`;
        bar.style.display = 'block';
        if (progressDetails) progressDetails.style.display = 'none';
        progress.style.width = '0%';
        checkBtn.disabled = false;
        updateBtn.disabled = true;
        otaReconnectBlocked = false;
        stopOtaInfoPolling();
    }
}

// =============================================================================
// MESSAGE HANDLING
// =============================================================================
function onMessage(event) {
    try {
        const data = JSON.parse(event.data);
        console.log('[WebSocket] Received data:', data);

        markValuesReceived(data);

        // Ignore history data intended for the graph
        if (data.type === 'history') {
            return;
        }

        // Cache state for PWA offline mode
        try {
            const cachedState = {
                meatTemp: data.boxValue0,
                pitTemp: data.boxValue1,
                setpoint: data.boxValue2,
                fanSpeed: data.boxValue3,
                timestamp: Date.now()
            };
            localStorage.setItem('smokerLastState', JSON.stringify(cachedState));
        } catch (e) {
            console.log('[PWA] localStorage cache failed:', e);
        }

        // Update meat temperature (box 0)
        if (data.boxValue0 !== undefined) {
            updateDisplayValue('boxValue0', 'box0', data.boxValue0);
        }

        // Update pit temperature (box 1)
        if (data.boxValue1 !== undefined) updateDisplayValue('boxValue1', 'box1', data.boxValue1);

        // Update fan speed (box 3)
        if (data.boxValue3 !== undefined) updateDisplayValue('boxValue3', 'box3', data.boxValue3);

        if (data.fanAuto !== undefined) {
            updateFanModeButton(data.fanAuto === true || data.fanAuto === 'true');
        }

        // Update active setpoint label confirmation
        const lbl = document.getElementById('lblSetpoint');
        if (lbl && data.boxValue2 !== undefined) {
            lbl.textContent = `(${data.boxValue2})`;
        }

        // Update meat done label if done alarm is enabled
        const meatDoneLbl = document.getElementById('lblMeatDone');
        if (data.boxValue6 !== undefined) {
            lastKeepWarmEnabled = (data.boxValue6 === 'true');
            lastDoneAlarmEnabled = (data.boxValue6 === 'true');
        }
        if (data.boxValue8 !== undefined) {
            lastMeatDoneSetpoint = data.boxValue8;
        }
        if (data.boxValue0 !== undefined) {
            lastMeatTempValue = data.boxValue0;
        }
        if (meatDoneLbl) {
            if (lastKeepWarmEnabled) {
                if (lastMeatDoneSetpoint !== null && lastMeatDoneSetpoint !== undefined) {
                    meatDoneLbl.textContent = `(${lastMeatDoneSetpoint})`;
                }
            } else {
                meatDoneLbl.textContent = "";
            }
        }

        // Trigger Done Alarm toast when target reached
        checkMeatDoneToast();

        // Update setpoint input only if changed externally
        if (data.boxValue2 !== undefined && data.boxValue2 !== null) {
            if (lastSetpoint !== data.boxValue2) {
                updateDisplayValue('boxValue2', 'box2', data.boxValue2);
                lastSetpoint = data.boxValue2;
            }
        }

        // Update meat target input only if changed externally
        if (data.boxValue8 !== undefined) {
            const box8 = document.getElementById('box8');
            if (shouldApplyIncomingValue('box8', box8, data.boxValue8)) {
                box8.value = data.boxValue8;
            }
        }

        // Update keep warm setpoint input only if changed externally
        if (data.boxValue9 !== undefined) {
            const box9 = document.getElementById('box9');
            if (shouldApplyIncomingValue('box9', box9, data.boxValue9)) {
                box9.value = data.boxValue9;
            }
        }

        // Pit Probe
        if (data.boxValue1 !== undefined && data.boxValue1 !== null) {
            updateDisplayValue('calPitTemp', null, data.boxValue1);
        }
        const pitOffsetInput = document.getElementById('pitOffsetInput');
        if (data.pitOffset !== undefined && shouldApplyIncomingValue('pitOffsetInput', pitOffsetInput, data.pitOffset)) {
            pitOffsetInput.value = data.pitOffset;
        }

        // Meat Probe
        if (data.boxValue0 !== undefined && data.boxValue0 !== null) {
            updateDisplayValue('calMeatTemp', null, data.boxValue0);
        }
        const meatOffsetInput = document.getElementById('meatOffsetInput');
        if (data.meatOffset !== undefined && shouldApplyIncomingValue('meatOffsetInput', meatOffsetInput, data.meatOffset)) {
            meatOffsetInput.value = data.meatOffset;
        }

        // PID Parameters
        const kpInput = document.getElementById('kpInput');
        if (data.kp !== undefined && shouldApplyIncomingValue('kpInput', kpInput, data.kp)) {
            kpInput.value = data.kp;
        }
        const kiInput = document.getElementById('kiInput');
        if (data.ki !== undefined && shouldApplyIncomingValue('kiInput', kiInput, data.ki)) {
            kiInput.value = data.ki;
        }
        const kdInput = document.getElementById('kdInput');
        if (data.kd !== undefined && shouldApplyIncomingValue('kdInput', kdInput, data.kd)) {
            kdInput.value = data.kd;
        }

        if (data.isAP !== undefined) {
            updateAPModeUI(data.isAP);
            updateWiFiDisplay(data);
        }

        // OTA status/progress
        if (data.otaStatus !== undefined) {
            handleOTAStatus(data);
        }

        // Sync Alarm Checkboxes (boxValue 4 and 6)
        ['boxValue4', 'boxValue6'].forEach(id => {
            const cb = document.getElementById(id);
            if (cb && data[id] !== undefined) {
                cb.checked = (data[id] === 'true');
            }
        });
        
        // Update alarm button states
        updateAlarmButtonStates();

        if (data.timezone && selectedTimezone === "") {
            selectedTimezone = data.timezone;
            updateTimezoneUI();
        }

        // Handle Autotune Status
        if (data.atActive !== undefined) {
            const atToggle = document.getElementById('autotuneToggle');
            if (atToggle) atToggle.checked = data.atActive;

            const atStatusText = document.getElementById('autotuneStatusValue');
            const atResults = document.getElementById('autotuneResults');
            const saveBtn = document.getElementById('savePidBtn');

            if (atStatusText) {
                const states = ['Idle', 'Tuning...', 'Complete!', 'Failed'];
                atStatusText.innerText = states[data.atState || 0];
                atStatusText.style.color = (data.atState === 1) ? '#f1c40f' : (data.atState === 2 ? '#2ecc71' : (data.atState === 3 ? '#e74c3c' : '#888'));
            }

            if (atResults) {
                // Populate results from current PID values
                document.getElementById('atKp').innerText = data.kp || "--";
                document.getElementById('atKi').innerText = data.ki || "--";
                document.getElementById('atKd').innerText = data.kd || "--";

                if (data.atState === 2) { // Complete
                    atResults.style.opacity = '1';
                    atResults.style.border = '1px solid #2ecc71';
                } else if (data.atState === 1) { // Tuning
                    atResults.style.opacity = '0.6';
                    atResults.style.border = '1px solid transparent';
                } else { // Idle / Failed
                    atResults.style.opacity = '0.7';
                    atResults.style.border = '1px solid rgba(255,255,255,0.05)';
                }
            }
            if (saveBtn) {
                saveBtn.disabled = data.atActive;
                saveBtn.style.opacity = data.atActive ? '0.5' : '1';
                saveBtn.style.pointerEvents = data.atActive ? 'none' : 'auto';
            }
        }
    } catch (error) {
        console.error('[WebSocket] Failed to parse message:', error);
    }
}

function markValuesReceived(data) {
    if (hasReceivedValues) return;
    if (!data || typeof data !== 'object') return;

    if (data.boxValue0 !== undefined || data.boxValue1 !== undefined ||
        data.boxValue2 !== undefined || data.boxValue3 !== undefined) {
        hasReceivedValues = true;
        stopValuesRetry();
    }
}

function startValuesRetry() {
    if (valuesRetryTimer) return;
    valuesRetryTimer = setInterval(() => {
        if (!hasReceivedValues) {
            getValues();
        } else {
            stopValuesRetry();
        }
    }, 1500);
}

function stopValuesRetry() {
    if (!valuesRetryTimer) return;
    clearInterval(valuesRetryTimer);
    valuesRetryTimer = null;
}

function updateAPModeUI(isAP) {
    const tzBtn = document.getElementById('saveTzBtn');
    const tzGrid = document.getElementById('timezoneGrid');
    const notice = document.getElementById('apModeNotice');

    // OTA elements
    const otaCheckBtn = document.getElementById('otaCheckBtn');
    const otaUpdateBtn = document.getElementById('otaUpdateBtn');
    const otaNotice = document.getElementById('otaApModeNotice');

    if (isAP) {
        // Disable timezone controls
        if (tzBtn) {
            tzBtn.disabled = true;
            tzBtn.style.opacity = '0.5';
            tzBtn.style.cursor = 'not-allowed';
            tzBtn.innerText = 'Disabled in AP Mode';
        }
        if (tzGrid) {
            tzGrid.style.pointerEvents = 'none';
            tzGrid.style.opacity = '0.4';
        }
        if (notice) notice.style.display = 'block';

        // Disable OTA controls
        if (otaCheckBtn) {
            otaCheckBtn.disabled = true;
            otaCheckBtn.style.opacity = '0.5';
            otaCheckBtn.style.cursor = 'not-allowed';
            otaCheckBtn.innerText = 'Disabled in AP Mode';
        }
        if (otaUpdateBtn) {
            otaUpdateBtn.disabled = true;
            otaUpdateBtn.style.opacity = '0.5';
            otaUpdateBtn.style.cursor = 'not-allowed';
        }
        if (otaNotice) otaNotice.style.display = 'block';
    } else {
        // Enable timezone controls
        if (tzBtn) {
            tzBtn.disabled = false;
            tzBtn.style.opacity = '1';
            tzBtn.style.cursor = 'pointer';
            tzBtn.innerText = 'Apply Timezone Change';
        }
        if (tzGrid) {
            tzGrid.style.pointerEvents = 'auto';
            tzGrid.style.opacity = '1';
        }
        if (notice) notice.style.display = 'none';

        // Enable OTA controls
        if (otaCheckBtn) {
            otaCheckBtn.disabled = false;
            otaCheckBtn.style.opacity = '1';
            otaCheckBtn.style.cursor = 'pointer';
            otaCheckBtn.innerText = 'Check for Update';
        }
        // Note: otaUpdateBtn stays disabled until an update is available
        if (otaNotice) otaNotice.style.display = 'none';
    }
}

function updateWiFiDisplay(data) {
    const indicator = document.getElementById('wifi-indicator');
    const modeEl = document.getElementById('wifi-mode');
    const ssidEl = document.getElementById('wifi-ssid');
    const signalEl = document.getElementById('wifi-signal');

    if (!indicator || !modeEl || !ssidEl || !signalEl) return;

    // Update mode and indicator color
    const isAP = data.isAP === true;
    const connected = data.connected !== undefined ? data.connected : !isAP;
    const ssid = data.ssid || (isAP ? 'Access Point Active' : '--');
    const rssi = (data.rssi !== undefined && data.rssi !== null) ? data.rssi : null;

    if (isAP) {
        modeEl.textContent = 'AP MODE';
        indicator.style.background = '#fbbf24'; // Yellow for AP
        indicator.style.boxShadow = '0 0 6px rgba(251,191,36,0.6)';
        ssidEl.textContent = ssid;
        signalEl.textContent = 'n/a';
    } else {
        modeEl.textContent = connected ? 'STA MODE' : 'STA MODE (DISCONNECTED)';
        indicator.style.background = connected ? '#10b981' : '#ef4444';
        indicator.style.boxShadow = connected
            ? '0 0 6px rgba(16,185,129,0.6)'
            : '0 0 6px rgba(239,68,68,0.6)';
        ssidEl.textContent = connected ? ssid : 'Disconnected';
        signalEl.textContent = (connected && rssi !== null) ? `${rssi} dBm` : '--';
    }
}


function updateDisplayValue(spanId, inputId, value) {
    const span = document.getElementById(spanId);
    const input = document.getElementById(inputId);

    if (value === undefined || value === null) {
        return;
    }

    const safeValue = (value === '' || value === 'undefined') ? '--' : value;

    if (span) {
        span.textContent = safeValue;
        console.log(`[UI] Updated ${spanId} to ${safeValue}`);
    }

    if (input && input.tagName === 'INPUT') {
        if (String(input.value) !== String(safeValue)) {
            input.value = safeValue;
        }
    }
}

// =============================================================================
// USER INPUT HANDLERS
// =============================================================================
function updateBox(element) {
    if (!element) return;
    const id = element.id;
    const value = parseInt(element.value, 10);

    if (id === 'box2') {
        if (isValidSetpoint(value)) {
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                const message = "2b" + value.toString();
                websocket.send(message);
                console.log(`[WebSocket] Sent setpoint update: ${message}`);
                lastSetpoint = value.toString();
            } else {
                console.error('[WebSocket] Cannot send - not connected');
                alert("Connection lost. Reconnecting...");
                element.value = lastSetpoint;
            }
        } else {
            alert("Enter a value between " + SETPOINT_MIN + "-" + SETPOINT_MAX + "°F");
            element.value = lastSetpoint;
            getValues();
        }
    } else if (id === 'box8') {
        if (!isNaN(value) && value >= 32 && value <= 225) {
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                const message = "8b" + value.toString();
                websocket.send(message);
                console.log(`[WebSocket] Sent meat target update: ${message}`);
            } else {
                console.error('[WebSocket] Cannot send - not connected');
                alert("Connection lost. Reconnecting...");
                getValues();
            }
        } else {
            alert("Enter a value between 32-225°F");
            getValues();
        }
    } else if (id === 'box9') {
        if (!isNaN(value) && value >= 160 && value <= 450) {
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                const message = "9b" + value.toString();
                websocket.send(message);
                console.log(`[WebSocket] Sent keep warm setpoint update: ${message}`);
            } else {
                console.error('[WebSocket] Cannot send - not connected');
                alert("Connection lost. Reconnecting...");
                getValues();
            }
        } else {
            alert("Enter a value between 160-450°F");
            getValues();
        }
    }
}

function isValidSetpoint(value) {
    return !isNaN(value) && value >= SETPOINT_MIN && value <= SETPOINT_MAX;
}

// =============================================================================
// ALARM CHECKBOX HANDLERS (for alarms.html page)
// =============================================================================
function updateCheckbox() {
    sendAlarmState('boxValue4', 'KeepWarm');
    sendAlarmState('boxValue6', 'DoneAlarm');
    
    // Update button states based on checkbox states
    updateAlarmButtonStates();
}

function sendAlarmState(checkboxId, alarmName) {
    const checkbox = document.getElementById(checkboxId);
    if (!checkbox) return;

    const state = checkbox.checked ? 'true' : 'false';

    if (websocket && websocket.readyState === WebSocket.OPEN) {
        const message = alarmName + state;
        websocket.send(message);
        console.log(`[WebSocket] Sent alarm state: ${message}`);
    }
}

function updateAlarmButtonStates() {
    // Done Alarm checkbox (boxValue6) controls Meat Done Temp SET button (box8)
    const doneAlarmCheckbox = document.getElementById('boxValue6');
    const box8Button = document.querySelector('button[onclick="updateBox(document.getElementById(\'box8\'))"]');
    
    if (doneAlarmCheckbox && box8Button) {
        box8Button.disabled = !doneAlarmCheckbox.checked;
        box8Button.style.opacity = doneAlarmCheckbox.checked ? '1' : '0.5';
        box8Button.style.cursor = doneAlarmCheckbox.checked ? 'pointer' : 'not-allowed';
    }
    
    // Keep Warm checkbox (boxValue4) can only be clicked if Done Alarm is checked
    const keepWarmCheckbox = document.getElementById('boxValue4');
    const box9Button = document.querySelector('button[onclick="updateBox(document.getElementById(\'box9\'))"]');
    
    if (keepWarmCheckbox && doneAlarmCheckbox) {
        // Disable Keep Warm checkbox if Done Alarm is not checked
        keepWarmCheckbox.disabled = !doneAlarmCheckbox.checked;
        keepWarmCheckbox.style.opacity = doneAlarmCheckbox.checked ? '1' : '0.5';
        keepWarmCheckbox.style.cursor = doneAlarmCheckbox.checked ? 'pointer' : 'not-allowed';
        
        // If Done Alarm is unchecked, also uncheck Keep Warm
        if (!doneAlarmCheckbox.checked && keepWarmCheckbox.checked) {
            keepWarmCheckbox.checked = false;
            sendAlarmState('boxValue4', 'KeepWarm');
        }
    }
    
    // Keep Warm checkbox (boxValue4) controls Keep Warm SET button (box9)
    if (keepWarmCheckbox && box9Button) {
        box9Button.disabled = !keepWarmCheckbox.checked;
        box9Button.style.opacity = keepWarmCheckbox.checked ? '1' : '0.5';
        box9Button.style.cursor = keepWarmCheckbox.checked ? 'pointer' : 'not-allowed';
    }
}

// =============================================================================
// TOAST NOTIFICATIONS
// =============================================================================
function ensureToastContainer() {
    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        container.className = 'toast-container';
        document.body.appendChild(container);
    }
    return container;
}

function showToast(message, type = 'info') {
    const container = ensureToastContainer();
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.textContent = message;
    container.appendChild(toast);

    requestAnimationFrame(() => {
        toast.classList.add('show');
    });

    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

function checkMeatDoneToast() {
    if (!lastDoneAlarmEnabled) {
        doneAlarmTriggered = false;
        return;
    }

    const meatTemp = parseFloat(lastMeatTempValue);
    const meatSetpoint = parseFloat(lastMeatDoneSetpoint);

    if (isNaN(meatTemp) || isNaN(meatSetpoint)) {
        doneAlarmTriggered = false;
        return;
    }

    if (meatTemp >= meatSetpoint) {
        if (!doneAlarmTriggered) {
            const meatTempDisplay = Number.isFinite(meatTemp) ? meatTemp.toFixed(1) : '--';
            const setpointDisplay = Number.isFinite(meatSetpoint) ? Math.round(meatSetpoint) : '--';
            showToast(`Meat done: ${meatTempDisplay}°F reached target ${setpointDisplay}°F`, 'success');
            doneAlarmTriggered = true;
        }
    } else {
        doneAlarmTriggered = false;
    }
}

// =============================================================================
// WIFI PAGE HELPERS (for wifi.html page)
// =============================================================================
function hidePass() {
    const passInput = document.getElementById("pass");
    if (!passInput) return;

    passInput.type = (passInput.type === "password") ? "text" : "password";
}

// =============================================================================
// CALIBRATION FUNCTIONS (for calibration.html page)
// =============================================================================
function savePitCalibration() {
    const offsetInput = document.getElementById("pitOffsetInput");
    if (!offsetInput) return;

    const offset = offsetInput.value;
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        const message = "CalibratePit:" + offset;
        websocket.send(message);
        console.log(`[WebSocket] Sent pit calibration: ${message}`);
        alert("Pit offset saved!");
    } else {
        alert("Not connected to controller!");
    }
}

function saveMeatCalibration() {
    const offsetInput = document.getElementById("meatOffsetInput");
    if (!offsetInput) return;

    const offset = offsetInput.value;
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        const message = "CalibrateMeat:" + offset;
        websocket.send(message);
        console.log(`[WebSocket] Sent meat calibration: ${message}`);
        alert("Meat offset saved!");
    } else {
        alert("Not connected to controller!");
    }
}

function savePIDSettings() {
    const kpInput = document.getElementById("kpInput");
    const kiInput = document.getElementById("kiInput");
    const kdInput = document.getElementById("kdInput");
    if (!kpInput || !kiInput || !kdInput) return;

    const kp = parseFloat(kpInput.value).toFixed(2);
    const ki = parseFloat(kiInput.value).toFixed(2);
    const kd = parseFloat(kdInput.value).toFixed(2);

    if (websocket && websocket.readyState === WebSocket.OPEN) {
        const message = `UpdatePID:${kp}:${ki}:${kd}`;
        websocket.send(message);
        console.log(`[WebSocket] Sent PID tuning: ${message}`);
        alert("PID tuning parameters updated and saved!");
    } else {
        alert("Not connected to controller!");
    }
}

function adjustValue(inputId, delta) {
    const input = document.getElementById(inputId);
    if (!input) return;

    let currentValue = parseFloat(input.value);
    if (isNaN(currentValue)) {
        currentValue = 0;
    }

    // Use toFixed to avoid floating point issues if delta is small
    input.value = (currentValue + delta);
    markInputDirty(inputId);
    input.focus();
}
// =============================================================================
// TIMEZONE HANDLERS
// =============================================================================
function updateTimezoneUI() {
    const grid = document.getElementById('timezoneGrid');
    if (!grid) return;

    const options = grid.querySelectorAll('.tz-option');
    options.forEach(opt => {
        const radio = opt.querySelector('input');
        if (radio.value === selectedTimezone) {
            opt.classList.add('selected');
            radio.checked = true;
        } else {
            opt.classList.remove('selected');
            radio.checked = false;
        }
    });
}

function selectTimezone(tz, element) {
    selectedTimezone = tz;
    updateTimezoneUI();
}

function saveTimezoneSettings() {
    if (!selectedTimezone) {
        alert("Please select a timezone first.");
        return;
    }

    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send("UpdateTimezone:" + selectedTimezone);
        alert("Timezone update sent to controller.");
    } else {
        alert("Not connected to controller. Please refresh and try again.");
    }
}

function toggleAutotune() {
    const atToggle = document.getElementById('autotuneToggle');
    const saveBtn = document.getElementById('savePidBtn');
    if (!atToggle) return;

    const active = atToggle.checked;

    // Immediate UI feedback
    if (saveBtn) {
        saveBtn.disabled = active;
        saveBtn.style.opacity = active ? '0.5' : '1';
        saveBtn.style.pointerEvents = active ? 'none' : 'auto';
    }

    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send(`StartAutotune:${active}`);
        console.log(`[WebSocket] Sent Autotune toggle: ${active}`);
    } else {
        atToggle.checked = !active; // Revert checkbox
        if (saveBtn) {
            saveBtn.disabled = !active;
            saveBtn.style.opacity = !active ? '0.5' : '1';
            saveBtn.style.pointerEvents = !active ? 'none' : 'auto';
        }
        alert("Not connected to controller!");
    }
}
