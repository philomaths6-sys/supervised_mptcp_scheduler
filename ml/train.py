#!/usr/bin/env python3
"""
ml/train.py
Model Training and Validation with GroupKFold by Scenario.
Implements Tasks 3.1, 3.2, 3.3, 3.4, and 3.5 of the MPTCP Implementation Roadmap.
"""

import os
import json
import joblib
import pandas as pd
import numpy as np
from sklearn.linear_model import Ridge
from sklearn.tree import DecisionTreeRegressor
from sklearn.ensemble import RandomForestRegressor, ExtraTreesRegressor
from sklearn.model_selection import GroupKFold
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATASET_PATH = os.path.join(REPO_ROOT, "dataset", "processed", "dataset.csv")
SPLITS_PATH = os.path.join(REPO_ROOT, "dataset", "processed", "splits.json")
MODELS_DIR = os.path.join(REPO_ROOT, "ml", "models")

FEATURE_COLS = [
    "feat_0_rtt_ms",
    "feat_1_min_rtt_ms",
    "feat_2_bw_mbps",
    "feat_3_cwnd",
    "feat_4_inflight",
    "feat_5_avail_win",
    "feat_6_loss_rate",
    "feat_7_rtt_ratio",
    "feat_8_bw_share",
    "feat_9_win_headroom",
    "feat_10_rtt_inflation",
    "feat_11_n_paths"
]
TARGET_COL = "target_delay_ms"

def train_and_evaluate():
    os.makedirs(MODELS_DIR, exist_ok=True)
    print(f"Loading processed dataset from {DATASET_PATH}...")
    df = pd.read_csv(DATASET_PATH)
    
    with open(SPLITS_PATH, "r") as f:
        splits = json.load(f)
        
    train_scenarios = set(splits["train"])
    val_scenarios = set(splits["val"])
    
    # Filter train and validation sets strictly by scenario_id
    train_df = df[df["scenario_id"].isin(train_scenarios)].copy()
    val_df = df[df["scenario_id"].isin(val_scenarios)].copy()
    
    # If val_df is empty or small, take 20% of train scenarios
    if len(val_df) < 1000:
        unique_sc = list(train_df["scenario_id"].unique())
        n_val = max(2, int(len(unique_sc) * 0.2))
        val_sc = set(unique_sc[:n_val])
        train_sc = set(unique_sc[n_val:])
        val_df = train_df[train_df["scenario_id"].isin(val_sc)].copy()
        train_df = train_df[train_df["scenario_id"].isin(train_sc)].copy()
        
    print(f"Training set: {len(train_df)} rows across {train_df['scenario_id'].nunique()} scenarios.")
    print(f"Validation set: {len(val_df)} rows across {val_df['scenario_id'].nunique()} scenarios.")
    
    X_train = train_df[FEATURE_COLS].values
    y_train = train_df[TARGET_COL].values
    groups_train = train_df["scenario_id"].values
    
    X_val = val_df[FEATURE_COLS].values
    y_val = val_df[TARGET_COL].values
    
    # Calculate feature normalization stats for C++ export
    mean = np.mean(X_train, axis=0)
    scale = np.std(X_train, axis=0)
    scale[scale < 1e-4] = 1.0 # prevent div-by-zero
    
    # Standardize features
    X_train_scaled = (X_train - mean) / scale
    X_val_scaled = (X_val - mean) / scale
    

    # Candidate Models (Task 3.3)
    candidates = {
        "Ridge": Ridge(alpha=1.0),
        "DecisionTree": DecisionTreeRegressor(max_depth=6, min_samples_leaf=20, random_state=42),
        "RandomForest": RandomForestRegressor(n_estimators=5, max_depth=6, min_samples_leaf=20, random_state=42, n_jobs=-1),
        "ExtraTrees": ExtraTreesRegressor(n_estimators=8, max_depth=7, min_samples_leaf=8, random_state=42, n_jobs=-1)
    }
    
    print("\n--- Running 5-Fold GroupKFold Cross-Validation ---")
    gkf = GroupKFold(n_splits=min(5, train_df['scenario_id'].nunique()))
    results = {}
    
    for name, model in candidates.items():
        fold_maes = []
        for fold, (trn_idx, hld_idx) in enumerate(gkf.split(X_train_scaled, y_train, groups=groups_train)):
            X_f_trn, y_f_trn = X_train_scaled[trn_idx], y_train[trn_idx]
            X_f_hld, y_f_hld = X_train_scaled[hld_idx], y_train[hld_idx]
            
            model.fit(X_f_trn, y_f_trn)
            pred_hld = model.predict(X_f_hld)
            fold_maes.append(mean_absolute_error(y_f_hld, pred_hld))
            
        cv_mae = float(np.mean(fold_maes))
        
        # Fit on full training set
        model.fit(X_train_scaled, y_train)
        pred_val = model.predict(X_val_scaled)
        
        val_mae = float(mean_absolute_error(y_val, pred_val))
        val_rmse = float(np.sqrt(mean_squared_error(y_val, pred_val)))
        val_r2 = float(r2_score(y_val, pred_val))
        
        # Save model
        joblib_path = os.path.join(MODELS_DIR, f"model_{name.lower()}.joblib")
        joblib.dump({"model": model, "mean": mean, "scale": scale, "features": FEATURE_COLS}, joblib_path)
        
        results[name] = {
            "cv_mae": round(cv_mae, 4),
            "val_mae": round(val_mae, 4),
            "val_rmse": round(val_rmse, 4),
            "val_r2": round(val_r2, 4),
            "joblib_path": joblib_path
        }
        print(f"Model: {name:15s} | CV MAE: {cv_mae:7.3f} ms | Val MAE: {val_mae:7.3f} ms | Val RMSE: {val_rmse:7.3f} ms | Val R2: {val_r2:6.3f}")

    # Select best model (lowest validation MAE within latency/size budget)
    best_name = min(results, key=lambda k: results[k]["val_mae"])
    print(f"\nSelected Best Model: {best_name} (Val MAE: {results[best_name]['val_mae']} ms)")
    
    best_payload = joblib.load(results[best_name]["joblib_path"])
    joblib.dump(best_payload, os.path.join(MODELS_DIR, "best_model.joblib"))
    
    # Save training summary
    summary_path = os.path.join(MODELS_DIR, "model_summary.json")
    with open(summary_path, "w") as f:
        json.dump({
            "selected_model": best_name,
            "metrics": results,
            "mean": mean.tolist(),
            "scale": scale.tolist(),
            "features": FEATURE_COLS
        }, f, indent=2)
    print(f"Saved model training summary to {summary_path}")

if __name__ == "__main__":
    train_and_evaluate()
