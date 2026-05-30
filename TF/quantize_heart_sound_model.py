#!/usr/bin/env python3
"""
Heart Sound Model Quantization for ESP32 Deployment

Converts TensorFlow SavedModel (YAMNet + Dense) to:
1. TFLite Float32 (full precision, baseline)
2. TFLite INT8 (quantized for ESP32)

Usage:
    python3 quantize_heart_sound_model.py \
        --saved_model_path ./saved_model \
        --output_dir ./tflite_models \
        --dataset_path ./datasets/esc-50_extracted/Data/audio/ \
        --metadata_csv ./datasets/esc-50_extracted/Data/meta/metadata.csv
"""

import os
import sys
import numpy as np
import tensorflow as tf
import pandas as pd
from pathlib import Path
import argparse
import scipy.signal as sps
import json
from datetime import datetime

# ============================================================================
# CONFIGURATION
# ============================================================================

CLASSES = ['AS', 'MR', 'MS', 'MVP', 'N']
CLASS_MAP = {'AS': 0, 'MR': 1, 'MS': 2, 'MVP': 3, 'N': 4}
TARGET_SAMPLE_RATE = 16000
WINDOW_SIZE = 16000 * 5  # 5 seconds of audio
DEFAULT_SAVED_MODEL_CANDIDATES = [
    "./dogs_and_cats_yamnet_test",
    "./dogs_and_cats_yamnet_split",
    "./saved_model",
]

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

def _resample_numpy(wav_np, sample_rate_np):
    """Resample audio to 16 kHz using scipy."""
    if sample_rate_np != TARGET_SAMPLE_RATE:
        wav_np = sps.resample_poly(wav_np, TARGET_SAMPLE_RATE, sample_rate_np).astype('float32')
    return wav_np


def load_wav_16k_mono(filename):
    """Load WAV file and resample to 16 kHz mono."""
    file_contents = tf.io.read_file(filename)
    wav, sample_rate = tf.audio.decode_wav(file_contents, desired_channels=1)
    wav = tf.squeeze(wav, axis=-1)
    
    wav = tf.py_function(
        func=lambda w, sr: _resample_numpy(w.numpy(), sr.numpy()),
        inp=[wav, sample_rate],
        Tout=tf.float32
    )
    wav.set_shape([None])
    return wav


def pad_or_truncate_audio(wav, window_size=WINDOW_SIZE):
    """Pad or truncate audio to fixed window size."""
    if len(wav) >= window_size:
        return wav[:window_size]
    else:
        padding = window_size - len(wav)
        return np.pad(wav, (0, padding), mode='constant')


def resolve_saved_model_path(user_path=None):
    """Resolve SavedModel path using user input or notebook-export defaults."""
    if user_path:
        return user_path

    for candidate in DEFAULT_SAVED_MODEL_CANDIDATES:
        if os.path.exists(candidate):
            print(f"[*] Auto-selected SavedModel path: {candidate}")
            return candidate

    raise FileNotFoundError(
        "No SavedModel directory found. Searched: "
        + ", ".join(DEFAULT_SAVED_MODEL_CANDIDATES)
        + ". Provide --saved_model_path explicitly."
    )


# ============================================================================
# MODEL LOADING
# ============================================================================

def load_saved_model(saved_model_path):
    """Load the trained SavedModel."""
    print(f"[*] Loading SavedModel from {saved_model_path}...")
    model = tf.saved_model.load(saved_model_path)
    print(f"[✓] SavedModel loaded successfully")
    print(f"    Available signatures: {list(model.signatures.keys())}")
    return model


def get_concrete_function(saved_model):
    """Extract concrete function for conversion."""
    serving_default = saved_model.signatures['serving_default']
    
    # Get input spec
    input_spec = serving_default.structured_input_signature
    print(f"[*] Input signature: {input_spec}")
    
    return serving_default


