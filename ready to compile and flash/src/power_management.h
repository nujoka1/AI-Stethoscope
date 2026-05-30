#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>

// Power modes
typedef enum {
    POWER_MODE_ACTIVE,
    POWER_MODE_SLEEP,
    POWER_MODE_DEEP_SLEEP,
    POWER_MODE_HIBERNATION
} PowerMode;

// Battery & power status
struct PowerStatus {
    uint8_t battery_percent;
    float battery_voltage;
    bool on_usb_power;
    bool charging;
    PowerMode current_mode;
    unsigned long sleep_duration_ms;
};

// Power management functions
void power_init();
void power_cleanup();

// Battery monitoring
uint8_t power_get_battery_percent();
uint8_t power_get_battery_level();
float power_get_battery_voltage();
bool power_is_charging();
bool power_on_external_power();
PowerStatus power_get_status();

// Power modes
void power_set_mode(PowerMode mode, unsigned long duration_ms = 0);
void power_wake_on_gpio(uint8_t gpio_pin);
void power_wake_on_timer(unsigned long ms);

// Low power optimizations
void power_disable_radio(bool disable_wifi = true, bool disable_bt = true);
void power_set_cpu_freq(uint32_t freq_mhz);  // 80, 160, 240 MHz
void power_optimize_for_battery();
void power_optimize_for_performance();

// Emergency shutdown
void power_emergency_shutdown(unsigned long delay_ms = 1000);

#endif // POWER_MANAGEMENT_H
