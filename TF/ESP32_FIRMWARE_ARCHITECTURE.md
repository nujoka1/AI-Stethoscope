# ESP32 Heart Sound AI Stethoscope - Firmware Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│               ESP32 FIRMWARE ARCHITECTURE                     │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────┐      ┌──────────────┐     ┌────────────┐  │
│  │   ADC Task  │──→   │ Audio Buffer │──→  │  TFLite    │  │
│  │  (FreeRTOS) │      │  (Ring Buf)  │     │ Inference  │  │
│  └─────────────┘      └──────────────┘     └────────────┘  │
│         ↑                                          ↓          │
│   3.5mm Audio                                Output:         │
│   Input                                    Confidence %      │
│   16 kHz                                                    │
│   16-bit                               ┌──────────────┐     │
│                                        │  OLED UI     │     │
│                                        │  Display     │     │
│                                        └──────────────┘     │
│                                                               │
│  FreeRTOS Tasks:                                            │
│  - Task 1: ADC Sampling (16 kHz)                           │
│  - Task 2: TFLite Inference                                │
│  - Task 3: Display Update (2 Hz)                           │
│  - Task 4: Power Management / Logging                      │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Hardware Pinout (ESP32-S3)

### Audio Input (ADC)
```
3.5mm Jack (Mono)
├─ Tip (Left/Audio) → ESP32 GPIO 2 (ADC1_CH1) via 1kΩ resistor
├─ Sleeve (GND) → ESP32 GND
└─ Ring (unused) → leave floating or connect to GND

Protection Circuit:
    3.5mm_in ──[1kΩ]──┬──→ GPIO 2 (ADC)
                      │
                    [100nF cap to GND]
```

### Display (OLED SSD1306)
```
I2C Interface:
├─ SDA → GPIO 21 (I2C SDA)
├─ SCL → GPIO 22 (I2C SCL)
├─ VCC → 3.3V
└─ GND → GND
```

### Power Management
```
Battery (3.7V Li-ion)
├─ [TP4056 Charger Module]
│  └─ OUT+ → LDO/Power Distribution
│  └─ OUT- → GND
└─ [Optional: Voltage Divider for ADC monitoring]
   ├─ 3.7V ──[100kΩ]──┬──→ GPIO 1 (ADC input)
   │                  │
   │                [51kΩ]
   │                  │
   │                 GND
```

### Optional: Status LED
```
Status Indicator:
├─ GPIO 25 → LED via 330Ω resistor → GND
│  (indicates inference running)
```

---

## Software Architecture

### Module Structure

```
esp32_stethoscope/
├── main.cpp                  # Entry point, task scheduler
├── audio_capture.h/cpp       # ADC sampling task
├── audio_buffer.h/cpp        # Ring buffer implementation
├── model_inference.h/cpp     # TFLite inference wrapper
├── model_data.h              # Quantized model (auto-generated)
├── display_ui.h/cpp          # OLED display driver
├── power_management.h/cpp    # Battery monitoring
├── config.h                  # Global configuration
├── platformio.ini            # PlatformIO build config
└── README.md
```

---

## Core Module Specifications

### 1. Audio Capture (audio_capture.cpp)

**Purpose:** Continuous ADC sampling at 16 kHz

**Implementation:**
```c
// Sampling Configuration
#define SAMPLE_RATE 16000
#define ADC_RESOLUTION 12  // bits
#define ADC_CHANNEL ADC1_CHANNEL_1  // GPIO 2
#define BUFFER_SIZE (16000 * 5)  // 5 seconds

// FreeRTOS Task
void audio_capture_task(void *arg) {
    uint16_t adc_buffer[BUFFER_SIZE];
    int buf_index = 0;
    
    // Configure ADC
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_12);
    
    while (1) {
        // Read ADC sample every 1/16000 = 62.5 microseconds
        uint16_t sample = adc1_get_raw(ADC_CHANNEL);
        
        // Simple low-pass filter (optional)
        sample = (sample * 0.8) + (last_sample * 0.2);
        last_sample = sample;
        
        adc_buffer[buf_index++] = sample;
        
        // Trigger inference every 5 seconds
        if (buf_index >= BUFFER_SIZE) {
            xQueueSend(audio_queue, adc_buffer, 0);
            buf_index = 0;
        }
        
        // Timing: use hardware timer or precise delay
        vTaskDelay(pdMS_TO_TICKS(1) / 16);  // ~62.5us
    }
}

// Public API
void audio_capture_init();
void audio_capture_start();
void audio_capture_stop();
int audio_capture_get_level();  // Return current audio level %
```

