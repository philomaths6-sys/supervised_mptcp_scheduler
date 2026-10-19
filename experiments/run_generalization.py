#!/usr/bin/env python3
"""
experiments/run_generalization.py
Evaluates MPTCP Schedulers across 5 Generalization Dimensions (Phase 6):
- G0: In-Distribution baseline reference
- G1: Parameter Extrapolation (extreme disparity 16x, high loss 4.5%, rate asymmetry 10x)
- G2: Topology Shift (3-subflow heterogeneous topology)
- G3: Dynamic Shift (quality swap, bufferbloat delay spike, link outage & recovery)
- G4: Workload Shift (Short, medium, and large flow completion times)
- Computes Generalization Gap and Feature Permutation Importance
"""

import os
import sys
import subprocess
import json
import numpy as np
import pandas as pd
from scipy import stats
import joblib
from concurrent.futures import ProcessPoolExecutor, as_completed

PROTOCOLS = [
    "Standard-TCP",
    "minRTT",
    "RoundRobin",
    "BLEST",
    "ECF",
    "Supervised"
]

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NS3_DIR = os.path.join(REPO_ROOT, "ns-3-mptcp")
RESULTS_DIR = os.path.join(REPO_ROOT, "results", "data")
EVENTS_DIR = os.path.join(REPO_ROOT, "dataset", "events")

GENERALIZATION_TESTS = [
    # G0: In-Distribution Reference
    {
        "dim": "G0_InDistribution",
        "name": "G0_WiFi_Cellular",
        "topo": "WiFi_Cellular",
        "duration": 6.0,
        "extra_args": []
    },
    # G1: Parameter Extrapolation
    {
        "dim": "G1_ParameterExtrapolation",
        "name": "G1_ExtremeDisparity_16x",
        "topo": "WiFi_Cellular",
        "duration": 6.0,
        "extra_args": ["--path0Rate=15Mbps", "--path0Delay=5ms", "--path1Rate=6Mbps", "--path1Delay=80ms"]
    },
    {
        "dim": "G1_ParameterExtrapolation",
        "name": "G1_HighLoss_Environment",
        "topo": "WiFi_Cellular",
        "duration": 6.0,
        "extra_args": ["--path0Loss=0.035", "--path1Loss=0.045"]
    },
    {
        "dim": "G1_ParameterExtrapolation",
        "name": "G1_RateAsymmetry_10x",
        "topo": "WiFi_Cellular",
        "duration": 6.0,
        "extra_args": ["--path0Rate=30Mbps", "--path0Delay=10ms", "--path1Rate=3Mbps", "--path1Delay=30ms"]
    },
    # G2: Topology Shift
    {
        "dim": "G2_TopologyShift",
        "name": "G2_TriPath_Asymmetric",
        "topo": "TriPath",
        "duration": 6.0,
        "extra_args": []
    },
    # G3: Dynamic Shift
    {
        "dim": "G3_DynamicShift",
        "name": "G3_Quality_Swap",
        "topo": "Ethernet_WiFi",
        "duration": 9.0,
        "extra_args": [f"--events={os.path.join(EVENTS_DIR, 's0061_quality_swap.csv')}"]
    },
    {
        "dim": "G3_DynamicShift",
        "name": "G3_Delay_Spike",
        "topo": "WiFi_Cellular",
        "duration": 8.0,
        "extra_args": [f"--events={os.path.join(EVENTS_DIR, 's0069_delay_spike.csv')}"]
    },
    {
        "dim": "G3_DynamicShift",
        "name": "G3_Link_Outage",
        "topo": "WiFi_Cellular",
        "duration": 8.0,
        "extra_args": [f"--events={os.path.join(EVENTS_DIR, 's0070_outage.csv')}"]
    },
    # G4: Workload Shift (FCT)
    {
        "dim": "G4_WorkloadShift",
        "name": "G4_ShortFlow_500KB",
        "topo": "WiFi_Cellular",
        "duration": 10.0,
        "extra_args": ["--maxBytes=500000"]
    },
    {
        "dim": "G4_WorkloadShift",
        "name": "G4_MediumFlow_2MB",
        "topo": "WiFi_Cellular",
        "duration": 10.0,
        "extra_args": ["--maxBytes=2000000"]
    },
    {
        "dim": "G4_WorkloadShift",
        "name": "G4_LargeFlow_5MB",
        "topo": "WiFi_Cellular",
        "duration": 10.0,
        "extra_args": ["--maxBytes=5000000"]
    }
]

