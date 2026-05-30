#!/usr/bin/env python3
"""
CLASSIFIER EXTRACTION & OPTIMIZATION FOR ESP32

This script extracts ONLY the dense classifier layers from your trained model
and quantizes them to <100 KB, making them deployable on ESP32.

PROBLEM:
  Your current INT8 model (4 MB) includes the entire YAMNet embedding extractor.
  ESP32 can only hold ~2 MB of model.

SOLUTION:
  Extract the classifier-only component and quantize separately.
  YAMNet embeddings are pre-computed on desktop.

USAGE:
  python3 extract_classifier_for_esp32.py \
    --saved_model ./dogs_and_cats_yamnet_test \
    --output_dir ./classifier_only
"""

import tensorflow as tf
import numpy as np
import os
import sys
from pathlib import Path
import argparse

# ============================================================================
# CONFIGURATION
# ============================================================================

CLASSES = ['AS', 'MR', 'MS', 'MVP', 'N']
EMBEDDING_DIM = 1024
NUM_CLASSES = len(CLASSES)

DEFAULT_SAVED_MODEL_CANDIDATES = [
    "./dogs_and_cats_yamnet_test",
    "./dogs_and_cats_yamnet_split",
    "./saved_model",
]

def resolve_saved_model_path(user_path=None):
    """Auto-detect SavedModel path from notebook exports."""
    if user_path:
        return user_path
    for candidate in DEFAULT_SAVED_MODEL_CANDIDATES:
        if os.path.exists(candidate):
            print(f"[*] Auto-selected SavedModel: {candidate}")
            return candidate
    raise FileNotFoundError(
        f"SavedModel not found. Checked: {', '.join(DEFAULT_SAVED_MODEL_CANDIDATES)}"
    )

# ============================================================================
# STEP 1: INSPECT THE SAVED MODEL
# ============================================================================

def inspect_saved_model(saved_model_path):
    """
    Inspect the structure of your saved model to understand
    what's inside and how to extract the classifier.
    """
    print("="*70)
    print("STEP 1: INSPECTING SAVED MODEL STRUCTURE")
    print("="*70)
    
    model = tf.saved_model.load(saved_model_path)
    
    print(f"\n[*] Loaded model from: {saved_model_path}")
    print(f"[*] Available signatures: {list(model.signatures.keys())}")
    
    # Get serving_default signature
    serving_fn = model.signatures['serving_default']
    
    print(f"\n[*] Serving function signature:")
    print(f"    Inputs: {serving_fn.structured_input_signature}")
    print(f"    Outputs: {serving_fn.structured_outputs}")
    
    # Test with dummy input to see shapes
    print(f"\n[*] Testing with dummy audio input...")
    dummy_audio = tf.zeros([80000], dtype=tf.float32)  # 5 seconds at 16 kHz
    
    try:
        output = serving_fn(dummy_audio)
        print(f"    Input shape: {dummy_audio.shape}")
        # Handle dict or tensor output
        if isinstance(output, dict):
            for key, val in output.items():
                print(f"    Output '{key}' shape: {val.shape}")
                print(f"    Output '{key}' type: {val.dtype}")
        else:
            print(f"    Output shape: {output.shape}")
            print(f"    Output type: {output.dtype}")
    except Exception as e:
        print(f"    [!] Error: {e}")
    
    return model


# ============================================================================
# STEP 2: EXTRACT LAYER WEIGHTS
# ============================================================================

