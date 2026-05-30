#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/AI-Stethoscope"
  exit 1
fi

PROJECT_ROOT="$1"
SRC_DIR="$PROJECT_ROOT/ready to compile and flash/src"
ROOT_DIR="$PROJECT_ROOT/ready to compile and flash"

if [ ! -d "$PROJECT_ROOT" ]; then
  echo "ERROR: Project root does not exist: $PROJECT_ROOT"
  exit 2
fi
if [ ! -d "$ROOT_DIR" ]; then
  echo "ERROR: Firmware folder not found: $ROOT_DIR"
  exit 3
fi

mkdir -p "$SRC_DIR"

# Move files that belong in src/
for f in main.cpp audio_capture.h config.h; do
  if [ -f "$ROOT_DIR/$f" ] && [ ! -f "$SRC_DIR/$f" ]; then
    mv "$ROOT_DIR/$f" "$SRC_DIR/"
    echo "moved: $f -> src/"
  elif [ -f "$ROOT_DIR/$f" ]; then
    echo "already in root but exists in src/: $f"
  else
    echo "not found in root: $f"
  fi
done

# Ensure platformio.ini is in root
if [ ! -f "$ROOT_DIR/platformio.ini" ]; then
  echo "ERROR: platformio.ini missing from firmware root"
  exit 4
fi

# Create missing headers if they are absent
create_if_missing() {
  local path="$1"
  local content="$2"
  if [ ! -f "$path" ]; then
    printf '%s' "$content" > "$path"
    echo "created: $(basename "$path")"
  else
    echo "exists: $(basename "$path")"
  fi
}

create_if_missing "$SRC_DIR/model_inference.h" "#ifndef MODEL_INFERENCE_H\n#define MODEL_INFERENCE_H\n\n#include <TensorFlowLite_ESP32.h>\n\nextern const unsigned char heart_sound_model_tflite[];\nextern const unsigned int heart_sound_model_tflite_len;\n\n#define MODEL_INPUT_LENGTH 16000\n#define MODEL_NUM_CLASSES 4\n#define INFERENCE_THRESHOLD 0.7f\n\nenum HeartSoundClass { NORMAL = 0, MURMUR = 1, AORTIC_STENOSIS = 2, MITRAL_REGURGITATION = 3 };\n\nstruct InferenceResult {\n    HeartSoundClass classification;\n    float confidence;\n    float scores[MODEL_NUM_CLASSES];\n    unsigned long inference_time_ms;\n};\n\nbool model_init();\nInferenceResult model_infer(const int16_t* audio_data, size_t audio_len);\nvoid model_cleanup();\nconst char* get_class_name(HeartSoundClass cls);\n\n#endif // MODEL_INFERENCE_H\n"

create_if_missing "$SRC_DIR/display_ui.h" "#ifndef DISPLAY_UI_H\n#define DISPLAY_UI_H\n\n#include <Arduino.h>\n\nvoid display_init();\nvoid display_cleanup();\n\ntypedef enum { DISPLAY_IDLE, DISPLAY_LISTENING, DISPLAY_PROCESSING, DISPLAY_RESULT, DISPLAY_ERROR } DisplayMode;\n\nvoid display_show_status(const char* status_text);\nvoid display_show_listening(int progress_percent);\nvoid display_show_processing(int progress_percent);\nvoid display_show_result(const char* classification, float confidence);\nvoid display_show_error(const char* error_message);\nvoid display_clear();\n\nvoid display_set_brightness(uint8_t level);\nvoid display_set_mode(DisplayMode mode);\nDisplayMode display_get_mode();\n\nvoid display_show_debug_info(const char* info);\nvoid display_show_battery_level(uint8_t percent);\n\n#endif // DISPLAY_UI_H\n"

create_if_missing "$SRC_DIR/power_management.h" "#ifndef POWER_MANAGEMENT_H\n#define POWER_MANAGEMENT_H\n\n#include <Arduino.h>\n\ntypedef enum { POWER_MODE_ACTIVE, POWER_MODE_SLEEP, POWER_MODE_DEEP_SLEEP, POWER_MODE_HIBERNATION } PowerMode;\n\nstruct PowerStatus {\n    uint8_t battery_percent;\n    float battery_voltage;\n    bool on_usb_power;\n    bool charging;\n    PowerMode current_mode;\n    unsigned long sleep_duration_ms;\n};\n\nvoid power_init();\nvoid power_cleanup();\n\nuint8_t power_get_battery_percent();\nfloat power_get_battery_voltage();\nbool power_is_charging();\nbool power_on_external_power();\nPowerStatus power_get_status();\n\nvoid power_set_mode(PowerMode mode, unsigned long duration_ms);\nvoid power_wake_on_gpio(uint8_t gpio_pin);\nvoid power_wake_on_timer(unsigned long ms);\n\nvoid power_disable_radio(bool disable_wifi, bool disable_bt);\nvoid power_set_cpu_freq(uint32_t freq_mhz);\nvoid power_optimize_for_battery();\nvoid power_optimize_for_performance();\n\nvoid power_emergency_shutdown(unsigned long delay_ms);\n\n#endif // POWER_MANAGEMENT_H\n"

# Final verification
printf '\n=== READY TO BUILD VERIFICATION ===\n'
ls -la "$ROOT_DIR" | grep -E 'platformio.ini|\.gitignore|README\.md|\.pio|include|model|src'
printf '\n=== SRC FILES ===\n'
ls -la "$SRC_DIR" | grep -E 'main\.cpp|audio_capture\.h|config\.h|model_inference\.h|display_ui\.h|power_management\.h'
printf '\n=== MODEL FILE ===\n'
ls -lh "$ROOT_DIR/model/heart_sound_classifier_int8.tflite" 2>/dev/null || echo "Model file missing: heart_sound_classifier_int8.tflite"

printf '\nREADY TO BUILD\n'
