#!/bin/bash

# ESP32 Smoker Controller - Automated Test Script
# Run this after making changes to verify basic functionality

echo "=== ESP32 Smoker Controller Test Script ==="
echo "Testing changes: Removed serial prints from wifiLedTask"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check command success
check_result() {
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $1"
        return 0
    else
        echo -e "${RED}✗ FAIL${NC}: $1"
        return 1
    fi
}

echo "1. Building firmware..."
pio run > build_log.txt 2>&1
check_result "Firmware build"

echo "2. Building filesystem..."
pio run -t buildfs > buildfs_log.txt 2>&1
check_result "Filesystem build"

echo "3. Checking for compilation errors..."
if grep -q "error:" build_log.txt; then
    echo -e "${RED}✗ FAIL${NC}: Compilation errors found"
    grep "error:" build_log.txt
else
    echo -e "${GREEN}✓ PASS${NC}: No compilation errors"
fi

echo "4. Checking binary sizes..."
FIRMWARE_SIZE=$(stat -c%s .pio/build/esp32dev/firmware.bin 2>/dev/null || echo "0")
FS_IMAGE_PATH=".pio/build/esp32dev/littlefs.bin"
FS_LABEL="LittleFS"

FS_SIZE=$(stat -c%s "$FS_IMAGE_PATH" 2>/dev/null || echo "0")

if [ "$FIRMWARE_SIZE" -gt 100000 ]; then
    echo -e "${GREEN}✓ PASS${NC}: Firmware size reasonable ($FIRMWARE_SIZE bytes)"
else
    echo -e "${RED}✗ FAIL${NC}: Firmware size too small ($FIRMWARE_SIZE bytes)"
fi

if [ "$FS_SIZE" -gt 10000 ]; then
    echo -e "${GREEN}✓ PASS${NC}: $FS_LABEL size reasonable ($FS_SIZE bytes)"
else
    echo -e "${RED}✗ FAIL${NC}: $FS_LABEL size too small ($FS_SIZE bytes)"
fi

echo "5. Checking for removed LOOP serial prints..."
if grep -A5 -B5 "wifiLedTask" src/main.cpp | grep -q "Serial\.printf.*Toggle\|Serial\.printf.*AP mode\|Serial\.printf.*Unknown state\|Serial\.println.*TASK STARTED"; then
    echo -e "${RED}✗ FAIL${NC}: Serial prints still present in wifiLedTask loop"
else
    echo -e "${GREEN}✓ PASS${NC}: Serial spam prints removed from wifiLedTask loop"
fi

echo "6. Verifying WebSocket code integrity..."
if grep -q "WebSocketHandler" src/main.cpp && grep -q "wsHandler->init" src/main.cpp; then
    echo -e "${GREEN}✓ PASS${NC}: WebSocket initialization code intact"
else
    echo -e "${RED}✗ FAIL${NC}: WebSocket initialization may be broken"
fi

echo "7. Checking memory usage..."
RAM_USAGE=$(grep "RAM:" build_log.txt | grep -o "[0-9]*%" | head -1)
FLASH_USAGE=$(grep "Flash:" build_log.txt | grep -o "[0-9]*%" | head -1)

if [ -n "$RAM_USAGE" ] && [ "${RAM_USAGE%\%}" -lt 90 ]; then
    echo -e "${GREEN}✓ PASS${NC}: RAM usage acceptable ($RAM_USAGE)"
else
    echo -e "${YELLOW}? WARN${NC}: RAM usage high or not found ($RAM_USAGE)"
fi

if [ -n "$FLASH_USAGE" ] && [ "${FLASH_USAGE%\%}" -lt 95 ]; then
    echo -e "${GREEN}✓ PASS${NC}: Flash usage acceptable ($FLASH_USAGE)"
else
    echo -e "${YELLOW}? WARN${NC}: Flash usage high or not found ($FLASH_USAGE)"
fi

echo ""
echo "=== Manual Tests Required ==="
echo "After uploading firmware + filesystem, manually verify:"
echo "• Serial output shows clean startup (no LED spam)"
echo "• Web interface loads at http://<IP>/"
echo "• WebSocket updates temperatures in real-time"
echo "• WiFi LED indicates connection status correctly"
echo "• No system lockups during 30+ minute operation"
echo ""

echo "=== Upload Commands ==="
echo "pio run -t upload --upload-port /dev/ttyUSB1 && pio run -t uploadfs --upload-port /dev/ttyUSB1"
echo "pio device monitor -p /dev/ttyUSB1 -b 115200"
echo ""

echo "Test completed. Check above for any FAIL results."