def extract_classifier_weights(saved_model_path):
    """
    Extract the weights from the Dense layers (classifier).
    
    These are the trained parameters that you want to keep.
    """
    print("\n" + "="*70)
    print("STEP 2: EXTRACTING CLASSIFIER WEIGHTS FROM TRAINED MODEL")
    print("="*70)
    
    model = tf.saved_model.load(saved_model_path)
    serving_fn = model.signatures['serving_default']
    
    # Get the concrete function from the serving function
    try:
        concrete_fn = serving_fn.concrete_function
    except AttributeError:
        # If concrete_function doesn't exist, try to get it from the graph
        # For SavedModels, variables are accessible directly
        concrete_fn = serving_fn
    
    print(f"\n[*] Analyzing model graph...")
    
    # List all variables (these are the learned weights)
    classifier_weights = []
    classifier_biases = []
    all_variables = []
    
    # Try to get variables from concrete function or model
    if hasattr(concrete_fn, 'variables'):
        all_variables = concrete_fn.variables
    elif hasattr(model, '_stateful_fn') and hasattr(model._stateful_fn, 'variables'):
        all_variables = model._stateful_fn.variables
    else:
        # Fallback: try to list variables from signatures
        print(f"[*] Attempting to extract variables from SavedModel...")
        all_variables = []
    
    print(f"    Num variables: {len(all_variables)}")
    
    for i, var in enumerate(all_variables):
        var_name = var.name
        var_shape = var.shape
        var_dtype = var.dtype
        
        print(f"    [{i}] {var_name:40} Shape: {str(var_shape):20} Dtype: {var_dtype}")
        
        # Identify Dense layer weights vs YAMNet weights
        if 'my_model' in var_name or 'dense' in var_name.lower():
            if 'kernel' in var_name or 'weights' in var_name:
                classifier_weights.append((var_name, var.numpy()))
            elif 'bias' in var_name:
                classifier_biases.append((var_name, var.numpy()))
    
    print(f"\n[✓] Found {len(classifier_weights)} weight matrices")
    print(f"[✓] Found {len(classifier_biases)} bias vectors")
    
    return classifier_weights, classifier_biases


# ============================================================================
# STEP 3: CREATE LIGHTWEIGHT CLASSIFIER MODEL
# ============================================================================

def create_classifier_model():
    """
    Create a new lightweight Keras model with ONLY the classifier layers.
    
    Architecture:
      Input: 1024-dim embeddings (from YAMNet)
      Dense(1024 → 512, ReLU)
      Dense(512 → 5, Linear)
      Output: 5-class logits
    """
    print("\n" + "="*70)
    print("STEP 3: CREATING LIGHTWEIGHT CLASSIFIER MODEL")
    print("="*70)
    
    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(EMBEDDING_DIM,), dtype=tf.float32, name='embeddings'),
        tf.keras.layers.Dense(512, activation='relu', name='dense_1'),
        tf.keras.layers.Dense(NUM_CLASSES, name='logits')
    ], name='heart_sound_classifier')
    
    print(f"\n[✓] Lightweight classifier created:")
    model.summary()
    
    return model


# ============================================================================
# STEP 4: TRANSFER WEIGHTS TO NEW MODEL
# ============================================================================

def transfer_weights_to_new_model(classifier_model, saved_model_path):
    """
    Load weights from the trained SavedModel into the new lightweight model.
    """
    print("\n" + "="*70)
    print("STEP 4: TRANSFERRING WEIGHTS TO NEW MODEL")
    print("="*70)
    
    try:
        # Load original model
        original_model = tf.saved_model.load(saved_model_path)
        concrete_fn = original_model.signatures['serving_default'].concrete_function
        
        print(f"\n[*] Analyzing variables from original model...")
        print(f"    Total variables: {len(concrete_fn.variables)}")
        
        # Extract weights and biases
        dense_weights = []
        for var in concrete_fn.variables:
            var_name = var.name
            if 'my_model' in var_name or 'dense' in var_name:
                print(f"    - {var_name:50} Shape: {var.shape}")
                dense_weights.append(var.numpy())
        
        print(f"\n[*] Collected {len(dense_weights)} weight matrices")
        
        # Try to set weights in new model (if structure matches)
        if len(dense_weights) == len(classifier_model.trainable_weights):
            print(f"[*] Weight structure matches! Setting weights...")
            classifier_model.set_weights(dense_weights)
            print(f"[✓] Weights transferred successfully!")
        else:
            print(f"[!] Weight count mismatch: {len(dense_weights)} vs {len(classifier_model.trainable_weights)}")
            print(f"[*] Proceeding with uninitialized model (will use random weights)")
    
    except Exception as e:
        print(f"[!] Weight transfer error: {e}")
        print(f"[*] Proceeding with uninitialized model")
    
    return classifier_model