**Key Considerations:**
- **Sampling precision:** Use timer interrupt for exact 16 kHz timing
- **Buffering:** Ring buffer to prevent overwriting during inference
- **Filtering:** Optional software filters (median, exponential moving average)
- **Power:** ADC in continuous mode vs low-power sampling

---

### 2. Audio Buffer (audio_buffer.cpp)

**Purpose:** Thread-safe circular buffer for audio data

**Implementation:**
```c
#define RING_BUFFER_SIZE (16000 * 5 * 2)  // 10 seconds total

typedef struct {
    uint16_t data[RING_BUFFER_SIZE];
    volatile int write_index;
    volatile int read_index;
    SemaphoreHandle_t semaphore;
} RingBuffer;

// Write from ADC task
void ring_buffer_write(RingBuffer *buf, uint16_t sample) {
    buf->data[buf->write_index] = sample;
    buf->write_index = (buf->write_index + 1) % RING_BUFFER_SIZE;
}

// Read for preprocessing & inference
int ring_buffer_read_window(RingBuffer *buf, uint16_t *output, int length) {
    // Ensure read pointer doesn't catch write pointer
    for (int i = 0; i < length; i++) {
        output[i] = buf->data[buf->read_index];
        buf->read_index = (buf->read_index + 1) % RING_BUFFER_SIZE;
    }
    return length;
}

// Convert raw ADC values to normalized float [-1, 1]
void normalize_audio_window(uint16_t *raw_data, float *normalized, int length) {
    uint16_t max_val = 0;
    
    // Find max value
    for (int i = 0; i < length; i++) {
        if (raw_data[i] > max_val) max_val = raw_data[i];
    }
    
    // Normalize
    float scale = 1.0f / (max_val + 1e-6);
    for (int i = 0; i < length; i++) {
        normalized[i] = (raw_data[i] - 2048) * scale;  // Subtract ADC offset
    }
}
```

---

### 3. Model Inference (model_inference.cpp)

**Purpose:** TFLite inference execution with I/O handling

**Implementation:**
```c
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.h"  // Auto-generated quantized model

#define TENSOR_ARENA_SIZE 4096  // Bytes for inference workspace
uint8_t tensor_arena[TENSOR_ARENA_SIZE];

typedef struct {
    tflite::MicroInterpreter *interpreter;
    TfLiteTensor *input;
    TfLiteTensor *output;
    float inference_time_ms;
} InferenceEngine;

// Initialize TFLite interpreter
InferenceEngine* inference_init() {
    InferenceEngine *engine = (InferenceEngine*)malloc(sizeof(InferenceEngine));
    
    // Load model
    const tflite::Model* model = tflite::GetModel(heart_sound_model_quantized);
    
    // Create resolver with required ops
    static tflite::MicroMutableOpResolver<4> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();
    resolver.AddQuantize();
    
    // Create interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, TENSOR_ARENA_SIZE
    );
    engine->interpreter = &static_interpreter;
    
    // Allocate tensors
    if (engine->interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE("INFERENCE", "AllocateTensors() failed");
        return NULL;
    }
    
    engine->input = engine->interpreter->input(0);
    engine->output = engine->interpreter->output(0);
    
    ESP_LOGI("INFERENCE", "Interpreter initialized successfully");
    return engine;
}

// Run inference
void inference_run(InferenceEngine *engine, float *input_data) {
    // Copy input to model (quantize if needed)
    float *input_ptr = (float *)engine->input->data.f;
    for (int i = 0; i < 1024; i++) {  // 1024 dim embeddings
        input_ptr[i] = input_data[i];
    }
    
    // Record start time
    uint32_t start_ms = esp_timer_get_time() / 1000;
    
    // Execute inference
    if (engine->interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE("INFERENCE", "Invoke() failed");
        return;
    }
    
    engine->inference_time_ms = (esp_timer_get_time() / 1000) - start_ms;
    ESP_LOGI("INFERENCE", "Inference completed in %.1f ms", engine->inference_time_ms);
}

// Get output probabilities
void inference_get_output(InferenceEngine *engine, float *output_probs, int num_classes) {
    TfLiteTensor *output = engine->output;
    
    if (output->type == kTfLiteFloat32) {
        memcpy(output_probs, output->data.f, num_classes * sizeof(float));
    } else if (output->type == kTfLiteInt8) {
        // Dequantize int8 output
        float scale = output->params.scale;
        int zero_point = output->params.zero_point;
        
        int8_t *output_data = (int8_t *)output->data.raw;
        for (int i = 0; i < num_classes; i++) {
            output_probs[i] = scale * (output_data[i] - zero_point);
        }
    }
}

// Get predicted class
int inference_get_prediction(float *probs, int num_classes) {
    int max_class = 0;
    float max_prob = probs[0];
    
    for (int i = 1; i < num_classes; i++) {
        if (probs[i] > max_prob) {
            max_prob = probs[i];
            max_class = i;
        }
    }
    
    return max_class;
}
```

