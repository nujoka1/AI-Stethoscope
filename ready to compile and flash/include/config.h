#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

// Hardware profile
#define I2S_PORT                I2S_NUM_0
#define I2S_BCLK_PIN            26   // BCLK
#define I2S_LRCK_PIN            25   // WS / LRCK
#define I2S_SD_PIN              35   // SD
#define I2S_SAMPLE_BITS         I2S_BITS_PER_SAMPLE_32BIT
#define I2S_DMA_BUF_COUNT       4
#define I2S_DMA_BUF_LEN         512

#ifndef LED_BUILTIN
#define LED_BUILTIN -1
#endif

constexpr int kStatusLedPin = LED_BUILTIN;
constexpr int kBatteryAdcPin = 1;
constexpr float kBatteryDividerRatio = 2.0f;
constexpr bool kEnableBatteryMonitor = false;

constexpr i2s_port_t kI2sPort = I2S_PORT;
constexpr int kI2sBclkPin = I2S_BCLK_PIN;
constexpr int kI2sLrckPin = I2S_LRCK_PIN;
constexpr int kI2sSdPin = I2S_SD_PIN;
constexpr i2s_bits_per_sample_t kI2sSampleBits = static_cast<i2s_bits_per_sample_t>(I2S_SAMPLE_BITS);
constexpr int kI2sDmaBufCount = I2S_DMA_BUF_COUNT;
constexpr int kI2sDmaBufLen = I2S_DMA_BUF_LEN;

// Audio and feature sizing
constexpr uint32_t kAudioSampleRateHz = 16000;
// Audio window and tensor arena sizes change depending on PSRAM availability.
// Define `CONFIG_USE_PSRAM` in build flags to enable PSRAM-sized buffers.
#ifdef CONFIG_USE_PSRAM
// PSRAM-enabled settings (larger window/arena for higher accuracy)
constexpr uint32_t kAudioWindowSeconds = 5;
constexpr size_t kAudioWindowSamples = kAudioSampleRateHz * kAudioWindowSeconds;
constexpr size_t kFeatureDim = 1024;
// Larger tensor arena when PSRAM is available
constexpr size_t kTensorArenaSize = 192 * 1024;
#else
// No-PSRAM defaults — lower memory footprint to avoid DRAM overflow
constexpr uint32_t kAudioWindowSeconds = 2;
constexpr size_t kAudioWindowSamples = kAudioSampleRateHz * kAudioWindowSeconds;
constexpr size_t kFeatureDim = 1024;
// Smaller tensor arena to reduce .bss pressure; adjust if model/accuracy requires it
constexpr size_t kTensorArenaSize = 128 * 1024;
#endif

// Teleplot-style serial output
constexpr bool kEnableTeleplotOutput = true;
constexpr size_t kTeleplotWaveformPoints = 64;

// Model and runtime
constexpr size_t kClassCount = 5;
constexpr uint32_t kCaptureInterWindowDelayMs = 150;
constexpr uint32_t kLowBatteryWarningPercent = 10;
constexpr uint32_t kLowBatteryShutdownPercent = 5;

inline constexpr const char* kClassNames[kClassCount] = {
    "AS",
    "MR",
    "MS",
    "MVP",
    "N",
};