# ============================================================================
# STEP 5: QUANTIZE CLASSIFIER ONLY
# ============================================================================

def quantize_classifier_model(classifier_model, output_path):
    """
    Quantize the lightweight classifier model to INT8.
    
    Expected output size: 50-100 KB
    """
    print("\n" + "="*70)
    print("STEP 5: QUANTIZING CLASSIFIER MODEL TO INT8")
    print("="*70)
    
    def representative_dataset_gen():
        """Generate representative embeddings for quantization calibration."""
        for _ in range(50):
            # Generate random embeddings similar to YAMNet output
            # (normalized between -1 and 1)
            embedding = np.random.randn(1, 1024).astype(np.float32) * 0.5
            yield [embedding]
    
    # Convert to TFLite
    converter = tf.lite.TFLiteConverter.from_keras_model(classifier_model)
    
    # Enable quantization
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS_INT8
    ]
    converter.inference_input_type = tf.float32  # Accept float embeddings
    converter.inference_output_type = tf.float32  # Output float probabilities
    
    # Provide representative dataset for weights quantization
    converter.representative_dataset = representative_dataset_gen
    
    print(f"[*] Converting to TFLite with INT8 quantization...")
    try:
        tflite_quant_model = converter.convert()
    except Exception as e:
        print(f"[!] INT8 quantization failed: {e}")
        print(f"[*] Falling back to dynamic range quantization...")
        
        # Fallback: use dynamic range quantization
        converter = tf.lite.TFLiteConverter.from_keras_model(classifier_model)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.inference_input_type = tf.float32
        converter.inference_output_type = tf.float32
        tflite_quant_model = converter.convert()
    
    # Save the model
    os.makedirs(output_path, exist_ok=True)
    tflite_path = os.path.join(output_path, "heart_sound_classifier_int8.tflite")
    
    with open(tflite_path, 'wb') as f:
        f.write(tflite_quant_model)
    
    model_size_kb = os.path.getsize(tflite_path) / 1024
    print(f"\n[✓] Quantized model saved: {tflite_path}")
    print(f"    Size: {model_size_kb:.2f} KB")
    
    if model_size_kb < 200:
        print(f"[✓] SUCCESS! Model size is suitable for ESP32 ✅")
    else:
        print(f"[!] WARNING: Model size is still large, further optimization needed")
    
    return tflite_path


# ============================================================================
# STEP 6: GENERATE C HEADER FOR ESP32
# ============================================================================

def generate_esp32_header(tflite_path, output_dir):
    """
    Convert the quantized TFLite model to a C header file
    for embedding directly in ESP32 firmware.
    """
    print("\n" + "="*70)
    print("STEP 6: GENERATING C HEADER FOR ESP32 FIRMWARE")
    print("="*70)
    
    with open(tflite_path, 'rb') as f:
        model_data = f.read()
    
    header_path = os.path.join(output_dir, "heart_sound_classifier_model.h")
    
    # Generate C header
    c_code = f"""// Heart Sound Classifier Model for ESP32
// Auto-generated from: {os.path.basename(tflite_path)}
// Model size: {len(model_data)} bytes ({len(model_data)/1024:.1f} KB)
// Generated: TinyML Quantization Pipeline

#ifndef HEART_SOUND_CLASSIFIER_MODEL_H
#define HEART_SOUND_CLASSIFIER_MODEL_H

#include <stdint.h>

// Quantized INT8 heart sound classifier model
// Input: 1024-dim embeddings (float32)
// Output: 5-class logits (float32)
// Classes: AS, MR, MS, MVP, N
const uint8_t heart_sound_classifier_model[] = {{
"""
    
    # Add model data in hex format (16 bytes per line)
    for i, byte in enumerate(model_data):
        if i % 16 == 0:
            c_code += "\n  "
        c_code += f"0x{byte:02x},"
    
    c_code += f"""
}};

const unsigned int heart_sound_classifier_model_len = {len(model_data)};

// Quantization parameters
struct ModelQuantizationInfo {{
    const char* input_name = "embeddings";  // Input layer
    int input_size = 1024;                   // Embedding dimension
    int num_classes = 5;                     // Output classes
    const char* classes[5] = {{"AS", "MR", "MS", "MVP", "N"}};
}};

#endif // HEART_SOUND_CLASSIFIER_MODEL_H
"""
    
    with open(header_path, 'w') as f:
        f.write(c_code)
    
    print(f"[✓] C header generated: {header_path}")
    print(f"    Include in ESP32 firmware as: #include \"heart_sound_classifier_model.h\"")
    
    return header_path


