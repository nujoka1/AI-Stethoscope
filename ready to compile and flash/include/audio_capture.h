#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>
#include "config.h"

class AudioCapture {
public:
    bool begin() {
        if (initialized_) {
            return true;
        }

        i2s_config_t config = {
            .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = kAudioSampleRateHz,
            .bits_per_sample = kI2sSampleBits,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = kI2sDmaBufCount,
            .dma_buf_len = kI2sDmaBufLen,
            .use_apll = false,
            .tx_desc_auto_clear = false,
            .fixed_mclk = 0
        };

        i2s_pin_config_t pin_config = {
            .bck_io_num = kI2sBclkPin,
            .ws_io_num = kI2sLrckPin,
            .data_out_num = I2S_PIN_NO_CHANGE,
            .data_in_num = kI2sSdPin
        };

        esp_err_t err = i2s_driver_install(kI2sPort, &config, 0, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install I2S driver: %d", err);
            return false;
        }

        err = i2s_set_pin(kI2sPort, &pin_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set I2S pins: %d", err);
            return false;
        }

        i2s_zero_dma_buffer(kI2sPort);

        initialized_ = true;
        ESP_LOGI(TAG, "I2S capture ready: %d Hz on INMP441", kAudioSampleRateHz);
        return true;
    }

    bool captureWindow(uint16_t* output, size_t count) {
        if (!initialized_ || output == nullptr || count == 0) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            uint32_t raw_sample = 0;
            size_t bytes_read = 0;
            esp_err_t err = i2s_read(kI2sPort, &raw_sample, sizeof(raw_sample), &bytes_read, portMAX_DELAY);
            if (err != ESP_OK || bytes_read != sizeof(raw_sample)) {
                return false;
            }

            int32_t signed_sample = static_cast<int32_t>(raw_sample) >> 8;
            float normalized = static_cast<float>(signed_sample) / 8388607.0f;
            int32_t mapped = static_cast<int32_t>(normalized * 2047.0f) + 2048;
            mapped = mapped < 0 ? 0 : (mapped > 4095 ? 4095 : mapped);
            output[i] = static_cast<uint16_t>(mapped);
        }

        return true;
    }

    struct WindowStats {
        float mean = 0.0f;
        float rms = 0.0f;
        uint16_t peak = 0;
        bool silence = false;
    };

    WindowStats analyze(const uint16_t* samples, size_t count) const {
        WindowStats stats;
        if (samples == nullptr || count == 0) {
            return stats;
        }

        double sum = 0.0;
        double sum_squares = 0.0;
        uint16_t peak = 0;

        for (size_t i = 0; i < count; ++i) {
            const uint16_t value = samples[i];
            if (value > peak) {
                peak = value;
            }

            const float centered = static_cast<float>(value) - 2048.0f;
            sum += centered;
            sum_squares += static_cast<double>(centered) * static_cast<double>(centered);
        }

        stats.mean = static_cast<float>(sum / static_cast<double>(count));
        stats.rms = sqrtf(static_cast<float>(sum_squares / static_cast<double>(count))) / 2048.0f;
        stats.peak = peak;
        stats.silence = stats.rms < 0.01f;
        return stats;
    }

private:
    static constexpr const char* TAG = "AUDIO";
    bool initialized_ = false;
};

inline AudioCapture& audioCapture() {
    static AudioCapture instance;
    return instance;
}