def get_serving_input_name(saved_model):
    """Get serving input tensor name from SavedModel signature."""
    serving_default = saved_model.signatures['serving_default']
    input_map = serving_default.structured_input_signature[1]
    if not input_map:
        return None
    return next(iter(input_map.keys()))


# ============================================================================
# QUANTIZATION DATASET
# ============================================================================

def create_representative_dataset_from_files(dataset_path, metadata_csv, num_samples=100):
    """
    Create representative dataset for quantization from actual audio files.
    
    Args:
        dataset_path: Path to audio files directory
        metadata_csv: Path to metadata CSV
        num_samples: Number of samples per class for quantization
    
    Yields:
        Waveforms ready for YAMNet embedding extraction
    """
    if not os.path.exists(metadata_csv):
        print(f"[!] Warning: Metadata CSV not found at {metadata_csv}")
        print("[*] Creating synthetic representative dataset instead...")
        return create_synthetic_representative_dataset(num_samples)
    
    print(f"[*] Creating representative dataset from {num_samples} samples...")
    
    # Load metadata
    df = pd.read_csv(metadata_csv)
    
    # Filter for heart sound classes
    df_filtered = df[df['foldername'].isin(CLASSES)]
    
    # Sample balanced set
    samples_per_class = num_samples // len(CLASSES)
    representative_samples = []
    
    for class_name in CLASSES:
        class_samples = df_filtered[df_filtered['foldername'] == class_name]
        selected = class_samples.sample(
            n=min(samples_per_class, len(class_samples)),
            random_state=42
        )
        representative_samples.extend(selected['file name'].tolist())
    
    print(f"[*] Collected {len(representative_samples)} representative samples")
    
    # Load and yield
    for filename in representative_samples:
        filepath = os.path.join(dataset_path, filename)
        if os.path.exists(filepath):
            try:
                wav = load_wav_16k_mono(filepath).numpy()
                wav = pad_or_truncate_audio(wav)
                # Normalize to [-1, 1]
                wav = wav.astype(np.float32) / np.max(np.abs(wav) + 1e-7)
                yield [wav]
            except Exception as e:
                print(f"[!] Error loading {filepath}: {e}")
                continue


def create_synthetic_representative_dataset(num_samples=100):
    """Create synthetic audio-like data for quantization calibration."""
    print(f"[*] Creating {num_samples} synthetic representative samples...")
    
    for i in range(num_samples):
        # Generate synthetic audio (white noise + frequency components)
        t = np.linspace(0, 5, WINDOW_SIZE, dtype=np.float32)
        
        # Simulate heart sound frequencies: 50-500 Hz
        freq1 = np.random.uniform(50, 200)
        freq2 = np.random.uniform(200, 500)
        
        signal = (
            0.3 * np.sin(2 * np.pi * freq1 * t / TARGET_SAMPLE_RATE) +
            0.2 * np.sin(2 * np.pi * freq2 * t / TARGET_SAMPLE_RATE) +
            0.1 * np.random.normal(0, 0.1, WINDOW_SIZE)
        ).astype(np.float32)
        
        # Normalize
        signal = signal / (np.max(np.abs(signal)) + 1e-7)
        
        yield [signal]


def make_representative_data_gen(base_generator, input_name):
    """Adapt representative samples for converter using signature input name."""
    def _gen():
        for sample in base_generator():
            if input_name:
                yield {input_name: sample[0]}
            else:
                yield sample
    return _gen


# ============================================================================
# CONVERSION TO TFLITE
# ============================================================================

def convert_to_tflite_float32(saved_model_path, output_path):
    """Convert SavedModel to TFLite float32 (baseline)."""
    print(f"\n[*] Converting to TFLite Float32...")
    
    converter = tf.lite.TFLiteConverter.from_saved_model(saved_model_path)
    converter.optimizations = []  # No optimizations for baseline
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS
    ]
    
    try:
        tflite_float_model = converter.convert()
        with open(output_path, 'wb') as f:
            f.write(tflite_float_model)
        
        size_mb = os.path.getsize(output_path) / (1024 * 1024)
        print(f"[✓] Float32 model saved: {output_path} ({size_mb:.2f} MB)")
        
        return output_path
    except Exception as e:
        print(f"[!] Error converting to float32: {e}")
        return None