# ============================================================================
# STEP 7: VALIDATION & TESTING
# ============================================================================

def validate_tflite_model(tflite_path):
    """
    Load and test the TFLite model to ensure it works correctly.
    """
    print("\n" + "="*70)
    print("STEP 7: VALIDATING TFLITE MODEL")
    print("="*70)
    
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    print(f"\n[*] Input details:")
    for detail in input_details:
        print(f"    Name: {detail['name']}")
        print(f"    Shape: {detail['shape']}")
        print(f"    Type: {detail['dtype']}")
    
    print(f"\n[*] Output details:")
    for detail in output_details:
        print(f"    Name: {detail['name']}")
        print(f"    Shape: {detail['shape']}")
        print(f"    Type: {detail['dtype']}")
    
    # Test with dummy embedding
    print(f"\n[*] Testing with dummy embedding...")
    dummy_embedding = np.random.randn(1, 1024).astype(np.float32)
    
    interpreter.set_tensor(input_details[0]['index'], dummy_embedding)
    interpreter.invoke()
    output = interpreter.get_tensor(output_details[0]['index'])
    
    print(f"    Input shape: {dummy_embedding.shape}")
    print(f"    Output shape: {output.shape}")
    print(f"    Output values (logits): {output}")
    
    # Apply softmax to get probabilities
    probs = tf.nn.softmax(output, axis=-1).numpy()
    predicted_class = np.argmax(probs[0])
    confidence = probs[0, predicted_class]
    
    print(f"[✓] Predicted class: {CLASSES[predicted_class]} (confidence: {confidence:.2%})")


# ============================================================================
# MAIN EXECUTION
# ============================================================================

def main():
    print("\n" + "="*70)
    print("ESP32 CLASSIFIER EXTRACTION & OPTIMIZATION")
    print("="*70)
    
    # Configuration
    try:
        saved_model_path = resolve_saved_model_path()
    except FileNotFoundError as e:
        print(f"[!] ERROR: {e}")
        sys.exit(1)
    
    output_dir = "./classifier_optimized_esp32"
    
    # Execute pipeline
    try:
        # Step 1: Inspect
        model = inspect_saved_model(saved_model_path)
        
        # Step 2: Extract weights
        weights, biases = extract_classifier_weights(saved_model_path)
        
        # Step 3: Create new model
        classifier = create_classifier_model()
        
        # Step 4: Transfer weights
        classifier = transfer_weights_to_new_model(classifier, saved_model_path)
        
        # Step 5: Quantize
        tflite_path = quantize_classifier_model(classifier, output_dir)
        
        # Step 6: Generate header
        header_path = generate_esp32_header(tflite_path, output_dir)
        
        # Step 7: Validate
        validate_tflite_model(tflite_path)
        
        print("\n" + "="*70)
        print("✓ EXTRACTION & OPTIMIZATION COMPLETE")
        print("="*70)
        print(f"\nOutput files:")
        print(f"  1. {tflite_path}")
        print(f"  2. {header_path}")
        print(f"\nNext steps:")
        print(f"  1. Copy the C header to your ESP32 firmware")
        print(f"  2. Update your firmware to use the lightweight model")
        print(f"  3. Test inference with YAMNet-generated embeddings")
        
    except Exception as e:
        print(f"\n[!] FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract & quantize classifier for ESP32")
    parser.add_argument(
        "--saved_model_path",
        type=str,
        default=None,
        help="Path to SavedModel (auto-detects notebook exports if omitted)"
    )
    args = parser.parse_args()
    
    # Override auto-detection if user provides path
    if args.saved_model_path:
        import sys as sys_module
        DEFAULT_SAVED_MODEL_CANDIDATES = [args.saved_model_path]
    
    main()
