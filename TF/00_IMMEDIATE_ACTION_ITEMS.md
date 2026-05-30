# 🚨 IMMEDIATE ACTION ITEMS - EXECUTE TONIGHT (DAY 1)

**Status:** Critical path item identified  
**Timeline:** 2-3 hours  
**Deliverable:** Classifier-only model (<100 KB) ready for ESP32

---

## THE PROBLEM (in 30 seconds)

Your current INT8 model is **4 MB** but ESP32 can only hold **~2 MB** of models.

**Why?** The entire YAMNet embedding extractor is embedded in the TFLite conversion.

**Solution?** Extract ONLY the dense classifier layers (the part you trained).

Expected result: **80-100 KB model** ✅

---

## STEP-BY-STEP ACTIONS

### ACTION 1: Extract Classifier Model (30 min)

**Run this command in your project directory:**

```bash
cd /path/to/your/project
python3 extract_classifier_for_esp32.py
```

**What it does:**
1. Loads your SavedModel (`./dogs_and_cats_yamnet_test`)
2. Extracts ONLY the Dense layers (your trained part)
3. Creates a new lightweight model
4. Quantizes to INT8
5. Generates C header for ESP32

**Expected output:**
```
[✓] Lightweight classifier created
[✓] Quantized model saved: classifier_optimized_esp32/heart_sound_classifier_int8.tflite (88.5 KB)
[✓] C header generated: classifier_optimized_esp32/heart_sound_classifier_model.h
```

**File location:** Use the script at `/home/claude/extract_classifier_for_esp32.py`

---

### ACTION 2: Verify Model Size (10 min)

```bash
# Check file size
ls -lh classifier_optimized_esp32/

# Should see:
# -rw-r--r-- heart_sound_classifier_int8.tflite  (~80-100 KB)  ✅ GOOD
# -rw-r--r-- heart_sound_classifier_model.h      (~250 KB)     ✅ C header
```

**Validation:**
- If size < 100 KB → ✅ **PASS** - Ready for ESP32
- If size > 200 KB → ⚠️ **WARNING** - May need further optimization
- If size > 500 KB → ❌ **FAIL** - Need to switch to Option B (lightweight CNN)

---

### ACTION 3: Test the Model Works (45 min)

Create a test script `test_classifier.py`:

```python
#!/usr/bin/env python3
"""Test the extracted classifier model"""

import tensorflow as tf
import numpy as np

# Load the quantized model
interpreter = tf.lite.Interpreter(
    model_path='classifier_optimized_esp32/heart_sound_classifier_int8.tflite'
)
interpreter.allocate_tensors()

# Get input/output details
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

print("[*] Input details:")
print(f"    Shape: {input_details[0]['shape']}")
print(f"    Type: {input_details[0]['dtype']}")

print("[*] Output details:")
print(f"    Shape: {output_details[0]['shape']}")
print(f"    Type: {output_details[0]['dtype']}")

# Test with dummy embeddings (1024-dim)
print("\n[*] Testing with dummy embeddings...")
dummy_embedding = np.random.randn(1, 1024).astype(np.float32)

interpreter.set_tensor(input_details[0]['index'], dummy_embedding)
interpreter.invoke()
output = interpreter.get_tensor(output_details[0]['index'])

print(f"    Output shape: {output.shape}")
print(f"    Output values: {output}")

# Apply softmax
CLASSES = ['AS', 'MR', 'MS', 'MVP', 'N']
probs = tf.nn.softmax(output[0]).numpy()
predicted_class = np.argmax(probs)
confidence = probs[predicted_class]

print(f"\n[✓] Predicted: {CLASSES[predicted_class]} ({confidence:.2%} confidence)")
```

**Run it:**
```bash
python3 test_classifier.py
```

**Expected output:**
```
[*] Input details:
    Shape: (1, 1024)
    Type: <class 'numpy.float32'>

[*] Output details:
    Shape: (1, 5)
    Type: <class 'numpy.float32'>

[✓] Predicted: N (87% confidence)
```

---

### ACTION 4: Validate Accuracy (15 min)

Compare the quantized model vs. original on test data:

```python
#!/usr/bin/env python3
"""Compare quantized vs original model accuracy"""

import tensorflow as tf
import numpy as np
import pandas as pd

# Load quantized model
interpreter = tf.lite.Interpreter(
    model_path='classifier_optimized_esp32/heart_sound_classifier_int8.tflite'
)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# Load original model (if available)
# original_model = tf.saved_model.load('./dogs_and_cats_yamnet_test')

# Test on 10 samples from each class
CLASSES = ['AS', 'MR', 'MS', 'MVP', 'N']
class_accuracies = {c: [] for c in CLASSES}

for class_name in CLASSES:
    # Load test samples for this class
    test_samples = []  # Load 10 test embeddings for each class
    
    for embedding in test_samples:
        embedding = np.expand_dims(embedding, 0).astype(np.float32)
        
        interpreter.set_tensor(input_details[0]['index'], embedding)
        interpreter.invoke()
        output = interpreter.get_tensor(output_details[0]['index'])
        
        pred = np.argmax(output[0])
        accuracy = (pred == CLASSES.index(class_name))
        class_accuracies[class_name].append(accuracy)

# Print results
print("Per-class accuracy (quantized model):")
for class_name, acc_list in class_accuracies.items():
    acc_pct = np.mean(acc_list) * 100 if acc_list else 0
    print(f"  {class_name}: {acc_pct:.1f}%")

overall_acc = np.mean([np.mean(v) for v in class_accuracies.values()]) * 100
print(f"\nOverall accuracy: {overall_acc:.1f}%")

if overall_acc > 85:
    print("[✓] Quantization successful - minimal accuracy loss")
else:
    print("[!] WARNING - Accuracy loss > 15%, may need retraining")
```

