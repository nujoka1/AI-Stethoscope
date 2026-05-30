#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>

// Display initialization
void display_init();
void display_cleanup();

// Display modes
typedef enum {
    DISPLAY_IDLE,
    DISPLAY_LISTENING,
    DISPLAY_PROCESSING,
    DISPLAY_RESULT,
    DISPLAY_ERROR
} DisplayMode;

// UI update functions
void display_show_status(const char* status_text, uint16_t battery_percent);
void display_show_listening(int progress_percent);
void display_show_processing(int progress_percent);
void display_show_result(const char* classification, float confidence, float average_confidence, uint32_t inference_count);
void display_show_error(const char* error_message);
void display_clear();

// Low-level control
void display_set_brightness(uint8_t level);  // 0-255
void display_set_mode(DisplayMode mode);
DisplayMode display_get_mode();

// Debug/info display
void display_show_debug_info(const char* info);
void display_show_battery_level(uint8_t percent);

#endif // DISPLAY_UI_H
