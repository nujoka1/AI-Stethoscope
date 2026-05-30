/*
 * Audio Capture Module
 * ====================
 * I2S digital capture at 16 kHz from INMP441 MEMS microphone
 * Implements simple sample conversion for downstream inference
 */

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include "config.h"

// ============================================================================
// I2S INITIALIZATION
// ============================================================================

void audio_capture_init() {
    const char *TAG = "AUDIO_INIT";

    ESP_LOGI(TAG, "Initializing I2S for INMP441 microphone");

    i2s_config_t i2s_config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_SAMPLE_BITS,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRCK_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver: %d", err);
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins: %d", err);
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);

    ESP_LOGI(TAG, "I2S configured: BCLK=%d WS=%d SD=%d @ %d Hz",
             static_cast<int>(I2S_BCLK_PIN),
             static_cast<int>(I2S_LRCK_PIN),
             static_cast<int>(I2S_SD_PIN),
             SAMPLE_RATE);
}

// ============================================================================
// SINGLE SAMPLE CAPTURE
// ============================================================================

uint16_t audio_capture_sample() {
    uint32_t raw_sample = 0;
    size_t bytes_read = 0;
    esp_err_t err = i2s_read(I2S_PORT, &raw_sample, sizeof(raw_sample), &bytes_read, portMAX_DELAY);

    if (err != ESP_OK || bytes_read != sizeof(raw_sample)) {
        return 2048;
    }

    // Convert 32-bit I2S sample from INMP441 to signed 24-bit PCM,
    // then map to the existing 12-bit unsigned range expected by downstream code.
    int32_t signed_sample = static_cast<int32_t>(raw_sample) >> 8;
    float normalized = static_cast<float>(signed_sample) / 8388607.0f;
    int32_t mapped = static_cast<int32_t>(normalized * 2047.0f) + 2048;
    mapped = mapped < 0 ? 0 : (mapped > 4095 ? 4095 : mapped);
    return static_cast<uint16_t>(mapped);
}

// ============================================================================
// AUDIO LEVEL MEASUREMENT
// ============================================================================

struct AudioLevel {
    uint16_t peak;          // Highest value in window
    uint16_t rms;           // Root mean square
    float normalized_rms;   // RMS normalized to 0-1
    bool is_silence;        // Is this silence?
};

AudioLevel audio_capture_get_level(uint16_t *samples, int count) {
    AudioLevel level = {0, 0, 0.0f, false};
    
    uint32_t sum_squares = 0;
    
    // Calculate RMS and peak
    for (int i = 0; i < count; i++) {
        if (samples[i] > level.peak) {
            level.peak = samples[i];
        }
        
        // RMS: (value - midpoint)^2
        int16_t centered = samples[i] - 2048;  // ADC midpoint
        sum_squares += centered * centered;
    }
    
    // RMS = sqrt(mean of squares)
    uint32_t mean_square = sum_squares / count;
    level.rms = (uint16_t)sqrt(mean_square);
    
    // Normalize RMS to 0-1 range (assuming 0-2048 is useful range)
    level.normalized_rms = (float)level.rms / 2048.0f;
    
    // Detect silence (RMS below threshold)
    if (level.normalized_rms < NOISE_GATE_THRESHOLD) {
        level.is_silence = true;
    }
    
    return level;
}

// ============================================================================
// SIMPLE LOW-PASS FILTER (IIR)
// ============================================================================

class SimpleFilter {
private:
    float last_output;
    float alpha;  // Filter coefficient (0-1, smaller = more filtering)
    
public:
    SimpleFilter(float filter_alpha = 0.3f) : last_output(0.0f), alpha(filter_alpha) {}
    
    uint16_t filter(uint16_t input) {
        // y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
        last_output = (alpha * input) + ((1.0f - alpha) * last_output);
        return (uint16_t)last_output;
    }
    
    void reset() {
        last_output = 0.0f;
    }
};

// ============================================================================
// RING BUFFER FOR AUDIO DATA
// ============================================================================

class AudioRingBuffer {
private:
    uint16_t *buffer;
    int buffer_size;
    volatile int write_pos;
    volatile int read_pos;
    
public:
    AudioRingBuffer(int size) : buffer_size(size), write_pos(0), read_pos(0) {
        buffer = (uint16_t *)malloc(size * sizeof(uint16_t));
        if (!buffer) {
            ESP_LOGE("RING_BUF", "Failed to allocate ring buffer");
        }
    }
    
