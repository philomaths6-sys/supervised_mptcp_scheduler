#!/usr/bin/env python3
"""
ml/export_cpp.py
Export Trained Tree Ensemble to Header-Only Dependency-Free C++ Arrays.
Implements Task 3.6 of the MPTCP Implementation Roadmap.
"""

import os
import joblib
import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODEL_PATH = os.path.join(REPO_ROOT, "ml", "models", "best_model.joblib")
OUT_HEADER = os.path.join(REPO_ROOT, "adaptive_supervised_scheduler", "model_generated.h")

def export_model_to_cpp():
    print(f"Loading trained model from {MODEL_PATH}...")
    payload = joblib.load(MODEL_PATH)
    model = payload["model"]
    mean = payload["mean"]
    scale = payload["scale"]
    
    # Support RandomForestRegressor or DecisionTreeRegressor
    if hasattr(model, "estimators_"):
        trees = [est.tree_ for est in model.estimators_]
    elif hasattr(model, "tree_"):
        trees = [model.tree_]
    else:
        raise ValueError(f"Unsupported model type for tree export: {type(model)}")
        
    kNumFeatures = len(mean)
    kNumTrees = len(trees)
    
    kFeat = []
    kThr = []
    kLeft = []
    kRight = []
    kVal = []
    kRoot = []
    
    node_offset = 0
    for t_idx, tree in enumerate(trees):
        kRoot.append(node_offset)
        n_nodes = tree.node_count
        
        for n in range(n_nodes):
            feat = tree.feature[n]
            thr = tree.threshold[n]
            left = tree.children_left[n]
            right = tree.children_right[n]
            val = tree.value[n][0][0]
            
            if feat >= 0:
                # Internal node
                kFeat.append(int(feat))
                kThr.append(float(thr))
                kLeft.append(int(node_offset + left))
                kRight.append(int(node_offset + right))
                kVal.append(0.0)
            else:
                # Leaf node (feat < 0)
                kFeat.append(-1)
                kThr.append(0.0)
                kLeft.append(-1)
                kRight.append(-1)
                kVal.append(float(val))
                
        node_offset += n_nodes
        
    # Format arrays for C++
    mean_str = ", ".join([f"{x:.6f}f" for x in mean])
    scale_str = ", ".join([f"{x:.6f}f" for x in scale])
    feat_str = ", ".join([str(x) for x in kFeat])
    thr_str = ", ".join([f"{x:.6f}f" for x in kThr])
    left_str = ", ".join([str(x) for x in kLeft])
    right_str = ", ".join([str(x) for x in kRight])
    val_str = ", ".join([f"{x:.6f}f" for x in kVal])
    root_str = ", ".join([str(x) for x in kRoot])
    
    cpp_code = f"""/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// Generated automatically by ml/export_cpp.py. Do not edit manually.
// Embeds trained decision tree ensemble for ultra-low-latency in-kernel inference (< 5 us).
#ifndef MODEL_GENERATED_H
#define MODEL_GENERATED_H

namespace ns3 {{
namespace supervised_model {{

static const int kNumFeatures = {kNumFeatures};
static const int kNumTrees = {kNumTrees};

// Feature normalization constants
static const float kMean[kNumFeatures] = {{{mean_str}}};
static const float kScale[kNumFeatures] = {{{scale_str}}};

// Flat node arrays across all trees (kFeat[n] < 0 indicates leaf node)
static const short kFeat[] = {{{feat_str}}};
static const float kThr[] = {{{thr_str}}};
static const short kLeft[] = {{{left_str}}};
static const short kRight[] = {{{right_str}}};
static const float kVal[] = {{{val_str}}};
static const short kRoot[kNumTrees] = {{{root_str}}};

/**
 * \\brief Predict delivery delay (ms) on pre-scaled feature vector
 */
inline float Predict (const float* x)
{{
  float sum = 0.0f;
  for (int t = 0; t < kNumTrees; ++t)
    {{
      int n = kRoot[t];
      while (kFeat[n] >= 0)
        {{
          n = (x[kFeat[n]] <= kThr[n]) ? kLeft[n] : kRight[n];
        }}
      sum += kVal[n];
    }}
  return sum / static_cast<float> (kNumTrees);
}}

/**
 * \\brief Standardize raw features and evaluate ensemble prediction
 */
inline float ScaleAndPredict (const float* raw_features)
{{
  float x[kNumFeatures];
  for (int i = 0; i < kNumFeatures; ++i)
    {{
      x[i] = (raw_features[i] - kMean[i]) / kScale[i];
    }}
  return Predict (x);
}}

}} // namespace supervised_model
}} // namespace ns3

#endif /* MODEL_GENERATED_H */
"""
    with open(OUT_HEADER, "w") as f:
        f.write(cpp_code)
        
    size_kb = os.path.getsize(OUT_HEADER) / 1024.0
    print(f"Exported C++ model header to {OUT_HEADER}")
    print(f"Total Nodes: {node_offset}, Trees: {kNumTrees}, Header Size: {size_kb:.2f} KB (Budget: < 100 KB)")
    assert size_kb < 100.0, f"Generated model exceeds 100 KB budget: {size_kb} KB!"

if __name__ == "__main__":
    export_model_to_cpp()
