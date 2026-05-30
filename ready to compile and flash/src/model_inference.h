#ifndef MODEL_INFERENCE_H
#define MODEL_INFERENCE_H

#include <TensorFlowLite_ESP32.h>

// TensorFlow Lite model buffer (embedded)
extern const unsigned char heart_sound_model_tflite[];
extern const unsigned int heart_sound_model_tflite_len;

// Model input/output dimensions
#define MODEL_INPUT_LENGTH 16000      // 1 second @ 16kHz
#define MODEL_NUM_CLASSES 4            // Healthy, Murmur, AS, MR
#define INFERENCE_THRESHOLD 0.7f

// Classification labels
enum HeartSoundClass {
    NORMAL = 0,
    MURMUR = 1,
    AORTIC_STENOSIS = 2,
    MITRAL_REGURGITATION = 3
};

// Inference result struct
struct InferenceResult {
    HeartSoundClass classification;
    float confidence;
    float scores[MODEL_NUM_CLASSES];
    unsigned long inference_time_ms;
};

// Function prototypes
bool model_init();
InferenceResult model_infer(const int16_t* audio_data, size_t audio_len);
void model_cleanup();
const char* get_class_name(HeartSoundClass cls);

#endif // MODEL_INFERENCE_H