SEEDS = [1, 2, 3, 4, 5]  # 5 seeds per generalization point

def execute_single_run(cmd_args, extra_meta=None):
    bin_path = os.path.join(NS3_DIR, "build", "scratch", "run_experiment")
    env = os.environ.copy()
    lib_dir = os.path.join(NS3_DIR, "build", "lib")
    env["LD_LIBRARY_PATH"] = f"{lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"

    cmd = [bin_path] + cmd_args
    proc = subprocess.run(
        cmd,
        cwd=NS3_DIR,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    if proc.returncode != 0:
        return None
    stdout = proc.stdout
    s = stdout.find("{")
    e = stdout.rfind("}") + 1
    if s >= 0 and e > s:
        try:
            data = json.loads(stdout[s:e])
            if extra_meta:
                data.update(extra_meta)
            return data
        except Exception:
            return None
    return None

def compute_permutation_importance():
    print("\nComputing Feature Permutation Importance on unseen test scenarios...")
    model_path = os.path.join(REPO_ROOT, "ml", "models", "best_model.joblib")
    dataset_csv = os.path.join(REPO_ROOT, "dataset", "processed", "dataset.csv")
    splits_json = os.path.join(REPO_ROOT, "dataset", "processed", "splits.json")
    out_csv = os.path.join(RESULTS_DIR, "feature_importance.csv")

    if not os.path.exists(model_path) or not os.path.exists(dataset_csv):
        print("Model or dataset not found for permutation importance.")
        return

    payload = joblib.load(model_path)
    model = payload["model"]
    mean = payload["mean"]
    scale = payload["scale"]

    with open(splits_json, "r") as f:
        splits = json.load(f)
    test_scenarios = set(splits.get("val", []))

    df = pd.read_csv(dataset_csv)
    df_test = df[df["scenario_id"].isin(test_scenarios)]
    if len(df_test) == 0:
        df_test = df.sample(min(20000, len(df)), random_state=42)
    else:
        df_test = df_test.sample(min(20000, len(df_test)), random_state=42)

    feat_cols = [c for c in df.columns if c.startswith("feat_")]
    X_raw = df_test[feat_cols].values
    y_true = df_test["target_delay_ms"].values

    X_norm = (X_raw - mean) / scale
    base_preds = model.predict(X_norm)
    base_mae = np.mean(np.abs(base_preds - y_true))

    feat_names = [
        "RTT Ratio (to minRTT)", "Bandwidth Share", "Window Headroom",
        "Inflight / CWND", "Estimated Loss Rate", "Bufferbloat Inflation",
        "Min RTT Ratio", "Fastest RTT (conn)", "Total Window (conn)",
        "Inflight Ratio", "Path Index", "Active Subflows Count"
    ]

    importances = []
    np.random.seed(42)
    for col_idx in range(len(feat_cols)):
        X_perm = X_norm.copy()
        X_perm[:, col_idx] = np.random.permutation(X_perm[:, col_idx])
        perm_preds = model.predict(X_perm)
        perm_mae = np.mean(np.abs(perm_preds - y_true))
        mae_increase = perm_mae - base_mae
        rel_increase_pct = (mae_increase / base_mae) * 100.0

        f_name = feat_names[col_idx] if col_idx < len(feat_names) else feat_cols[col_idx]
        importances.append({
            "feature_index": col_idx,
            "feature_column": feat_cols[col_idx],
            "feature_name": f_name,
            "baseline_mae_ms": round(base_mae, 3),
            "permuted_mae_ms": round(perm_mae, 3),
            "mae_increase_ms": round(mae_increase, 3),
            "importance_score_pct": round(rel_increase_pct, 2)
        })

    df_imp = pd.DataFrame(importances).sort_values("importance_score_pct", ascending=False)
    df_imp.to_csv(out_csv, index=False)
    print(f"Saved feature permutation importance to: {out_csv}")
    print("\nFeature Importance Rankings:")
    for _, r in df_imp.head(6).iterrows():
        print(f" - {r['feature_name']:28s}: +{r['mae_increase_ms']:5.2f} ms MAE (+{r['importance_score_pct']:5.1f}%)")

def run_generalization_study(max_workers=10):
    os.makedirs(RESULTS_DIR, exist_ok=True)
    raw_csv = os.path.join(RESULTS_DIR, "generalization_results.csv")
    summary_csv = os.path.join(RESULTS_DIR, "generalization_summary.csv")
    gap_csv = os.path.join(RESULTS_DIR, "generalization_gap.csv")

    print("=" * 95)
    print("STARTING MPTCP SCHEDULER GENERALIZATION STUDY (PHASE 6)")
    print(f"Dimensions: G0 (In-Dist), G1 (Param Extrap), G2 (Topo Shift), G3 (Dynamic Shift), G4 (Workload Shift)")
    print(f"Test cases: {len(GENERALIZATION_TESTS)} | Schedulers: {len(PROTOCOLS)} | Seeds: {len(SEEDS)}")
    print("=" * 95)

    tasks = []
    for g_test in GENERALIZATION_TESTS:
        for proto in PROTOCOLS:
            for seed in SEEDS:
                args = [
                    f"--topology={g_test['topo']}",
                    f"--protocol={proto}",
                    f"--duration={g_test['duration']}",
                    f"--seed={seed}",
                    "--json=true"
                ] + g_test["extra_args"]
                meta = {
                    "dimension": g_test["dim"],
                    "test_case": g_test["name"],
                    "protocol": proto,
                    "seed": seed
                }
                tasks.append((args, meta))

    print(f"Executing {len(tasks)} generalization simulation runs...")
    results = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(execute_single_run, args, meta): meta for args, meta in tasks}
        completed = 0
        for f in as_completed(futures):
            res = f.result()
            if res:
                results.append(res)
            completed += 1
            if completed % 50 == 0 or completed == len(tasks):
                print(f" Progress: {completed}/{len(tasks)} runs completed ({len(results)} valid).")

    df_gen = pd.DataFrame(results)
    df_gen.to_csv(raw_csv, index=False)
    print(f"Saved raw generalization runs to: {raw_csv}")

    # Summary Table across seeds
    summary_rows = []
    for g_test in GENERALIZATION_TESTS:
        t_name = g_test["name"]
        dim = g_test["dim"]
        for proto in PROTOCOLS:
            sub = df_gen[(df_gen["test_case"] == t_name) & (df_gen["protocol"] == proto)]
            if len(sub) == 0:
                continue
            summary_rows.append({
                "dimension": dim,
                "test_case": t_name,
                "protocol": proto,
                "samples": len(sub),
                "goodput_mean_mbps": round(sub["sessionGoodputMbps"].mean(), 3),
                "goodput_std_mbps": round(sub["sessionGoodputMbps"].std(), 3),
                "throughput_mean_mbps": round(sub["activeThroughputMbps"].mean(), 3),
                "hol_delay_mean_ms": round(sub["holDelayMs"].mean(), 3),
                "mean_delay_ms": round(sub["meanDelayMs"].mean(), 3),
                "loss_percent": round(sub["packetLossPercent"].mean(), 3),
                "fct_sec": round(sub["flowCompletionTimeSec"].mean(), 3) if "flowCompletionTimeSec" in sub else 0.0
            })

    df_summary = pd.DataFrame(summary_rows)
    df_summary.to_csv(summary_csv, index=False)
    print(f"Saved generalization summary to: {summary_csv}")

    # Generalization Gap Analysis relative to G0 In-Distribution
    g0_sub = df_summary[df_summary["dimension"] == "G0_InDistribution"]
    g0_goodput = {r["protocol"]: r["goodput_mean_mbps"] for _, r in g0_sub.iterrows()}

    gap_rows = []
    for _, row in df_summary.iterrows():
        if row["dimension"] == "G0_InDistribution":
            continue
        proto = row["protocol"]
        g0_val = g0_goodput.get(proto, 1.0)
        curr_val = row["goodput_mean_mbps"]
        degradation_pct = ((curr_val - g0_val) / g0_val * 100.0) if g0_val > 0 else 0.0

        gap_rows.append({
            "dimension": row["dimension"],
            "test_case": row["test_case"],
            "protocol": proto,
            "in_distribution_goodput": round(g0_val, 3),
            "shifted_goodput": round(curr_val, 3),
            "relative_change_percent": round(degradation_pct, 2)
        })

    df_gap = pd.DataFrame(gap_rows)
    df_gap.to_csv(gap_csv, index=False)
    print(f"Saved generalization gap analysis to: {gap_csv}")

    # Permutation importance
    compute_permutation_importance()
    print("\nGENERALIZATION STUDY COMPLETE!")

if __name__ == "__main__":
    run_generalization_study(max_workers=10)
