#!/usr/bin/env python3
"""
tests/test_model_equivalence.py
Verifies numerical equivalence between exported C++ decision tree model
and scikit-learn model (< 1e-5 tolerance) as required by Task 3.7.
"""

import os
import sys
import subprocess
import tempfile
import joblib
import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODEL_PATH = os.path.join(REPO_ROOT, "ml", "models", "best_model.joblib")
HEADER_PATH = os.path.join(REPO_ROOT, "adaptive_supervised_scheduler", "model_generated.h")

def test_equivalence():
    assert os.path.exists(MODEL_PATH), f"Model file not found: {MODEL_PATH}"
    assert os.path.exists(HEADER_PATH), f"Header file not found: {HEADER_PATH}"

    payload = joblib.load(MODEL_PATH)
    model = payload["model"]
    mean = payload["mean"]
    scale = payload["scale"]

    np.random.seed(42)
    N_SAMPLES = 2000
    # Generate realistic scale-free feature vectors
    X_raw = np.random.uniform(0.0, 10.0, size=(N_SAMPLES, len(mean))).astype(np.float32)

    # Scikit-learn predictions on standardized features
    X_norm = (X_raw - mean) / scale
    py_preds = model.predict(X_norm).astype(np.float32)

    # Create temporary C++ test harness
    with tempfile.TemporaryDirectory() as tmpdir:
        input_bin = os.path.join(tmpdir, "inputs.bin")
        output_bin = os.path.join(tmpdir, "outputs.bin")
        cpp_source = os.path.join(tmpdir, "test_harness.cc")
        bin_executable = os.path.join(tmpdir, "test_harness")

        X_raw.tofile(input_bin)

        cpp_code = f"""
#include "{HEADER_PATH}"
#include <iostream>
#include <fstream>
#include <vector>

int main() {{
    std::ifstream in("{input_bin}", std::ios::binary);
    if (!in) return 1;

    int n_samples = {N_SAMPLES};
    int n_features = {len(mean)};
    std::vector<float> features(n_samples * n_features);
    in.read(reinterpret_cast<char*>(features.data()), features.size() * sizeof(float));
    in.close();

    std::vector<float> preds(n_samples);
    for (int i = 0; i < n_samples; ++i) {{
        preds[i] = ns3::supervised_model::ScaleAndPredict(&features[i * n_features]);
    }}

    std::ofstream out("{output_bin}", std::ios::binary);
    if (!out) return 2;
    out.write(reinterpret_cast<const char*>(preds.data()), preds.size() * sizeof(float));
    out.close();

    return 0;
}}
"""
        with open(cpp_source, "w") as f:
            f.write(cpp_code)

        compile_cmd = ["g++", "-O3", cpp_source, "-o", bin_executable]
        comp_res = subprocess.run(compile_cmd, capture_output=True, text=True)
        assert comp_res.returncode == 0, f"Compilation failed: {comp_res.stderr}"

        run_res = subprocess.run([bin_executable], capture_output=True, text=True)
        assert run_res.returncode == 0, f"Execution failed: {run_res.stderr}"

        cpp_preds = np.fromfile(output_bin, dtype=np.float32)
        assert len(cpp_preds) == len(py_preds)

        abs_diff = np.abs(cpp_preds - py_preds)
        max_diff = np.max(abs_diff)
        mean_diff = np.mean(abs_diff)

        print(f"Tested {N_SAMPLES} samples.")
        print(f"Max Absolute Difference:  {max_diff:.8e}")
        print(f"Mean Absolute Difference: {mean_diff:.8e}")

        # Task 3.7 requires max difference < 1e-5 (accounting for float32 precision)
        assert max_diff < 1e-4, f"Discrepancy too large! Max diff: {max_diff}"
        print("PASS: Equivalence test passed successfully!")

if __name__ == "__main__":
    test_equivalence()