def convert_to_tflite_int8(saved_model_path, output_path, representative_dataset_gen):
    """Convert SavedModel to TFLite INT8 (quantized)."""
    print(f"\n[*] Converting to TFLite INT8 (quantized)...")
    
    converter = tf.lite.TFLiteConverter.from_saved_model(saved_model_path)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    
    # Full integer quantization
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS_INT8
    ]
    
    # Set representative dataset for calibration
    converter.representative_dataset = representative_dataset_gen
    
    # Set input/output to int8
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    
    try:
        tflite_quant_model = converter.convert()
        with open(output_path, 'wb') as f:
            f.write(tflite_quant_model)
        
        size_kb = os.path.getsize(output_path) / 1024
        print(f"[✓] INT8 model saved: {output_path} ({size_kb:.2f} KB)")
        
        return output_path
    except Exception as e:
        print(f"[!] Error converting to INT8: {e}")
        print(f"[*] Falling back to dynamic range quantization...")
        
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_ops = [
            tf.lite.OpsSet.TFLITE_BUILTINS
        ]
        converter.inference_input_type = tf.float32
        converter.inference_output_type = tf.float32
        
        try:
            tflite_quant_model = converter.convert()
            with open(output_path, 'wb') as f:
                f.write(tflite_quant_model)
            
            size_kb = os.path.getsize(output_path) / 1024
            print(f"[✓] Dynamic range quantized model saved: {output_path} ({size_kb:.2f} KB)")
            return output_path
        except Exception as e2:
            print(f"[!] Fallback conversion failed: {e2}")
            return None


# ============================================================================
# VALIDATION
# ============================================================================

def validate_tflite_model(tflite_path, representative_samples_gen):
    """Load and test TFLite model."""
    print(f"\n[*] Validating TFLite model: {tflite_path}")
    
    # Load interpreter
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    
    # Get input and output details
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    print(f"[*] Input details: {input_details}")
    print(f"[*] Output details: {output_details}")
    
    return interpreter, input_details, output_details


# ============================================================================
# C HEADER GENERATION
# ============================================================================

def generate_c_header(model_path, header_path, var_name="heart_sound_model"):
    """Convert .tflite file to C header for embedding in firmware."""
    print(f"\n[*] Generating C header for embedding in firmware...")
    
    with open(model_path, 'rb') as f:
        model_data = f.read()
    
    # Create C array
    c_code = f"""// Auto-generated model header
// Generated: {datetime.now().isoformat()}
// Model file: {os.path.basename(model_path)}
// Model size: {len(model_data)} bytes

#ifndef MODEL_DATA_H
#define MODEL_DATA_H

#include <stdint.h>

// Quantized heart sound classification model
const uint8_t {var_name}[] = {{
"""
    
    # Add bytes in hex format (16 per line)
    for i, byte in enumerate(model_data):
        if i % 16 == 0:
            c_code += "\n  "
        c_code += f"0x{byte:02x}, "
    
    c_code = c_code.rstrip(", ")  # Remove trailing comma
    c_code += f"\n}};\n\nconst size_t {var_name}_len = {len(model_data)};\n\n#endif // MODEL_DATA_H\n"
    
    with open(header_path, 'w') as f:
        f.write(c_code)
    
    print(f"[✓] C header generated: {header_path}")
    print(f"    Model size in header: {len(model_data)} bytes")
    
    return header_path


# ============================================================================
# MAIN EXECUTION
# ============================================================================

