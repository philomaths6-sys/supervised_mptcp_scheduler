#!/usr/bin/env python3
"""
experiments/run_benchmarks.py
Comprehensive Multi-Topology MPTCP Comparative Evaluation Matrix.
Implements Phase 5 of the MPTCP Implementation Roadmap:
- Evaluates 6 Schedulers: Standard-TCP, minRTT, RoundRobin, BLEST, ECF, Supervised.
- Scenarios: WiFi_Cellular, Ethernet_WiFi, TriPath, QuadPath, PentaPath, WiFi_Cellular_Handover.
- Delay Disparity Sweep: 1.0x, 2.0x, 3.0x, 5.0x, 8.0x, 16.0x.
- Statistical Rigor: >= 10 seeds per cell (Seeds 1..10).
- Computes Mean, 95% Confidence Intervals (t-distribution), and paired Wilcoxon signed-rank tests.
"""

import os
import sys
import subprocess
import json
import numpy as np
import pandas as pd
from scipy import stats
from concurrent.futures import ProcessPoolExecutor, as_completed

PROTOCOLS = [
    "Standard-TCP",
    "minRTT",
    "RoundRobin",
    "BLEST",
    "ECF",
    "Supervised"
]

SCENARIOS = [
    {
        "name": "WiFi_Cellular",
        "topo_arg": "WiFi_Cellular",
        "handover": False,
        "description": "Mobile Multi-Homed: Wi-Fi (12M/15ms, 0.5% loss) + LTE (6M/45ms, 1.5% loss) [18 Mbps]"
    },
    {
        "name": "Ethernet_WiFi",
        "topo_arg": "Ethernet_WiFi",
        "handover": False,
        "description": "Desktop/Enterprise: Ethernet (25M/5ms, 0.01% loss) + Wi-Fi (15M/20ms, 0.8% loss) [40 Mbps]"
    },
    {
        "name": "TriPath",
        "topo_arg": "TriPath",
        "handover": False,
        "description": "Multi-Access Gateway: Ethernet (30M/5ms) + Wi-Fi (15M/20ms) + 5G (10M/50ms) [55 Mbps]"
    },
    {
        "name": "QuadPath",
        "topo_arg": "QuadPath",
        "handover": False,
        "description": "Quad-Interface Multi-Homed: Ethernet (30M/5ms) + WiFi (20M/15ms) + 5G (20M/20ms) + LTE (10M/35ms) [80 Mbps]"
    },
    {
        "name": "PentaPath",
        "topo_arg": "PentaPath",
        "handover": False,
        "description": "5-Interface Multi-Homed: Ethernet (30M/5ms) + WiFi6 (20M/15ms) + 5G-mmWave (25M/10ms) + LTE (15M/25ms) + SatCom (5M/60ms) [95 Mbps]"
    },
    {
        "name": "WiFi_Cellular_Handover",
        "topo_arg": "WiFi_Cellular",
        "handover": True,
        "description": "Dynamic Fading: Wi-Fi + LTE with 8% Wi-Fi packet drop spike at t=[4s, 7s]"
    }
]

DISPARITY_FACTORS = [1.0, 2.0, 3.0, 5.0, 8.0, 16.0]
SEEDS = list(range(1, 11))  # Seeds 1 to 10 (10 seeds per cell)

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NS3_DIR = os.path.join(REPO_ROOT, "ns-3-mptcp")
RESULTS_DIR = os.path.join(REPO_ROOT, "results", "data")

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

def compute_ci95(series):
    series = np.array([x for x in series if np.isfinite(x)])
    n = len(series)
    if n < 2:
        return 0.0
    sem = stats.sem(series)
    ci = sem * stats.t.ppf((1 + 0.95) / 2.0, n - 1)
    return ci if np.isfinite(ci) else 0.0

