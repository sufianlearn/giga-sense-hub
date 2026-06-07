#!/bin/bash
# Flash waveshare-hub to ESP32-S3 7" display
# Usage: ./flash.sh [port]
# If no port given, auto-detects

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Source ESP-IDF
export IDF_PATH="$HOME/esp/esp-idf"
. "$IDF_PATH/export.sh" 2>/dev/null

# Auto-detect port
if [ -n "$1" ]; then
    PORT="$1"
else
    # Prefer UART port (CH343) over USB-JTAG
    for p in /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/cu.usbmodem58760464071; do
        if [ -e "$p" ]; then
            PORT="$p"
            break
        fi
    done
fi

if [ -z "$PORT" ]; then
    echo "ERROR: No serial port found. Connect the Waveshare board."
    exit 1
fi

echo "Flashing to $PORT..."
echo "Build dir: $BUILD_DIR"

# Check build exists
if [ ! -f "$BUILD_DIR/giga_sense_hub.bin" ]; then
    echo "No build found. Building first..."
    cd "$SCRIPT_DIR"
    idf.py build
fi

cd "$SCRIPT_DIR"
idf.py -p "$PORT" flash monitor
