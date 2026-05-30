#pragma once

#include <Arduino.h>
#include <cmath>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/micro_error_reporter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "config.h"

extern const unsigned char heart_sound_classifier_int8_tflite_start[] asm("_binary_src_model_heart_sound_classifier_int8_tflite_start");
extern const unsigned char heart_sound_classifier_int8_tflite_end[] asm("_binary_src_model_heart_sound_classifier_int8_tflite_end");

struct HeartSoundPrediction {
    int class_id = -1;
    float confidence = 0.0f;
    float scores[kClassCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

class HeartSoundInferenceEngine {
public:
    bool begin() {
        if (initialized_) {
            return true;
        }

        model_ = tflite::GetModel(heart_sound_classifier_int8_tflite_start);
        if (model_ == nullptr) {
            ESP_LOGE(TAG, "Failed to load embedded TFLite model");
            return false;
        }

        if (model_->version() != TFLITE_SCHEMA_VERSION) {
            ESP_LOGE(TAG, "TFLite schema mismatch: model=%d runtime=%d", model_->version(), TFLITE_SCHEMA_VERSION);
            return false;
        }

        resolver_.AddQuantize();
        resolver_.AddFullyConnected();
        resolver_.AddDequantize();

        interpreter_ = new tflite::MicroInterpreter(
            model_,
            resolver_,
            tensor_arena_,
            kTensorArenaSize,
            &error_reporter_);

        if (interpreter_ == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate interpreter");
            return false;
        }

        if (interpreter_->AllocateTensors() != kTfLiteOk) {
            ESP_LOGE(TAG, "AllocateTensors() failed");
            return false;
        }

        input_ = interpreter_->input(0);
        output_ = interpreter_->output(0);
        if (input_ == nullptr || output_ == nullptr) {
            ESP_LOGE(TAG, "Missing input/output tensors");
            return false;
        }

        input_features_ = tensorElementCount(*input_);
        if (input_features_ == 0) {
            ESP_LOGE(TAG, "Input tensor has zero elements");
            return false;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "Inference engine ready. Input elements: %u", static_cast<unsigned>(input_features_));
        return true;
    }

    size_t inputFeatures() const {
        return input_features_;
    }

    bool predict(const float* features, size_t feature_count, HeartSoundPrediction* prediction) {
        if (!initialized_ || features == nullptr || prediction == nullptr) {
            return false;
        }

        if (!writeInput(features, feature_count)) {
            return false;
        }

        if (interpreter_->Invoke() != kTfLiteOk) {
            ESP_LOGE(TAG, "Model invocation failed");
            return false;
        }

        if (!readOutput(prediction)) {
            return false;
        }

        return true;
    }

private:
    static constexpr const char* TAG = "INFER";
    static constexpr size_t kArenaAlignment = 16;

    static size_t tensorElementCount(const TfLiteTensor& tensor) {
        if (tensor.dims == nullptr) {
            return 0;
        }

        size_t count = 1;
        for (int i = 0; i < tensor.dims->size; ++i) {
            count *= static_cast<size_t>(tensor.dims->data[i]);
        }
        return count;
    }

    bool writeInput(const float* features, size_t feature_count) {
        const size_t count = input_features_ < feature_count ? input_features_ : feature_count;

        if (input_->type == kTfLiteFloat32) {
            float* dst = input_->data.f;
            for (size_t i = 0; i < count; ++i) {
                dst[i] = features[i];
            }
            for (size_t i = count; i < input_features_; ++i) {
                dst[i] = 0.0f;
            }
            return true;
        }

        if (input_->type == kTfLiteInt8) {
            const float scale = input_->params.scale == 0.0f ? 1.0f : input_->params.scale;
            const int zero_point = input_->params.zero_point;
            int8_t* dst = input_->data.int8;
            for (size_t i = 0; i < count; ++i) {
                int32_t q = static_cast<int32_t>(lroundf(features[i] / scale)) + zero_point;
                if (q < -128) q = -128;
                if (q > 127) q = 127;
                dst[i] = static_cast<int8_t>(q);
            }
            for (size_t i = count; i < input_features_; ++i) {
                dst[i] = static_cast<int8_t>(zero_point);
            }
            return true;
        }

        if (input_->type == kTfLiteUInt8) {
            const float scale = input_->params.scale == 0.0f ? 1.0f : input_->params.scale;
            const int zero_point = input_->params.zero_point;
            uint8_t* dst = input_->data.uint8;
            for (size_t i = 0; i < count; ++i) {
                int32_t q = static_cast<int32_t>(lroundf(features[i] / scale)) + zero_point;
                if (q < 0) q = 0;
                if (q > 255) q = 255;
                dst[i] = static_cast<uint8_t>(q);
            }
            for (size_t i = count; i < input_features_; ++i) {
                dst[i] = static_cast<uint8_t>(zero_point);
            }
            return true;
        }

        ESP_LOGE(TAG, "Unsupported input tensor type: %d", input_->type);
        return false;
    }

    bool readOutput(HeartSoundPrediction* prediction) {
        float raw_scores[kClassCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        if (output_->type == kTfLiteFloat32) {
            const size_t count = output_->bytes / sizeof(float);
            const float* src = output_->data.f;
            for (size_t i = 0; i < kClassCount && i < count; ++i) {
                raw_scores[i] = src[i];
            }
        } else if (output_->type == kTfLiteInt8) {
            const size_t count = output_->bytes;
            const int8_t* src = output_->data.int8;
            const float scale = output_->params.scale == 0.0f ? 1.0f : output_->params.scale;
            const int zero_point = output_->params.zero_point;
            for (size_t i = 0; i < kClassCount && i < count; ++i) {
                raw_scores[i] = (static_cast<int>(src[i]) - zero_point) * scale;
            }
        } else if (output_->type == kTfLiteUInt8) {
            const size_t count = output_->bytes;
            const uint8_t* src = output_->data.uint8;
            const float scale = output_->params.scale == 0.0f ? 1.0f : output_->params.scale;
            const int zero_point = output_->params.zero_point;
            for (size_t i = 0; i < kClassCount && i < count; ++i) {
                raw_scores[i] = (static_cast<int>(src[i]) - zero_point) * scale;
            }
        } else {
            ESP_LOGE(TAG, "Unsupported output tensor type: %d", output_->type);
            return false;
        }

        softmax(raw_scores, kClassCount);

        int best_index = 0;
        float best_value = raw_scores[0];
        for (size_t i = 1; i < kClassCount; ++i) {
            if (raw_scores[i] > best_value) {
                best_value = raw_scores[i];
                best_index = static_cast<int>(i);
            }
        }

        prediction->class_id = best_index;
        prediction->confidence = best_value;
        for (size_t i = 0; i < kClassCount; ++i) {
            prediction->scores[i] = raw_scores[i];
        }
        return true;
    }

    static void softmax(float* values, size_t count) {
        float max_value = values[0];
        for (size_t i = 1; i < count; ++i) {
            if (values[i] > max_value) {
                max_value = values[i];
            }
        }

        float total = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            values[i] = expf(values[i] - max_value);
            total += values[i];
        }

        if (total <= 0.0f) {
            const float uniform = 1.0f / static_cast<float>(count);
            for (size_t i = 0; i < count; ++i) {
                values[i] = uniform;
            }
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            values[i] /= total;
        }
    }

    alignas(kArenaAlignment) uint8_t tensor_arena_[kTensorArenaSize] = {0};
    tflite::MicroMutableOpResolver<3> resolver_;
    tflite::MicroErrorReporter error_reporter_;
    const tflite::Model* model_ = nullptr;
    tflite::MicroInterpreter* interpreter_ = nullptr;
    TfLiteTensor* input_ = nullptr;
    TfLiteTensor* output_ = nullptr;
    size_t input_features_ = 0;
    bool initialized_ = false;
};

inline HeartSoundInferenceEngine& inferenceEngine() {
    static HeartSoundInferenceEngine instance;
    return instance;
}