def run_all_benchmarks(duration=6.0, max_workers=10):
    os.makedirs(RESULTS_DIR, exist_ok=True)
    bench_csv = os.path.join(RESULTS_DIR, "multitopology_benchmark.csv")
    summary_csv = os.path.join(RESULTS_DIR, "benchmark_summary.csv")
    wilcoxon_csv = os.path.join(RESULTS_DIR, "wilcoxon_tests.csv")
    disparity_csv = os.path.join(RESULTS_DIR, "disparity_sweep.csv")
    disparity_summary_csv = os.path.join(RESULTS_DIR, "disparity_summary.csv")

    print("=" * 95)
    print("STARTING RIGOROUS MULTI-TOPOLOGY MPTCP COMPARATIVE EVALUATION MATRIX (PHASE 5)")
    print(f"Schedulers: {PROTOCOLS}")
    print(f"Scenarios:  {[s['name'] for s in SCENARIOS]}")
    print(f"Seeds:      {len(SEEDS)} seeds per cell (Seeds 1..{len(SEEDS)})")
    print(f"Parallel:   {max_workers} worker processes")
    print("=" * 95)

    # 1. Main Scenarios Runs
    tasks = []
    for scen in SCENARIOS:
        for proto in PROTOCOLS:
            for seed in SEEDS:
                args = [
                    f"--topology={scen['topo_arg']}",
                    f"--protocol={proto}",
                    f"--duration={duration}",
                    f"--handover={'true' if scen['handover'] else 'false'}",
                    f"--seed={seed}",
                    "--json=true"
                ]
                meta = {
                    "scenario": scen["name"],
                    "handover": scen["handover"],
                    "seed": seed,
                    "protocol": proto
                }
                tasks.append((args, meta))

    print(f"Executing {len(tasks)} main benchmark runs...")
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

    df_bench = pd.DataFrame(results)
    df_bench.to_csv(bench_csv, index=False)
    print(f"Saved raw benchmark runs ({len(df_bench)} rows) to: {bench_csv}")

    # Compute Summary Table with 95% Confidence Intervals
    summary_rows = []
    metric_cols = [
        "activeThroughputMbps", "sessionGoodputMbps", "aggregationEfficiencyPercent",
        "holDelayMs", "meanDelayMs", "packetLossPercent", "jitterMs", "flowCompletionTimeSec"
    ]

    for scen in SCENARIOS:
        s_name = scen["name"]
        for proto in PROTOCOLS:
            sub = df_bench[(df_bench["scenario"] == s_name) & (df_bench["protocol"] == proto)]
            if len(sub) == 0:
                continue
            row = {
                "scenario": s_name,
                "protocol": proto,
                "samples": len(sub)
            }
            for col in metric_cols:
                if col in sub.columns:
                    mean_val = sub[col].mean()
                    std_val = sub[col].std()
                    ci95_val = compute_ci95(sub[col])
                    row[f"{col}_mean"] = round(mean_val, 4)
                    row[f"{col}_std"] = round(std_val, 4)
                    row[f"{col}_ci95"] = round(ci95_val, 4)
            summary_rows.append(row)

    df_summary = pd.DataFrame(summary_rows)
    df_summary.to_csv(summary_csv, index=False)
    print(f"Saved aggregated benchmark metrics with 95% CIs to: {summary_csv}")

    # Compute Paired Wilcoxon Signed-Rank Tests (Supervised vs Baselines)
    wilcoxon_rows = []
    for scen in SCENARIOS:
        s_name = scen["name"]
        sup_sub = df_bench[(df_bench["scenario"] == s_name) & (df_bench["protocol"] == "Supervised")].sort_values("seed")
        if len(sup_sub) < 5:
            continue

        for baseline in [p for p in PROTOCOLS if p != "Supervised"]:
            base_sub = df_bench[(df_bench["scenario"] == s_name) & (df_bench["protocol"] == baseline)].sort_values("seed")
            # Align seeds
            common_seeds = sorted(list(set(sup_sub["seed"]).intersection(set(base_sub["seed"]))))
            if len(common_seeds) < 5:
                continue

            sup_aligned = sup_sub[sup_sub["seed"].isin(common_seeds)]
            base_aligned = base_sub[base_sub["seed"].isin(common_seeds)]

            for col in ["sessionGoodputMbps", "holDelayMs", "packetLossPercent", "aggregationEfficiencyPercent"]:
                v_sup = sup_aligned[col].values
                v_base = base_aligned[col].values
                diff = v_sup - v_base

                if np.all(diff == 0):
                    p_val = 1.0
                    stat = 0.0
                else:
                    try:
                        stat, p_val = stats.wilcoxon(v_sup, v_base)
                    except Exception:
                        stat, p_val = 0.0, 1.0

                mean_sup = np.mean(v_sup)
                mean_base = np.mean(v_base)
                delta_pct = ((mean_sup - mean_base) / mean_base * 100.0) if mean_base > 0 else 0.0

                wilcoxon_rows.append({
                    "scenario": s_name,
                    "baseline": baseline,
                    "metric": col,
                    "supervised_mean": round(mean_sup, 3),
                    "baseline_mean": round(mean_base, 3),
                    "delta_percent": round(delta_pct, 2),
                    "p_value": round(p_val, 5),
                    "statistically_significant": bool(p_val < 0.05)
                })

    df_wilcoxon = pd.DataFrame(wilcoxon_rows)
    df_wilcoxon.to_csv(wilcoxon_csv, index=False)
    print(f"Saved paired Wilcoxon statistical significance tests to: {wilcoxon_csv}")

    # 2. Delay Disparity Sweep Runs
    print("\n" + "=" * 95)
    print(f"RUNNING DELAY DISPARITY SWEEP (Factors: {DISPARITY_FACTORS}, 10 Seeds)")
    print("=" * 95)
    disp_tasks = []
    for disp in DISPARITY_FACTORS:
        for proto in PROTOCOLS:
            for seed in SEEDS:
                args = [
                    "--topology=WiFi_Cellular",
                    f"--protocol={proto}",
                    f"--duration={duration}",
                    f"--disparity={disp}",
                    f"--seed={seed}",
                    "--json=true"
                ]
                meta = {
                    "disparity": disp,
                    "protocol": proto,
                    "seed": seed
                }
                disp_tasks.append((args, meta))

    disp_results = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(execute_single_run, args, meta): meta for args, meta in disp_tasks}
        completed = 0
        for f in as_completed(futures):
            res = f.result()
            if res:
                disp_results.append(res)
            completed += 1
            if completed % 50 == 0 or completed == len(disp_tasks):
                print(f" Disparity Progress: {completed}/{len(disp_tasks)} runs completed ({len(disp_results)} valid).")

    df_disp = pd.DataFrame(disp_results)
    df_disp.to_csv(disparity_csv, index=False)
    print(f"Saved raw disparity sweep runs ({len(df_disp)} rows) to: {disparity_csv}")

    disp_summary = []
    for disp in DISPARITY_FACTORS:
        for proto in PROTOCOLS:
            sub = df_disp[(df_disp["disparity"] == disp) & (df_disp["protocol"] == proto)]
            if len(sub) == 0:
                continue
            row = {
                "disparity": disp,
                "protocol": proto,
                "samples": len(sub)
            }
            for col in metric_cols:
                if col in sub.columns:
                    row[f"{col}_mean"] = round(sub[col].mean(), 4)
                    row[f"{col}_std"] = round(sub[col].std(), 4)
                    row[f"{col}_ci95"] = round(compute_ci95(sub[col]), 4)
            disp_summary.append(row)

    df_disp_sum = pd.DataFrame(disp_summary)
    df_disp_sum.to_csv(disparity_summary_csv, index=False)
    print(f"Saved disparity sweep summary with 95% CIs to: {disparity_summary_csv}")
    print("\nBENCHMARK MATRIX EXECUTION COMPLETE!")

if __name__ == "__main__":
    run_all_benchmarks(duration=6.0, max_workers=10)
