#!/usr/bin/env python3
"""
experiments/run_separated_evaluations.py
Executes two distinct, rigorous empirical benchmark suites:
1. Dynamic Link Break Benchmark:
   - Scenario: WiFi_Cellular with severe link break on Path 0 at t=[3.0s, 6.0s] (duration 10s).
   - Protocols: Single-Path TCP (NewReno), minRTT, RoundRobin, BLEST, ECF, Supervised.
   - Generates 100ms continuous time-series traces and aggregated metrics.
2. Overall Multi-Scenario Benchmark:
   - Scenarios: WiFi_Cellular, Ethernet_WiFi, TriPath, QuadPath, PentaPath (duration 6.0s).
   - Evaluates multi-path aggregation across 2 to 5 network interfaces.
   - Protocols: Single-Path TCP (NewReno), minRTT, RoundRobin, BLEST, ECF, Supervised.
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

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NS3_DIR = os.path.join(REPO_ROOT, "ns-3-mptcp")
DATA_DIR = os.path.join(REPO_ROOT, "results", "data")
TS_DIR = os.path.join(REPO_ROOT, "results", "timeseries")

os.makedirs(DATA_DIR, exist_ok=True)
os.makedirs(TS_DIR, exist_ok=True)

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
        print(f"Error in run: {' '.join(cmd_args)}\nStderr: {proc.stderr[:200]}")
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
        except Exception as ex:
            print(f"JSON decode error: {ex}")
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

def run_dynamic_break_suite(seeds=range(1, 6), duration=10.0, max_workers=7):
    print("=" * 80)
    print("1. EXECUTING DYNAMIC LINK BREAK BENCHMARK SUITE")
    print(f"   Duration: {duration}s | Outage Window: t=[3.0s, 6.0s] | Seeds: {list(seeds)}")
    print(f"   Protocols: {PROTOCOLS}")
    print("=" * 80)

    tasks = []
    for proto in PROTOCOLS:
        for seed in seeds:
            ts_path = ""
            # Generate 100ms time series on seed 1
            if seed == 1:
                ts_path = os.path.join(TS_DIR, f"dynamic_break_{proto}.csv")

            args = [
                f"--protocol={proto}",
                "--topology=WiFi_Cellular",
                f"--duration={duration}",
                "--handover=true",
                f"--seed={seed}",
                "--json=true"
            ]
            if ts_path:
                args.append(f"--timeSeries={ts_path}")

            meta = {
                "benchmark": "dynamic_break",
                "protocol": proto,
                "tcpVariant": "TCP NewReno (SACK)" if proto == "Standard-TCP" else proto,
                "seed": seed,
                "duration": duration,
                "outageWindow": "3.0s-6.0s"
            }
            tasks.append((args, meta))

    results = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(execute_single_run, args, meta): meta for args, meta in tasks}
        completed = 0
        for f in as_completed(futures):
            res = f.result()
            if res:
                results.append(res)
            completed += 1
            print(f"   Dynamic Break Progress: {completed}/{len(tasks)} runs complete", end="\r")
    print(f"\n   Successfully completed {len(results)}/{len(tasks)} dynamic break runs.")

    df_break = pd.DataFrame(results)
    csv_path = os.path.join(DATA_DIR, "dynamic_break_benchmark.csv")
    df_break.to_csv(csv_path, index=False)
    print(f"   Saved raw dynamic break dataset to: {csv_path}")

    # Compute Summary
    summary_rows = []
    metrics = ["activeThroughputMbps", "sessionGoodputMbps", "packetLossPercent", "meanDelayMs",
               "holDelayMs", "jitterMs", "path0Bytes", "path1Bytes", "totalBytesReceived"]
    for proto in PROTOCOLS:
        sub = df_break[df_break["protocol"] == proto]
        if len(sub) == 0:
            continue
        row = {
            "protocol": proto,
            "tcpVariant": "Single-Path TCP (NewReno)" if proto == "Standard-TCP" else proto,
            "samples": len(sub)
        }
        for m in metrics:
            if m in sub.columns:
                row[f"{m}_mean"] = round(sub[m].mean(), 4)
                row[f"{m}_std"] = round(sub[m].std(), 4)
                row[f"{m}_ci95"] = round(compute_ci95(sub[m]), 4)
        summary_rows.append(row)

    df_summary = pd.DataFrame(summary_rows)
    summary_csv = os.path.join(DATA_DIR, "dynamic_break_summary.csv")
    df_summary.to_csv(summary_csv, index=False)
    print(f"   Saved dynamic break summary to: {summary_csv}")
    return df_break, df_summary

def run_overall_multitopology_suite(seeds=range(1, 6), duration=6.0, max_workers=7):
    scenarios = [
        {"name": "WiFi_Cellular", "topo": "WiFi_Cellular", "cap": 30.0},
        {"name": "Ethernet_WiFi", "topo": "Ethernet_WiFi", "cap": 40.0},
        {"name": "TriPath", "topo": "TriPath", "cap": 55.0},
        {"name": "QuadPath", "topo": "QuadPath", "cap": 80.0},
        {"name": "PentaPath", "topo": "PentaPath", "cap": 95.0}
    ]

    print("\n" + "=" * 80)
    print("2. EXECUTING OVERALL MULTI-SCENARIO BENCHMARK SUITE")
    print(f"   Duration: {duration}s | Topologies: {[s['name'] for s in scenarios]} | Seeds: {list(seeds)}")
    print(f"   Protocols: {PROTOCOLS}")
    print("=" * 80)

    tasks = []
    for scen in scenarios:
        for proto in PROTOCOLS:
            for seed in seeds:
                args = [
                    f"--protocol={proto}",
                    f"--topology={scen['topo']}",
                    f"--duration={duration}",
                    "--handover=false",
                    f"--seed={seed}",
                    "--json=true"
                ]
                meta = {
                    "benchmark": "overall_multitopology",
                    "scenario": scen["name"],
                    "topology": scen["topo"],
                    "capacityMbps": scen["cap"],
                    "protocol": proto,
                    "tcpVariant": "TCP NewReno (SACK)" if proto == "Standard-TCP" else proto,
                    "seed": seed,
                    "duration": duration
                }
                tasks.append((args, meta))

    results = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(execute_single_run, args, meta): meta for args, meta in tasks}
        completed = 0
        for f in as_completed(futures):
            res = f.result()
            if res:
                results.append(res)
            completed += 1
            print(f"   Overall Suite Progress: {completed}/{len(tasks)} runs complete", end="\r")
    print(f"\n   Successfully completed {len(results)}/{len(tasks)} overall runs.")

    df_overall = pd.DataFrame(results)
    csv_path = os.path.join(DATA_DIR, "multitopology_overall_benchmark.csv")
    df_overall.to_csv(csv_path, index=False)
    print(f"   Saved raw overall multi-scenario dataset to: {csv_path}")

    # Compute Summary
    summary_rows = []
    metrics = ["activeThroughputMbps", "sessionGoodputMbps", "aggregationEfficiencyPercent",
               "packetLossPercent", "meanDelayMs", "holDelayMs", "jitterMs", "totalBytesReceived"]
    for scen in scenarios:
        s_name = scen["name"]
        for proto in PROTOCOLS:
            sub = df_overall[(df_overall["scenario"] == s_name) & (df_overall["protocol"] == proto)]
            if len(sub) == 0:
                continue
            row = {
                "scenario": s_name,
                "protocol": proto,
                "tcpVariant": "Single-Path TCP (NewReno)" if proto == "Standard-TCP" else proto,
                "samples": len(sub)
            }
            for m in metrics:
                if m in sub.columns:
                    row[f"{m}_mean"] = round(sub[m].mean(), 4)
                    row[f"{m}_std"] = round(sub[m].std(), 4)
                    row[f"{m}_ci95"] = round(compute_ci95(sub[m]), 4)
            summary_rows.append(row)

    df_summary = pd.DataFrame(summary_rows)
    summary_csv = os.path.join(DATA_DIR, "multitopology_overall_summary.csv")
    df_summary.to_csv(summary_csv, index=False)
    print(f"   Saved overall multi-scenario summary to: {summary_csv}")
    return df_overall, df_summary

if __name__ == '__main__':
    run_dynamic_break_suite(seeds=range(1, 6), duration=10.0, max_workers=7)
    run_overall_multitopology_suite(seeds=range(1, 6), duration=6.0, max_workers=7)
    print("\nAll separated benchmark suites successfully executed and saved.")
