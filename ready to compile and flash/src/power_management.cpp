#include "power_management.h"
#include <Arduino.h>

void power_init() {
    Serial.println("[POWER] init");
}

void power_cleanup() {
    Serial.println("[POWER] cleanup");
}

uint8_t power_get_battery_percent() {
    return 100;
}

uint8_t power_get_battery_level() {
    return power_get_battery_percent();
}

float power_get_battery_voltage() {
    return 4.0f;
}

bool power_is_charging() {
    return false;
}

bool power_on_external_power() {
    return false;
}

PowerStatus power_get_status() {
    PowerStatus status;
    status.battery_percent = power_get_battery_percent();
    status.battery_voltage = power_get_battery_voltage();
    status.on_usb_power = power_on_external_power();
    status.charging = power_is_charging();
    status.current_mode = POWER_MODE_ACTIVE;
    status.sleep_duration_ms = 0;
    return status;
}

void power_set_mode(PowerMode mode, unsigned long duration_ms) {
    Serial.printf("[POWER] set mode %d for %lu ms\n", mode, duration_ms);
}

void power_wake_on_gpio(uint8_t gpio_pin) {
    Serial.printf("[POWER] wake on GPIO %u\n", gpio_pin);
}

void power_wake_on_timer(unsigned long ms) {
    Serial.printf("[POWER] wake on timer %lu ms\n", ms);
}

void power_disable_radio(bool disable_wifi, bool disable_bt) {
    Serial.printf("[POWER] disable radio WiFi=%d BT=%d\n", disable_wifi, disable_bt);
}

void power_set_cpu_freq(uint32_t freq_mhz) {
    Serial.printf("[POWER] CPU freq set to %u MHz\n", freq_mhz);
}

void power_optimize_for_battery() {
    Serial.println("[POWER] optimize for battery");
}

void power_optimize_for_performance() {
    Serial.println("[POWER] optimize for performance");
}

void power_emergency_shutdown(unsigned long delay_ms) {
    Serial.printf("[POWER] emergency shutdown in %lu ms\n", delay_ms);
}
