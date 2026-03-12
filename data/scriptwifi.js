// =============================================================================
// Smoker Controller - WiFi Setup Page JavaScript
// =============================================================================

// =============================================================================
// WIFI STATUS UPDATE
// =============================================================================
function updateWiFiDisplay(data) {
    const indicator = document.getElementById('wifi-indicator');
    const modeEl = document.getElementById('wifi-mode');
    const ssidEl = document.getElementById('wifi-ssid');
    const signalEl = document.getElementById('wifi-signal');

    if (!indicator || !modeEl || !ssidEl || !signalEl) return;

    // Update mode and indicator color based on mode property
    const isAP = data.mode === 'AP';
    
    if (isAP) {
        modeEl.textContent = 'AP MODE';
        indicator.style.background = '#fbbf24'; // Yellow for AP
        indicator.style.boxShadow = '0 0 6px rgba(251,191,36,0.6)';
        ssidEl.textContent = 'Access Point Active';
        signalEl.textContent = 'n/a';
    } else {
        modeEl.textContent = 'STA MODE';
        indicator.style.background = '#10b981'; // Green for STA
        indicator.style.boxShadow = '0 0 6px rgba(16,185,129,0.6)';
        ssidEl.textContent = data.ssid || 'Connected';
        signalEl.textContent = data.rssi ? `${data.rssi} dBm` : '--';
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================
window.addEventListener('load', onLoad);

function onLoad() {
    console.log('[WiFi] Initializing WiFi Setup Page...');
    initMenuDropdown();
    scanAndPopulateNetworks();

    // Initialize IP configuration fields visibility and labels
    toggleIPFields();
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
// PASSWORD VISIBILITY TOGGLE
// =============================================================================
function hidePass() {
    const passInput = document.getElementById("pass");
    if (!passInput) return;

    passInput.type = (passInput.type === "password") ? "text" : "password";
}

// =============================================================================
// NETWORK SCANNING
// =============================================================================
function setScanButtonEnabled(scanButton, enabled) {
    if (!scanButton) return;

    scanButton.disabled = !enabled;
    scanButton.style.opacity = enabled ? '1' : '0.5';
    scanButton.style.cursor = enabled ? 'pointer' : 'not-allowed';
}

function scanAndPopulateNetworks() {
    const ssidInput = document.getElementById('ssid');
    const ssidDatalist = document.getElementById('ssid-list');
    const scanIcon = document.getElementById('scan-icon');
    const scanText = document.getElementById('scan-text');
    const scanStatus = document.getElementById('scan-status');
    const scanButton = document.querySelector('button[onclick="scanAndPopulateNetworks()"]');
    let currentStatus = null;

    if (!ssidInput || !ssidDatalist) return;

    // Reset to scanning state
    scanIcon.textContent = '🔍';
    scanText.textContent = 'Checking status...';
    scanStatus.style.color = 'var(--text-muted)';
    ssidDatalist.innerHTML = ''; // Clear previous results

    console.log('[WiFi] Checking device status...');

    // First, check the current mode
    fetch('/api/status')
        .then(response => {
            if (!response.ok) {
                throw new Error('Status check failed');
            }
            return response.json();
        })
        .then(statusData => {
            console.log('[WiFi] Device status:', statusData);
            currentStatus = statusData;

            // Update WiFi display
            updateWiFiDisplay(statusData);

            // Check if we're in STA mode
            if (statusData.mode === 'STA' && statusData.connected) {
                console.log('[WiFi] STA mode detected - scan disabled while connected');

                // Pre-populate the input with current SSID
                ssidInput.value = statusData.ssid;
                scanIcon.textContent = '';
                scanText.textContent = `SSID: ${statusData.ssid} - IP: ${statusData.ip}`;
                scanStatus.style.color = 'var(--text-muted)';
                setScanButtonEnabled(scanButton, false);
                return { networks: [] };
            }

            console.log('[WiFi] Scanning for networks...');
            scanIcon.textContent = '🔍';
            scanText.textContent = 'Scanning...';
            setScanButtonEnabled(scanButton, true);

            // Fetch networks from API
            return fetch('/api/networks');
        })
        .then(response => {
            if (!response || typeof response.json !== 'function') {
                return response || { networks: [] };
            }
            if (!response.ok) {
                throw new Error('Network scan failed');
            }
            return response.json();
        })
        .then(data => {
            console.log('[WiFi] Networks received:', data);

            // Clear datalist
            ssidDatalist.innerHTML = '';

            if (data.networks && data.networks.length > 0) {
                // Sort networks by signal strength (RSSI)
                data.networks.sort((a, b) => b.rssi - a.rssi);

                // Add each network as an option in the datalist
                data.networks.forEach(network => {
                    const option = document.createElement('option');
                    option.value = network.ssid;

                    // Format: SSID (Signal Strength) [Security]
                    const signalBars = getSignalBars(network.rssi);
                    const security = network.secure ? '🔒' : '🔓';
                    option.label = `${network.ssid} ${signalBars} ${security}`;

                    ssidDatalist.appendChild(option);
                });

                // Update status
                scanIcon.textContent = '✓';
                scanText.textContent = `Found ${data.networks.length} network(s) - type or select`;
                scanStatus.style.color = 'var(--accent-green)';
            } else {
                // No networks found - keep STA status informative instead of showing a manual-entry error
                if (currentStatus && currentStatus.mode === 'STA' && currentStatus.connected) {
                    scanIcon.textContent = '';
                    scanText.textContent = `SSID: ${currentStatus.ssid} - IP: ${currentStatus.ip}`;
                    scanStatus.style.color = 'var(--text-muted)';
                } else {
                    scanIcon.textContent = '⚠';
                    scanText.textContent = 'No networks found - enter manually';
                    scanStatus.style.color = 'var(--accent-orange)';
                }
            }
        })
        .catch(error => {
            console.error('[WiFi] Error:', error);

            if (currentStatus && currentStatus.mode === 'STA' && currentStatus.connected) {
                scanIcon.textContent = '';
                scanText.textContent = `SSID: ${currentStatus.ssid} - IP: ${currentStatus.ip}`;
                scanStatus.style.color = 'var(--text-muted)';
                setScanButtonEnabled(scanButton, false);
                return;
            }

            // Show error state only when we do not have an active STA connection to display
            scanIcon.textContent = '✗';
            scanText.textContent = 'Error - enter manually';
            scanStatus.style.color = 'var(--accent-red)';
            setScanButtonEnabled(scanButton, true);
        });
}

// Helper function to convert RSSI to signal bars
function getSignalBars(rssi) {
    if (rssi >= -50) return '▰▰▰▰'; // Excellent
    if (rssi >= -60) return '▰▰▰▱'; // Good
    if (rssi >= -70) return '▰▰▱▱'; // Fair
    if (rssi >= -80) return '▰▱▱▱'; // Weak
    return '▱▱▱▱'; // Very weak
}

// =============================================================================
// IP CONFIGURATION MODE TOGGLE
// =============================================================================
function toggleIPFields() {
    const dhcpCheckbox = document.getElementById('usedhcp');
    const staticFields = document.getElementById('static-ip-fields');
    const ipInput = document.getElementById('ip');
    const gatewayInput = document.getElementById('gateway');
    const staticLabel = document.getElementById('mode-label-static');
    const dhcpLabel = document.getElementById('mode-label-dhcp');

    if (!dhcpCheckbox || !staticFields) return;

    const useDHCP = dhcpCheckbox.checked;

    if (useDHCP) {
        // DHCP mode - hide static IP fields
        staticFields.classList.add('hidden');
        ipInput.required = false;
        gatewayInput.required = false;

        // Update label colors
        staticLabel.style.color = 'var(--text-muted)';
        staticLabel.style.fontWeight = 'normal';
        dhcpLabel.style.color = 'var(--accent-cyan)';
        dhcpLabel.style.fontWeight = '600';

        console.log('[WiFi] IP mode: DHCP (automatic)');
    } else {
        // Static mode - show IP fields
        staticFields.classList.remove('hidden');
        ipInput.required = true;
        gatewayInput.required = true;

        // Update label colors
        staticLabel.style.color = 'var(--accent-blue)';
        staticLabel.style.fontWeight = '600';
        dhcpLabel.style.color = 'var(--text-muted)';
        dhcpLabel.style.fontWeight = 'normal';

        console.log('[WiFi] IP mode: Static');
    }
}

// =============================================================================
// ERASE CREDENTIALS HANDLER
// =============================================================================
function handleEraseToggle() {
    const eraseCheckbox = document.getElementById('erase');
    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('pass');
    const ipInput = document.getElementById('ip');
    const gatewayInput = document.getElementById('gateway');

    if (!eraseCheckbox) return;

    const isErasing = eraseCheckbox.checked;

    if (isErasing) {
        // Disable required validation when erasing
        ssidInput.required = false;
        passInput.required = false;
        ipInput.required = false;
        gatewayInput.required = false;

        console.log('[WiFi] Erase mode: Required fields disabled');
    } else {
        // Re-enable required validation
        ssidInput.required = true;
        // Password is optional, don't set required

        // IP and Gateway required only in Static mode
        const dhcpCheckbox = document.getElementById('usedhcp');
        if (dhcpCheckbox && !dhcpCheckbox.checked) {
            ipInput.required = true;
            gatewayInput.required = true;
        }

        console.log('[WiFi] Normal mode: Required fields enabled');
    }
}
