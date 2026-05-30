/*
 * ESP32 Heart Sound AI Stethoscope - Main Firmware
 * ================================================
 * Real-time heart sound classification using TinyML
 * 
 * Architecture:
 *   Task 1: Audio capture (16 kHz ADC sampling)
 *   Task 2: Model inference (TFLite quantized)
 *   Task 3: Display update (OLED UI)
 *   Task 4: Power management & logging
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../include/config.h"
#include "../include/audio_capture.h"
#include "../include/model_inference.h"
#include "display_ui.h"
#include "power_management.h"

// ============================================================================
// GLOBAL STATE & COMMUNICATION
// ============================================================================

// FreeRTOS communication queues
QueueHandle_t audio_ready_queue;
QueueHandle_t inference_result_queue;
QueueHandle_t display_update_queue;

// Semaphores
SemaphoreHandle_t inference_ready;
SemaphoreHandle_t display_ready;

// Global state
struct {
    uint32_t inference_count;
    float current_confidence;
    int current_class;
    uint32_t last_inference_time_ms;
    uint16_t battery_level_percent;
    bool system_healthy;
} system_state = {0, 0.0f, -1, 0, 100, true};

struct InferenceResultMessage {
    int class_id;
    float confidence;
    uint32_t timestamp;
};

// Utility declarations
void normalize_audio_window(uint16_t *raw_data, float *normalized, int length);
void extract_features(float *audio, float *features, int audio_len);

// ============================================================================
// TASK 1: AUDIO CAPTURE (High Priority - Timer Interrupt Driven)
// ============================================================================

void audio_capture_task(void *arg) {
    const char *TAG = "AUDIO_TASK";
    
    ESP_LOGI(TAG, "Audio capture task started");
    
    if (!audioCapture().begin()) {
        ESP_LOGE(TAG, "Failed to initialize I2S audio capture");
        vTaskSuspend(NULL);
    }

    // Buffer for audio data (5 seconds at 16 kHz)
    static uint16_t audio_buffer[kAudioWindowSamples];

    ESP_LOGI(TAG, "INMP441 I2S capture ready @ %u Hz", kAudioSampleRateHz);
    
    while (1) {
        if (!audioCapture().captureWindow(audio_buffer, kAudioWindowSamples)) {
            ESP_LOGW(TAG, "I2S read failed, retrying");
            continue;
        }

        if (xQueueSend(audio_ready_queue, audio_buffer, 0) == pdPASS) {
            ESP_LOGI(TAG, "Audio buffer queued (%u samples)", kAudioWindowSamples);
        } else {
            ESP_LOGW(TAG, "Audio queue full, dropping buffer");
        }
    }
}

// ============================================================================
// TASK 2: MODEL INFERENCE (High Priority)
// ============================================================================

void inference_task(void *arg) {
    const char *TAG = "INFERENCE_TASK";
    
    ESP_LOGI(TAG, "Inference task started");
    
    HeartSoundInferenceEngine &engine = inferenceEngine();
    if (!engine.begin()) {
        ESP_LOGE(TAG, "Failed to initialize inference engine");
        vTaskSuspend(NULL);
    }
    
    // Allocate buffer for normalized audio
    float normalized_audio[kAudioWindowSamples];
    float embeddings[kFeatureDim];
    
    uint16_t audio_buffer[kAudioWindowSamples];
    
    ESP_LOGI(TAG, "Ready for inference");
    
    while (1) {
        if (xQueueReceive(audio_ready_queue, audio_buffer, portMAX_DELAY) == pdPASS) {
            uint32_t start_time = esp_timer_get_time();
            
            normalize_audio_window(audio_buffer, normalized_audio, kAudioWindowSamples);
            extract_features(normalized_audio, embeddings, kAudioWindowSamples);
            
            HeartSoundPrediction prediction;
            if (!engine.predict(embeddings, kFeatureDim, &prediction)) {
                ESP_LOGE(TAG, "Inference prediction failed");
                continue;
            }
            
            int predicted_class = prediction.class_id;
            float confidence = prediction.confidence;
            
            system_state.current_class = predicted_class;
            system_state.current_confidence = confidence;
            system_state.inference_count++;
            system_state.last_inference_time_ms = (esp_timer_get_time() - start_time) / 1000;
            
            ESP_LOGI(TAG, "Inference [%lu] result: %s (%.1f%%) in %lu ms",
                     system_state.inference_count,
                     kClassNames[predicted_class],
                     confidence * 100,
                     static_cast<unsigned long>(system_state.last_inference_time_ms));
            
            InferenceResultMessage result = {predicted_class, confidence, esp_timer_get_time()};
            xQueueSend(inference_result_queue, &result, 0);
            xSemaphoreGive(inference_ready);
        }
    }
}

// ============================================================================
// TASK 3: DISPLAY UPDATE (Medium Priority)
// ============================================================================

void display_task(void *arg) {
    const char *TAG = "DISPLAY_TASK";
    
    ESP_LOGI(TAG, "Display task started");
    
    // Initialize OLED display
    display_init();
    
    struct {
        int class_id;
        float confidence;
        uint32_t timestamp;
    } inference_result;
    
    uint32_t last_update = 0;
    int running_avg_count = 0;
    float running_avg_confidence = 0.0f;
    
    ESP_LOGI(TAG, "Display ready");
    
    while (1) {
        // Wait for inference result with timeout
        if (xQueueReceive(inference_result_queue, &inference_result, pdMS_TO_TICKS(100)) == pdPASS) {
            
            // Update running average
            running_avg_confidence = (running_avg_confidence * 0.7f) + 
                                    (inference_result.confidence * 0.3f);
            running_avg_count++;
            
            // Display current result
            display_show_result(
                kClassNames[inference_result.class_id],
                inference_result.confidence,
                running_avg_confidence,
                system_state.inference_count
            );
            
            last_update = esp_timer_get_time();
        } else {
            // No inference data, show status
            display_show_status("Listening...", system_state.battery_level_percent);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));  // Update display every 500 ms
    }
}

// ============================================================================
// TASK 4: POWER MANAGEMENT & MONITORING
// ============================================================================

void power_management_task(void *arg) {
    const char *TAG = "POWER_TASK";
    
    ESP_LOGI(TAG, "Power management task started");
    
    while (1) {
        // Read battery voltage
        system_state.battery_level_percent = power_get_battery_level();
        
        // Monitor system health
        if (system_state.inference_count > 0) {
            if (system_state.last_inference_time_ms > 1000) {
                ESP_LOGW(TAG, "Inference latency high: %.0f ms", system_state.last_inference_time_ms);
                system_state.system_healthy = false;
            } else {
                system_state.system_healthy = true;
            }
        }
        
        // Log system stats
        if (system_state.inference_count % 10 == 0 && system_state.inference_count > 0) {
            ESP_LOGI(TAG, "System stats:");
            ESP_LOGI(TAG, "  Inferences: %lu", system_state.inference_count);
            ESP_LOGI(TAG, "  Battery: %u%%", system_state.battery_level_percent);
            ESP_LOGI(TAG, "  Last latency: %.0f ms", system_state.last_inference_time_ms);
            ESP_LOGI(TAG, "  Health: %s", system_state.system_healthy ? "OK" : "WARN");
        }
        
        // Check low battery
        if (system_state.battery_level_percent < 10) {
            ESP_LOGW(TAG, "LOW BATTERY - Consider charging");
        }
        
        // Update power state
        if (system_state.inference_count == 0) {
            // No inference yet, system in idle
            ESP_LOGD(TAG, "System idle, measuring baseline power");
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
    }
}

// ============================================================================
// SYSTEM INITIALIZATION
// ============================================================================

void setup() {
    // Initialize serial for logging
    Serial.begin(115200);
    delay(1000);  // Wait for serial to stabilize
    
    ESP_LOGI("MAIN", "===========================================");
    ESP_LOGI("MAIN", "ESP32 Heart Sound AI Stethoscope");
    ESP_LOGI("MAIN", "Firmware Version: 1.0.0");
    ESP_LOGI("MAIN", "Build Date: %s %s", __DATE__, __TIME__);
    ESP_LOGI("MAIN", "===========================================");
    
    // Create communication queues
    audio_ready_queue = xQueueCreate(1, kAudioWindowSamples * sizeof(uint16_t));
    inference_result_queue = xQueueCreate(5, sizeof(InferenceResultMessage));
    display_update_queue = xQueueCreate(2, 64);
    
    // Create semaphores
    inference_ready = xSemaphoreCreateBinary();
    display_ready = xSemaphoreCreateBinary();
    
    ESP_LOGI("MAIN", "Queues and semaphores created");
    
    // Create FreeRTOS tasks
    // Task 1: Audio capture (high priority, Timer-driven)
    xTaskCreatePinnedToCore(
        audio_capture_task,      // Function
        "audio_capture",         // Name
        4096,                    // Stack size
        NULL,                    // Parameters
        3,                       // Priority (high)
        NULL,                    // Task handle
        0                        // Core 0
    );
    
    // Task 2: Model inference (high priority)
    xTaskCreatePinnedToCore(
        inference_task,
        "inference",
        8192,                    // Larger stack for TFLite
        NULL,
        2,                       // Priority
        NULL,
        1                        // Core 1 (let Core 0 handle audio)
    );
    
    // Task 3: Display update (medium priority)
    xTaskCreatePinnedToCore(
        display_task,
        "display",
        2048,
        NULL,
        1,
        NULL,
        1
    );
    
    // Task 4: Power management (low priority)
    xTaskCreatePinnedToCore(
        power_management_task,
        "power",
        2048,
        NULL,
        0,
        NULL,
        0
    );
    
    ESP_LOGI("MAIN", "All tasks created and scheduled");
    ESP_LOGI("MAIN", "System ready - start speaking into microphone");
    
    // FreeRTOS scheduler starts automatically
}

void loop() {
    // Empty - everything runs via FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(10000));
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void normalize_audio_window(uint16_t *raw_data, float *normalized, int length) {
    // Find max value for normalization
    uint16_t max_val = 0;
    for (int i = 0; i < length; i++) {
        if (raw_data[i] > max_val) max_val = raw_data[i];
    }
    
    // Normalize to [-1, 1] range
    float scale = 1.0f / (max_val + 1e-6);
    uint16_t mid = 2048;  // ADC midpoint
    
    for (int i = 0; i < length; i++) {
        normalized[i] = ((float)raw_data[i] - mid) * scale;
    }
}

void extract_features(float *audio, float *features, int audio_len) {
    // Simplified feature extraction
    // In production, this would:
    // 1. Compute MFCC or spectrogram
    // 2. Call YAMNet embedding extractor
    // 3. Return 1024-dim embeddings
    
    // For now, using statistical features + zero-padding
    float mean = 0.0f, std_dev = 0.0f;
    
    for (int i = 0; i < audio_len; i++) {
        mean += audio[i];
    }
    mean /= audio_len;
    
    for (int i = 0; i < audio_len; i++) {
        std_dev += (audio[i] - mean) * (audio[i] - mean);
    }
    std_dev = sqrt(std_dev / audio_len);
    
    // Fill embeddings with basic stats
    for (int i = 0; i < kFeatureDim; i++) {
        if (i == 0) features[i] = mean;
        else if (i == 1) features[i] = std_dev;
        else if (i < 100) features[i] = audio[(i * audio_len) / 100];
        else features[i] = 0.0f;  // Zero-pad
    }
    
    ESP_LOGD("FEATURES", "Mean: %.3f, StdDev: %.3f", mean, std_dev);
}