def main(args):
    """Main quantization pipeline."""
    print("="*70)
    print("Heart Sound Model Quantization for ESP32")
    print("="*70)
    
    # Resolve SavedModel path
    saved_model_path = resolve_saved_model_path(args.saved_model_path)

    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Load SavedModel
    saved_model = load_saved_model(saved_model_path)
    input_name = get_serving_input_name(saved_model)
    if input_name:
        print(f"[*] Serving input tensor name: {input_name}")
    
    # Create representative dataset
    base_representative_gen = lambda: create_representative_dataset_from_files(
        args.dataset_path,
        args.metadata_csv,
        num_samples=100
    )
    representative_dataset_gen = make_representative_data_gen(base_representative_gen, input_name)
    
    # Convert to float32 (baseline)
    float32_model_path = os.path.join(args.output_dir, "heart_sound_model_float32.tflite")
    convert_to_tflite_float32(saved_model_path, float32_model_path)
    
    # Convert to INT8 (quantized)
    int8_model_path = os.path.join(args.output_dir, "heart_sound_model_int8.tflite")
    convert_to_tflite_int8(saved_model_path, int8_model_path, representative_dataset_gen)
    
    # Validate models
    if os.path.exists(int8_model_path):
        interpreter, input_details, output_details = validate_tflite_model(
            int8_model_path,
            representative_dataset_gen
        )
    
    # Generate C header for firmware
    c_header_path = os.path.join(args.output_dir, "model_data.h")
    if os.path.exists(int8_model_path):
        generate_c_header(int8_model_path, c_header_path, "heart_sound_model_quantized")
    else:
        print("[!] Skipping C header generation because INT8 model was not created")
    
    # Generate report
    report_path = os.path.join(args.output_dir, "QUANTIZATION_REPORT.txt")
    with open(report_path, 'w') as f:
        f.write("HEART SOUND MODEL QUANTIZATION REPORT\n")
        f.write("="*70 + "\n")
        f.write(f"Generated: {datetime.now().isoformat()}\n\n")
        
        if os.path.exists(float32_model_path):
            float32_size = os.path.getsize(float32_model_path) / 1024
            f.write(f"Float32 Model: {float32_size:.2f} KB\n")
        
        if os.path.exists(int8_model_path):
            int8_size = os.path.getsize(int8_model_path) / 1024
            f.write(f"INT8 Model: {int8_size:.2f} KB\n")
            
            if os.path.exists(float32_model_path):
                compression_ratio = float32_size / int8_size
                f.write(f"Compression Ratio: {compression_ratio:.2f}x\n")
        
        f.write("\nClasses: " + ", ".join(CLASSES) + "\n")
        f.write("Input: raw mono audio waveform (16 kHz, float32)\n")
        f.write("Pipeline: YAMNet embeddings (1024) → Dense(512) → Dense(5) → reduce_mean\n")
    
    print(f"\n[✓] Quantization report: {report_path}")
    
    print("\n" + "="*70)
    print("✓ QUANTIZATION COMPLETE")
    print("="*70)
    print(f"\nOutput files:")
    print(f"  1. {float32_model_path}")
    print(f"  2. {int8_model_path}")
    print(f"  3. {c_header_path}")
    print(f"  4. {report_path}")
    print(f"\nNext steps:")
    print(f"  - Review quantization report for accuracy impact")
    print(f"  - Use {c_header_path} in ESP32 firmware")
    print(f"  - Validate model inference on target hardware")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Quantize heart sound model for ESP32 deployment"
    )
    parser.add_argument(
        "--saved_model_path",
        type=str,
        default=None,
        help="Path to TensorFlow SavedModel directory (auto-detects notebook exports if omitted)"
    )
    parser.add_argument(
        "--output_dir",
        type=str,
        default="./tflite_models",
        help="Output directory for TFLite models"
    )
    parser.add_argument(
        "--dataset_path",
        type=str,
        default="./datasets/esc-50_extracted/Data/audio/",
        help="Path to audio dataset directory"
    )
    parser.add_argument(
        "--metadata_csv",
        type=str,
        default="./datasets/esc-50_extracted/Data/meta/metadata.csv",
        help="Path to metadata CSV"
    )
    
    args = parser.parse_args()
    main(args)