---

### 4. Display UI (display_ui.cpp)

**Purpose:** Real-time visualization on OLED (SSD1306)

**Implementation:**
```c
#include "U8g2lib.h"  // Library for SSD1306

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    /* clock=*/22,  // SCL
    /* data=*/21    // SDA
);

void display_init() {
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    
    // Show startup message
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "ESP32 Heart Sound");
    u8g2.drawStr(0, 30, "AI Stethoscope");
    u8g2.drawStr(0, 50, "Initializing...");
    u8g2.sendBuffer();
}

void display_show_listening(int progress) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "Listening...");
    
    // Draw progress bar
    int bar_width = (progress * 100) / 100;
    u8g2.drawBox(0, 25, bar_width, 8);
    u8g2.drawFrame(0, 25, 128, 8);
    
    u8g2.setFont(u8g2_font_7x10_tf);
    char buf[16];
    snprintf(buf, 16, "%d%%", progress);
    u8g2.drawStr(110, 35, buf);
    
    u8g2.sendBuffer();
}

void display_show_result(const char *class_name, float confidence) {
    u8g2.clearBuffer();
    
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 12, "Result:");
    
    // Class prediction
    char class_buf[20];
    snprintf(class_buf, 20, "%-6s", class_name);
    u8g2.drawBox(0, 20, 128, 18);
    u8g2.setDrawColor(0);  // Inverse text
    u8g2.drawStr(5, 33, class_buf);
    u8g2.setDrawColor(1);  // Normal text
    
    // Confidence bar
    u8g2.setFont(u8g2_font_7x10_tf);
    u8g2.drawStr(0, 50, "Confidence:");
    
    int bar_width = (confidence * 100);
    u8g2.drawBox(0, 55, bar_width, 6);
    u8g2.drawFrame(0, 55, 100, 6);
    
    char conf_buf[8];
    snprintf(conf_buf, 8, "%.1f%%", confidence * 100);
    u8g2.drawStr(105, 63, conf_buf);
    
    u8g2.sendBuffer();
}

void display_show_error(const char *msg) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "ERROR");
    u8g2.setFont(u8g2_font_7x10_tf);
    u8g2.drawStr(0, 30, msg);
    u8g2.sendBuffer();
}
```

---

### 5. Main Loop (main.cpp)

**Purpose:** Task orchestration and system initialization

