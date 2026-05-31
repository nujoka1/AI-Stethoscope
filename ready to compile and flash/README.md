# AI Stethoscope Day 2 Firmware

This folder contains a PlatformIO firmware for the ESP32-S3 heart sound classifier.

## What is included

- Audio capture over I2S (INMP441)
- Proxy embedding generation to match the classifier input shape
- TensorFlow Lite Micro inference using the embedded 522 KB classifier model
- Status logging over Serial
- Optional battery monitoring and status LED support

## Project layout

- `src/main.cpp` - firmware entry point and capture/inference loop
- `include/config.h` - hardware and model constants
- `include/audio_capture.h` - ADC capture and window analysis
- `include/model_inference.h` - TensorFlow Lite Micro wrapper
- `src/model/heart_sound_classifier_int8.tflite` - embedded classifier binary

## Build

From this folder:

```bash
pio run
```

## Flash

```bash
pio run -t upload
```

## Monitor

```bash
pio device monitor -b 115200
```

The firmware also emits Teleplot-style `label:value` lines so you can plot the audio window and classifier outputs directly.

Useful labels include:

- `hs_rms`
- `hs_peak`
- `hs_mean`
- `hs_class`
- `hs_confidence`
- `hs_latency_ms`
- `hs_wave_00` through `hs_wave_63`

-## Hardware wiring

- Audio input: INMP441 digital microphone via I2S
  - BCLK (SCK) → GPIO14
  - WS / LRCK → GPIO9
  - SD (DOUT) → GPIO8
 - Note: firmware `src/config.h` and `src/audio_capture.h` are the authoritative source for pin mappings.
- Battery sense: GPIO1 through a divider if enabled
- Status LED: uses `LED_BUILTIN` if defined by the board package

## Notes

- The firmware uses the extracted classifier model directly from flash.
- The current on-device feature extractor is a lightweight proxy that produces 1024 floating-point values, matching the trained classifier input size.
- If you want the exact YAMNet embedding pipeline on-device, replace the proxy extractor with a YAMNet feature stage later.