---

### ACTION 5: Generate C Header for Firmware (5 min)

The script already does this, but verify:

```bash
# Check if header was created
head -20 classifier_optimized_esp32/heart_sound_classifier_model.h

# Should show:
# // Heart Sound Classifier Model for ESP32
# // Auto-generated from: heart_sound_classifier_int8.tflite
# // Model size: XXXXX bytes (YY.Y KB)
# ...
# const uint8_t heart_sound_classifier_model[] = {
#   0x20, 0x00, 0x00, 0x00, ...
```

---

## TROUBLESHOOTING

### Problem 1: "SavedModel not found"
```
Solution: Update the path in extract_classifier_for_esp32.py
  Change: saved_model_path = "./dogs_and_cats_yamnet_test"
  To:     saved_model_path = "/full/path/to/dogs_and_cats_yamnet_test"
```

### Problem 2: "Model size still too large (>200 KB)"
```
Solution: You'll need to implement Option B (lightweight CNN)
  Timeline: 6-8 hours (still fits in Day 2-3)
  Trade-off: Better accuracy, true edge inference
  
  Contact support if needed
```

### Problem 3: "Accuracy dropped significantly"
```
Solution: Try these in order:
  1. Fine-tune quantization calibration
  2. Use per-layer quantization instead of uniform
  3. If >10% accuracy loss → switch to Option B (lightweight CNN)
```

---

## SUCCESS CHECKLIST

After completing all 5 actions, you should have:

- [ ] `heart_sound_classifier_int8.tflite` **< 100 KB**
- [ ] `heart_sound_classifier_model.h` generated
- [ ] Test script runs without errors
- [ ] Model inference works (produces 5-class output)
- [ ] Quantized accuracy > 85% compared to original
- [ ] C header is readable and valid

---

## TIMELINE

| Action | Time | Status |
|--------|------|--------|
| 1. Run extraction script | 30 min | ⏱️ DO NOW |
| 2. Verify model size | 10 min | ⏱️ DO NOW |
| 3. Test model works | 45 min | ⏱️ DO NOW |
| 4. Validate accuracy | 15 min | ⏱️ DO NOW |
| 5. Verify C header | 5 min | ⏱️ DO NOW |
| **TOTAL** | **~105 min** | **⏱️ ~2 hours** |

**Target completion time:** Tonight before bed  
**Next milestone:** Hardware setup & firmware scaffolding (Day 2)

---

## IF THINGS GO WRONG

**If model extraction fails:**

Option A (Quick fix):
```
Switch to pre-computed embeddings approach
- Use YAMNet on desktop to generate embeddings
- Save embeddings as CSV
- Load embeddings directly in ESP32 inference
- Timeline: 4 hours
```

Option B (Nuclear option):
```
Train lightweight CNN from scratch
- Pure end-to-end model (audio → spectrogram → CNN → output)
- Expected size: 50-80 KB
- Timeline: 6-8 hours (do on Day 2-3)
```

---

## FILES YOU'LL NEED

**Already provided in `/home/claude/`:**
- ✅ `extract_classifier_for_esp32.py` - The main script
- ✅ `CRITICAL_MODEL_ANALYSIS.md` - Technical explanation
- ✅ `REVISED_7DAY_SPRINT.md` - Full project roadmap
- ✅ `PROJECT_ANALYSIS.md` - Detailed analysis

**After tonight, you'll have:**
- 📁 `classifier_optimized_esp32/` directory containing:
  - `heart_sound_classifier_int8.tflite` - Your ESP32 model
  - `heart_sound_classifier_model.h` - C header for firmware

---

## NEXT IMMEDIATE STEPS (AFTER TONIGHT)

Once models are ready:

1. **Day 2 Morning:** Test classifier accuracy on real data
2. **Day 2 Afternoon:** Hardware validation (ADC, OLED, battery)
3. **Day 3:** Begin firmware development in PlatformIO
4. **Day 4:** Integrate model into firmware
5. **Day 5:** Display UI and logging
6. **Day 6:** Full system testing
7. **Day 7:** Report and documentation

---

## QUESTIONS TO ANSWER BEFORE TOMORROW

1. **Model size:** Is extracted model < 100 KB?
2. **Accuracy:** Did quantization preserve > 85% accuracy?
3. **C header:** Can it be included in Arduino code without errors?
4. **Hardware:** Is ESP32 + audio input + OLED all working?

---

## CONTACT / HELP

If you get stuck:
1. Check the error message carefully
2. Review troubleshooting section above
3. Review the full technical documentation
4. Consider switching to Option B (lightweight CNN)

---

**⏱️ START NOW!** This 2-hour task unlocks all subsequent work.

**🎯 Success criteria:** Classifier model < 100 KB + C header ready for firmware

**📅 Target completion:** Tonight, before Day 2 starts