    ~AudioRingBuffer() {
        if (buffer) free(buffer);
    }
    
    void write(uint16_t sample) {
        buffer[write_pos] = sample;
        write_pos = (write_pos + 1) % buffer_size;
        
        // Prevent write from catching up to read
        if (write_pos == read_pos) {
            read_pos = (read_pos + 1) % buffer_size;  // Drop oldest
        }
    }
    
    int available() {
        if (write_pos >= read_pos) {
            return write_pos - read_pos;
        } else {
            return buffer_size - (read_pos - write_pos);
        }
    }
    
    int read_window(uint16_t *output, int length) {
        int copied = 0;
        
        for (int i = 0; i < length; i++) {
            if (read_pos == write_pos) break;  // Buffer empty
            
            output[i] = buffer[read_pos];
            read_pos = (read_pos + 1) % buffer_size;
            copied++;
        }
        
        return copied;
    }
    
    void clear() {
        write_pos = 0;
        read_pos = 0;
    }
};

// ============================================================================
// BATCH CAPTURE WITH NOISE REDUCTION
// ============================================================================

int audio_capture_batch(uint16_t *output_buffer, int num_samples, 
                        SimpleFilter *filter = nullptr) {
    static AudioRingBuffer *ring_buffer = nullptr;
    
    // Initialize ring buffer on first call
    if (!ring_buffer) {
        ring_buffer = new AudioRingBuffer(AUDIO_BUFFER_SIZE);
    }
    
    // Capture samples into ring buffer
    for (int i = 0; i < num_samples; i++) {
        uint16_t raw_sample = audio_capture_sample();
        
        // Apply optional filter
        uint16_t filtered_sample = filter ? filter->filter(raw_sample) : raw_sample;
        
        ring_buffer->write(filtered_sample);
        
        // Precise timing for 16 kHz sampling
        delayMicroseconds(62);  // 1/16000 = 62.5 microseconds
    }
    
    // Read samples from buffer
    return ring_buffer->read_window(output_buffer, num_samples);
}

// ============================================================================
// AUDIO STATISTICS
// ============================================================================

struct AudioStats {
    float mean;
    float std_dev;
    float peak;
    float rms;
    bool is_clipping;  // If peak near max (4095)
};

AudioStats audio_capture_analyze(uint16_t *samples, int count) {
    AudioStats stats = {0.0f, 0.0f, 0.0f, 0.0f, false};
    
    // Calculate mean
    uint32_t sum = 0;
    uint16_t max_val = 0;
    for (int i = 0; i < count; i++) {
        sum += samples[i];
        if (samples[i] > max_val) max_val = samples[i];
    }
    stats.mean = (float)sum / count;
    stats.peak = (float)max_val;
    
    // Calculate standard deviation
    uint32_t sum_sq_diff = 0;
    for (int i = 0; i < count; i++) {
        float diff = samples[i] - stats.mean;
        sum_sq_diff += diff * diff;
    }
    stats.std_dev = sqrt((float)sum_sq_diff / count);
    
    // Calculate RMS
    uint32_t sum_sq = 0;
    for (int i = 0; i < count; i++) {
        int16_t centered = samples[i] - 2048;
        sum_sq += centered * centered;
    }
    stats.rms = sqrt((float)sum_sq / count);
    
    // Check for clipping (peak near max)
    if (max_val > 4000) {
        stats.is_clipping = true;
    }
    
    return stats;
}

// ============================================================================
// AUDIO LEVEL METER (for real-time feedback)
// ============================================================================

void audio_capture_print_level(uint16_t *samples, int count) {
    AudioStats stats = audio_capture_analyze(samples, count);
    
    // Create visual level meter
    int bar_length = (int)(stats.rms / 20.48f);  // Scale to ~0-200
    bar_length = constrain(bar_length, 0, 20);
    
    Serial.print("[LEVEL] ");
    for (int i = 0; i < bar_length; i++) Serial.print("█");
    for (int i = bar_length; i < 20; i++) Serial.print("░");
    
    Serial.printf(" RMS: %.1f | Peak: %.0f | Clip: %s\n", 
                  stats.rms, stats.peak, stats.is_clipping ? "YES" : "NO");
}

#endif // AUDIO_CAPTURE_H
