# AI Stethoscope — ESP32-S3 No-PSRAM Quick Start

This guide walks you through building and flashing the heart-sound classifier firmware to an **ESP32-S3-DevKitC-1** (no PSRAM).

## Hardware
- **Board:** ESP32-S3-DevKitC-1 (8 MB Flash, no PSRAM)
- **Audio input:** I2S microphone or PDM input (pins configured in `include/config.h`)
- **USB:** Micro-USB for flashing and power

## Prerequisites

1. **Python 3.8+** and `pip`
2. **Virtualenv** (recommended):
   ```bash
   python3 -m venv ~/yoloenv
   source ~/yoloenv/bin/activate
   pip install platformio
   ```

## Building the Firmware

### Option A: Quick build script
```bash
source ~/yoloenv/bin/activate
chmod +x build_and_flash.sh
./build_and_flash.sh esp32-s3-devkitc-1
```

### Option B: Manual build
```bash
source ~/yoloenv/bin/activate
platformio run -e esp32-s3-devkitc-1
```

**Expected output (end of build):**
```
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [====      ]  65.3% (used 213928 bytes from 327680 bytes)
Flash: [====      ]  27.5% (used 864921 bytes from 3145728 bytes)
```

## Flashing to Device

1. **Connect** ESP32-S3 via USB.
2. **Flash:**
   ```bash
   source ~/yoloenv/bin/activate
   platformio run -e esp32-s3-devkitc-1 -t upload
   ```
3. **Monitor (optional):**
   ```bash
   platformio device monitor -e esp32-s3-devkitc-1
   ```

## Configuration

**Audio & Model Settings:** Edit `include/config.h`
- `kAudioWindowSeconds` — audio window length (default: 2 sec, no-PSRAM)
- `kTensorArenaSize` — inference memory (default: 128 KB, no-PSRAM)
- `kAudioSampleRateHz` — sample rate (hardcoded: 16 kHz)
- `kClassNames` — disease classes (AS, MR, MS, MVP, N)

**Hardware Pins:** Edit `include/config.h`
- `I2S_BCLK_PIN`, `I2S_LRCK_PIN`, `I2S_SD_PIN` — I2S audio interface

## Troubleshooting

### Build error: DRAM overflow
**Cause:** Audio window or tensor arena is too large for available RAM.  
**Fix:** Reduce `kAudioWindowSeconds` or `kTensorArenaSize` in `include/config.h`.

### Upload fails / Device not found
**Fix:**
1. Check USB cable and port: `platformio device list`
2. Hold **BOOT** button, press **RST**, release **BOOT** (force bootloader mode).
3. Retry upload.

### Weird behavior after flash
**Fix:** Clear flash and reflash:
```bash
platformio run -e esp32-s3-devkitc-1 -t erase
platformio run -e esp32-s3-devkitc-1 -t upload
```

## Integration: Using with Dashboard

The firmware outputs classification results via serial and can integrate with the Dashboard via:
1. **Serial bridge** (USB→MQTT): parse UART output and forward to MQTT topics.
2. **Raspberry Pi + Viam** (optional): offload inference to a Pi using `tools/mqtt_bridge.py`.

See `INTEGRATION-MAPPING.md` for details.

## Memory Notes

This board has **320 KB RAM totals**:
- ~64 KB: FreeRTOS kernel & OS
- ~150 KB: Static buffers (audio window + features)
- ~128 KB: Tensor arena
- ~60 KB: Free (varies with FreeRTOS tasks)

If you upgrade to a **PSRAM-capable board**, use:
```bash
platformio run -e esp32-s3-devkitc-1-psram
```
to enable larger buffers (5-sec audio, 192 KB arena) for better accuracy.

## Next Steps

1. ✅ Flash the device.
2. 🔊 Connect microphone and test audio capture.
3. 🎯 Run inference and verify classification output.
4. 📊 (Optional) Stream to Dashboard via MQTT.

---
**Questions?** Check `ESP32-flashing-guide.md` for detailed flashing steps or open an issue.
