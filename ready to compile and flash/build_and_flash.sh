#!/bin/bash
#
# build_and_flash.sh
# Builds and flashes the AI Stethoscope firmware to an ESP32-S3 device.
#
# Usage:
#   ./build_and_flash.sh [env]
#
# Arguments:
#   env    - Build environment (default: esp32-s3-devkitc-1)
#            Use 'esp32-s3-devkitc-1-psram' for PSRAM boards.
#
# Prerequisites:
#   - Python virtualenv with PlatformIO installed (~/yoloenv/bin/activate)
#   - ESP32-S3 connected via USB

set -e

ENV=${1:-esp32-s3-devkitc-1}
VENV_PATH=~/yoloenv/bin/activate

# Check if virtualenv exists
if [ ! -f "$VENV_PATH" ]; then
    echo "❌ Error: virtualenv not found at $VENV_PATH"
    echo "Please create it with: python3 -m venv ~/yoloenv && source ~/yoloenv/bin/activate && pip install platformio"
    exit 1
fi

# Activate virtualenv
source "$VENV_PATH"

echo "📦 Building firmware for environment: $ENV"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Build
if platformio run -e "$ENV"; then
    echo ""
    echo "✅ Build succeeded!"
    echo ""
    echo "🔧 Ready to flash. Ensure ESP32-S3 is connected via USB."
    echo "   Run: platformio run -e $ENV -t upload"
    echo ""
    echo "📺 To monitor output after flashing:"
    echo "   platformio device monitor -e $ENV"
else
    echo "❌ Build failed. Check errors above."
    exit 1
fi
