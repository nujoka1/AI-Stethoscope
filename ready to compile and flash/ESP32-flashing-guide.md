# ESP32 Flashing & Build Guide

This guide explains how to build and flash the AI Stethoscope firmware when your ESP32 (S3) board arrives.

**Files referenced**:
- [include/config.h](include/config.h)
- [platformio.ini](platformio.ini)

## 1) Prerequisites
- Linux machine with USB access to the board.
- Python and a virtualenv (you have `yoloenv` in this workspace).

Commands assume you use the workspace `ready to compile and flash` folder.

## 2) Activate environment
In your shell (from the project root):

```bash
cd "ready to compile and flash"
. ~/yoloenv/bin/activate
```

Confirm Python and PlatformIO:

```bash
which python
platformio --version
```

If `platformio` is not installed in the venv, install it:

```bash
pip install --upgrade pip setuptools wheel
pip install platformio
```

## 3) Build the firmware
To build the release firmware for the default board (`esp32-s3-devkitc-1`):

```bash
platformio run -e esp32-s3-devkitc-1
```

For verbose output (useful for debugging link errors):

```bash
platformio run -e esp32-s3-devkitc-1 -v
```

## 4) Flash the board
Connect the ESP32 via USB and then:

```bash
platformio run -e esp32-s3-devkitc-1 -t upload
```

Open serial monitor (115200 default):

```bash
platformio device monitor -e esp32-s3-devkitc-1
```

## 5) Common issue: DRAM (.dram0.bss) overflow
If you see an error like `.dram0.bss will not fit in region 'dram0_0_seg'` or `DRAM segment data does not fit`, there isn't enough RAM for the static data.

Workarounds:
- Use a PSRAM-enabled board variant (recommended for larger models / long audio buffers).
  - Change the `board` in `platformio.ini` to a PSRAM-capable board (check PlatformIO boards list).
  - Or use a dev board with external PSRAM and enable it per board docs.
- Reduce memory usage in `include/config.h` (temporary quick fix):
  - Lower `kAudioWindowSeconds` (e.g., from 5 to 2).
  - Lower `kTensorArenaSize` (e.g., from 192*1024 to 128*1024).

Example file to edit: [include/config.h](include/config.h)

After edit, rebuild:

```bash
platformio run -e esp32-s3-devkitc-1
```

If you plan to use PSRAM, revert the reduced sizes to the values used for your model and tests.

## 6) Tips & recommended workflow
- Build first (`platformio run`) before uploading to make sure linking succeeds.
- Use `-e debug` environment in `platformio.ini` for debug builds (slower, with symbols).
- If flashing fails, try pressing BOOT/EN on your board during upload (some boards need manual reset/autoboot).
- Check `platformio.ini` `board` setting to match your exact hardware.

## 7) Verifying PSRAM presence at runtime
If you're using a board with PSRAM, you can add a quick runtime check in `setup()` to print heap/psram info, or use ESP-IDF APIs (`esp_spiram_get_size()`/`esp_spiram_is_initialized()`)

## 8) Advanced: inspect memory usage
PlatformIO prints memory usage after linking. For deeper inspection use:

```bash
platformio run -e esp32-s3-devkitc-1 -v
# or use PlatformIO Home > Project Inspect
```

Look at the `.pio/build/<env>/firmware.map` file and the RAM usage report.

## 9) Reverting the temporary memory reductions
If you edited `include/config.h` to get the build to succeed on a No-PSRAM board, revert the changes once you have a PSRAM board and re-run the full-size settings used during model training/validation.

---
If you'd like, I can also add a Makefile or a short script (`build_and_flash.sh`) to automate these steps. Do you want that?