**Implementation:**
```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define AUDIO_TASK_PRIORITY 2
#define INFERENCE_TASK_PRIORITY 1
#define DISPLAY_TASK_PRIORITY 0

static const char *TAG = "MAIN";

QueueHandle_t audio_queue;
SemaphoreHandle_t inference_ready;

void inference_task(void *arg) {
    InferenceEngine *engine = (InferenceEngine *)arg;
    float input_embeddings[1024];
    float output_probs[5];
    
    while (1) {
        // Wait for audio buffer to be ready
        if (xQueueReceive(audio_queue, input_embeddings, portMAX_DELAY)) {
            
            // Run inference
            inference_run(engine, input_embeddings);
            inference_get_output(engine, output_probs, 5);
            
            int predicted_class = inference_get_prediction(output_probs, 5);
            float confidence = output_probs[predicted_class];
            
            ESP_LOGI(TAG, "Predicted: %s (%.2f%%)", 
                CLASS_NAMES[predicted_class], confidence * 100);
            
            // Signal display task
            xSemaphoreGive(inference_ready);
        }
    }
}

void display_task(void *arg) {
    const TickType_t xDelay = pdMS_TO_TICKS(500);
    
    while (1) {
        if (xSemaphoreTake(inference_ready, xDelay)) {
            // Update display with latest inference results
            display_show_result(CLASS_NAMES[current_class], current_confidence);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32 Heart Sound AI Stethoscope");
    ESP_LOGI(TAG, "Firmware Version: 1.0.0");
    
    // Initialize
    display_init();
    audio_capture_init();
    InferenceEngine *engine = inference_init();
    
    // Create queues & semaphores
    audio_queue = xQueueCreate(1, 1024 * sizeof(float));
    inference_ready = xSemaphoreCreateBinary();
    
    // Create tasks
    xTaskCreate(audio_capture_task, "audio_capture",
               4096, NULL, AUDIO_TASK_PRIORITY, NULL);
    
    xTaskCreate(inference_task, "inference",
               8192, engine, INFERENCE_TASK_PRIORITY, NULL);
    
    xTaskCreate(display_task, "display",
               2048, NULL, DISPLAY_TASK_PRIORITY, NULL);
    
    // Start scheduler
    vTaskStartScheduler();
}
```

---

## Build & Deployment

### PlatformIO Configuration (platformio.ini)

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

lib_deps =
    ArduinoJson
    U8g2
    tensorflow-lite-micro
    
build_flags =
    -DARDUINO_LOG_LEVEL=4
    -DTF_LITE_MICRO_OPTIMIZED
    
upload_speed = 460800
```

### Compilation & Flashing

```bash
# Build
platformio run -e esp32-s3-devkitc-1

# Upload
platformio run -e esp32-s3-devkitc-1 -t upload

# Monitor serial output
platformio device monitor -e esp32-s3-devkitc-1
```

---

## Performance Targets

| Metric | Target | Comments |
|--------|--------|----------|
| **Audio Sampling** | 16 kHz ±0.1% | Hardware timer required |
| **Inference Latency** | <500 ms | For 5-sec window |
| **Memory Usage** | <320 KB heap | INT8 quantized model |
| **Flash Used** | <2 MB | Model + firmware |
| **Power (Active)** | ~200 mW | ADC + inference + display |
| **Power (Idle)** | <50 mW | With sleep modes |
| **Accuracy** | >85% | On test dataset |

---

## Debugging & Logging

### Serial Output Example

```
[MAIN] ESP32 Heart Sound AI Stethoscope
[AUDIO] ADC initialized on GPIO 2
[AUDIO] Sampling at 16 kHz
[INFERENCE] Interpreter initialized successfully
[MAIN] Starting inference task...
[INFERENCE] Audio window collected (80000 samples)
[INFERENCE] Predicted: N (0.94)
[DISPLAY] Updating UI...
[INFERENCE] Inference completed in 234.5 ms
```

### Enable Debug Logging

```c
// In config.h
#define DEBUG_LEVEL 3  // 0=OFF, 1=ERROR, 2=INFO, 3=DEBUG

#if DEBUG_LEVEL >= 3
#define DEBUG_PRINT(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) 
#endif
```

---

## Next Steps

1. **Day 2-3:** Implement core modules (audio capture, model inference)
2. **Day 4:** Integration & testing
3. **Day 5:** Display UI & logging
4. **Day 6:** Optimization & validation
5. **Day 7:** Documentation & deployment guide

---

**Document:** ESP32 Firmware Architecture v1.0  
**Date:** May 21, 2026  
**Status:** Ready for implementation
