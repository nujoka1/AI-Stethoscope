# Integration Mapping: Hackster (Viam/RPi) → This Repo (ESP32)

This file maps reusable assets from the Hackster project and outlines how to integrate ESP32 firmware with the Dashboard.

## ⚡ Quick Path (ESP32-only, No-PSRAM)

You have an **ESP32-S3 with no PSRAM**. The fastest path is:

1. **Build & flash** the firmware:
   ```bash
   source ~/yoloenv/bin/activate
   ./build_and_flash.sh esp32-s3-devkitc-1
   ```
2. **Start local MQTT broker** (Docker):
   ```bash
   docker-compose up -d
   ```
3. **Open Dashboard** and update `Dashboard/assets/js/config.js`:
   ```javascript
   window.MQTT_BROKER = 'ws://localhost:8080';
   ```
4. **Test**: Access `Dashboard/index.html` in a browser to see real-time analysis.

## Reusable Assets from Hackster

| Asset | Source | Use in This Repo |
|-------|--------|-----------------|
| Model training pipeline | `TF/Train.ipynb` | Reference for model retraining or fine-tuning |
| Class labels | `include/config.h` (`kClassNames`) | Already synced: AS, MR, MS, MVP, N |
| Dashboard UI | `Dashboard/index.html` | As-is; supports MQTT + WebSocket |
| MQTT topic structure | Hackster docs | Adopted in firmware & Dashboard (`VIAM-AI-STETH/*`) |

## Advanced: Raspberry Pi + Viam (Optional)

If you later add a **Raspberry Pi** to offload heavy processing:

1. **Install Viam + TensorFlow-CPU** on Pi (see Hackster guide).
2. **Run MQTT bridge** on Pi to forward results to Dashboard:
   ```bash
   python3 tools/mqtt_bridge.py --broker localhost --subscribe VIAM-AI-STETH/ANALYSE --publish VIAM-AI-STETH/ANALYSE
   ```
3. **Configure Dashboard** to point to Pi's MQTT broker (instead of local Docker).

## Model Formats

- **ESP32 firmware** (this repo): Uses quantized TFLite (`src/model/heart_sound_classifier_int8.tflite`) + TensorFlow Lite Micro runtime.
- **Raspberry Pi + Viam** (Hackster): Uses SavedModel or TFLite-CPU service.
- **Conversion reference**: See `TF/quantize_heart_sound_model.py` for TFLite export steps.

## Future: PSRAM Board Upgrade

When you upgrade to a **PSRAM-capable ESP32-S3**:

1. Build with larger buffers:
   ```bash
   ./build_and_flash.sh esp32-s3-devkitc-1-psram
   ```
2. This enables:
   - 5-second audio window (vs. 2 sec)
   - 192 KB tensor arena (vs. 128 KB)
   - Better model accuracy

## Key Files

- `NO-PSRAM-QUICKSTART.md` — Step-by-step for current ESP32 setup
- `build_and_flash.sh` — Build + flash automation
- `docker-compose.yml` — Local MQTT broker (testing)
- `tools/mqtt_bridge.py` — RPi↔MQTT bridge (advanced)
