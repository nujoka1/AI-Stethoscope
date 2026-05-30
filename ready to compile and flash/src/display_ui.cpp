#include "display_ui.h"
#include <Arduino.h>

void display_init() {
    Serial.println("[DISPLAY] init");
}

void display_cleanup() {
    Serial.println("[DISPLAY] cleanup");
}

void display_show_status(const char* status_text, uint16_t battery_percent) {
    Serial.printf("[DISPLAY] Status: %s | Battery: %u%%\n", status_text, battery_percent);
}

void display_show_listening(int progress_percent) {
    Serial.printf("[DISPLAY] Listening... %d%%\n", progress_percent);
}

void display_show_processing(int progress_percent) {
    Serial.printf("[DISPLAY] Processing... %d%%\n", progress_percent);
}

void display_show_result(const char* classification, float confidence, float average_confidence, uint32_t inference_count) {
    Serial.printf("[DISPLAY] Result: %s (%.1f%%), avg %.1f%%, count %lu\n",
                  classification,
                  confidence * 100.0f,
                  average_confidence * 100.0f,
                  inference_count);
}

void display_show_error(const char* error_message) {
    Serial.printf("[DISPLAY] Error: %s\n", error_message);
}

void display_clear() {
    Serial.println("[DISPLAY] Clear screen");
}

void display_set_brightness(uint8_t level) {
    Serial.printf("[DISPLAY] Brightness: %u\n", level);
}

void display_set_mode(DisplayMode mode) {
    Serial.printf("[DISPLAY] Mode: %d\n", mode);
}

DisplayMode display_get_mode() {
    return DISPLAY_IDLE;
}

void display_show_debug_info(const char* info) {
    Serial.printf("[DISPLAY] Debug: %s\n", info);
}

void display_show_battery_level(uint8_t percent) {
    Serial.printf("[DISPLAY] Battery level: %u%%\n", percent);
}
