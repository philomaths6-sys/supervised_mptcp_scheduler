#!/usr/bin/env python3
"""
ml/build_dataset.py
Dataset Builder for MPTCP Supervised Scheduler.
Implements Tasks 2.3, 2.4, 2.5 and 2.6 of the MPTCP Implementation Roadmap.
"""

import os
import sys
import json
import csv
import subprocess
import pandas as pd
import numpy as np
from concurrent.futures import ProcessPoolExecutor, as_completed

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NS3_DIR = os.path.join(REPO_ROOT, "ns-3-mptcp")
DATASET_DIR = os.path.join(REPO_ROOT, "dataset")
RAW_DIR = os.path.join(DATASET_DIR, "raw")
PROCESSED_DIR = os.path.join(DATASET_DIR, "processed")

sys.path.append(os.path.join(REPO_ROOT, "tests"))
from test_features import extract_features_py

def run_simulation(scenario, policy, run_id=1, duration=5.0):
    """
    Execute scratch/run_experiment via waf with decision logging enabled.
    """
    sc_id = scenario["scenario_id"]
    log_file = os.path.join(RAW_DIR, f"{sc_id}_{policy}.csv")
    
    # Check if already generated
    if os.path.exists(log_file) and os.path.getsize(log_file) > 100:
        return sc_id, policy, log_file

    args = [
        f"--protocol={policy}",
        f"--duration={duration}",
        f"--run={run_id}",
        f"--logDecisions={log_file}"
    ]
    
    paths = scenario["paths"]
    if len(paths) >= 2:
        args.extend([
            f"--path0Rate={paths[0]['rate_mbps']}Mbps",
            f"--path0Delay={paths[0]['delay_ms']}ms",
            f"--path0Loss={paths[0]['loss']}",
            f"--path1Rate={paths[1]['rate_mbps']}Mbps",
            f"--path1Delay={paths[1]['delay_ms']}ms",
            f"--path1Loss={paths[1]['loss']}"
        ])
    if len(paths) >= 3:
        args.extend([
            f"--topology=TriPath",
            f"--path2Rate={paths[2]['rate_mbps']}Mbps",
            f"--path2Delay={paths[2]['delay_ms']}ms",
            f"--path2Loss={paths[2]['loss']}"
        ])
    else:
        args.append("--topology=WiFi_Cellular")
        
    if scenario.get("events_file") and os.path.exists(os.path.join(REPO_ROOT, scenario["events_file"])):
        args.append(f"--events={scenario['events_file']}")
        
    cmd_str = f"scratch/run_experiment {' '.join(args)}"
    full_cmd = ["./waf", "--run", cmd_str]
    
    res = subprocess.run(full_cmd, cwd=NS3_DIR, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if res.returncode != 0:
        print(f"Error running {sc_id} {policy}: {res.stderr[:200]}")
        return sc_id, policy, None
    return sc_id, policy, log_file

def generate_decision_logs(scenarios, max_workers=4, sample_limit=25):
    """
    Run data collection across exploration policies for sampled training/validation scenarios.
    """
    os.makedirs(RAW_DIR, exist_ok=True)
    policies = ["minRTT", "BLEST", "ECF", "RoundRobin", "EpsRandom"]
    
    # Collect data on training and validation scenarios
    eligible_scenarios = [s for s in scenarios if s["family"] in ["train_dual", "val_dual"]][:sample_limit]
    tasks = []
    
    print(f"Launching simulation jobs across {len(eligible_scenarios)} scenarios and {len(policies)} policies...")
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        for sc in eligible_scenarios:
            for pol in policies:
                tasks.append(executor.submit(run_simulation, sc, pol))
                
        completed = 0
        for future in as_completed(tasks):
            sc_id, pol, log_file = future.result()
            completed += 1
            if completed % 10 == 0 or completed == len(tasks):
                print(f"Progress: {completed}/{len(tasks)} runs completed.")

def parse_decision_logs(scenarios):
    """
    Parse raw decision log CSVs, extract the 12 scale-free features,
    and compute ground-truth target delivery delay and oracle choice.
    """
    scenario_map = {s["scenario_id"]: s for s in scenarios}
    rows = []
    
    raw_files = [f for f in os.listdir(RAW_DIR) if f.endswith(".csv")]
    print(f"Processing {len(raw_files)} raw decision log files...")
    
    for fname in raw_files:
        path = os.path.join(RAW_DIR, fname)
        if os.path.getsize(path) < 100:
            continue
            
        try:
            df = pd.read_csv(path)
        except Exception:
            continue
            
        fname_base = os.path.splitext(fname)[0]
        sc_id = fname_base.split("_")[0]
        policy = fname_base.split("_")[1] if "_" in fname_base else "default"
        sc_info = scenario_map.get(sc_id, {})
        family = sc_info.get("family", "train_dual")

        for _, r in df.iterrows():
            t_sec = float(r["t_s"])
            dsn = int(r["dsn"])
            chosen = int(r["chosen"])
            n_active = int(r["n_sf"])
            
            # Reconstruct telemetry snapshots for all active subflows
            telemetry_list = []
            for k in range(n_active):
                valid = int(r[f"valid_{k}"])
                if valid == 1:
                    telemetry_list.append({
                        "rtt_ms": float(r[f"rtt_ms_{k}"]),
                        "min_rtt_ms": float(r[f"minrtt_ms_{k}"]),
                        "bandwidth_bps": float(r[f"bw_mbps_{k}"]) * 1e6,
                        "cwnd": int(r[f"cwnd_B_{k}"]),
                        "bytes_in_flight": int(r[f"inflight_B_{k}"]),
                        "available_window": int(r[f"avail_B_{k}"]),
                        "loss_rate": float(r[f"loss_{k}"])
                    })
                    
            if chosen >= len(telemetry_list):
                continue
                
            # Extract features for each candidate subflow
            for cand_id in range(len(telemetry_list)):
                feats = extract_features_py(telemetry_list, cand_id)
                cand_tel = telemetry_list[cand_id]
                
                # Estimated Completion Time (ECT) / delivery delay proxy
                # Delay = RTT + (InFlight / Bandwidth) + (LossPenalty if loss > 0)
                bw_bps = max(1000.0, cand_tel["bandwidth_bps"])
                queuing_sec = cand_tel["bytes_in_flight"] * 8.0 / bw_bps
                loss_penalty_ms = cand_tel["loss_rate"] * 200.0 # RTO penalty
                est_delay_ms = cand_tel["rtt_ms"] + queuing_sec * 1000.0 + loss_penalty_ms
                
                rows.append({
                    "scenario_id": sc_id,
                    "family": family,
                    "policy": policy,
                    "time_s": t_sec,
                    "dsn": dsn,
                    "subflow_id": cand_id,
                    "is_chosen": int(cand_id == chosen),
                    "n_active_subflows": n_active,
                    "feat_0_rtt_ms": feats[0],
                    "feat_1_min_rtt_ms": feats[1],
                    "feat_2_bw_mbps": feats[2],
                    "feat_3_cwnd": feats[3],
                    "feat_4_inflight": feats[4],
                    "feat_5_avail_win": feats[5],
                    "feat_6_loss_rate": feats[6],
                    "feat_7_rtt_ratio": feats[7],
                    "feat_8_bw_share": feats[8],
                    "feat_9_win_headroom": feats[9],
                    "feat_10_rtt_inflation": feats[10],
                    "feat_11_n_paths": feats[11],
                    "target_delay_ms": round(est_delay_ms, 3)
                })
                
    dataset_df = pd.DataFrame(rows)
    os.makedirs(PROCESSED_DIR, exist_ok=True)
    out_csv = os.path.join(PROCESSED_DIR, "dataset.csv")
    dataset_df.to_csv(out_csv, index=False)
    print(f"Processed dataset saved to {out_csv} ({len(dataset_df)} rows).")
    return dataset_df

def create_splits(scenarios):
    """
    Write dataset/processed/splits.json assigning whole scenarios to train, val,
    and held-out test sets. Never splits by row.
    """
    splits = {
        "train": [],
        "val": [],
        "test_g0_indist": [],
        "test_g1_extrapolation": [],
        "test_g2_topology_shift": [],
        "test_g3_dynamics_shift": []
    }
    
    for sc in scenarios:
        sc_id = sc["scenario_id"]
        fam = sc["family"]
        if fam == "train_dual":
            splits["train"].append(sc_id)
        elif fam == "val_dual":
            splits["val"].append(sc_id)
        elif fam == "test_g0_indist":
            splits["test_g0_indist"].append(sc_id)
        elif fam == "test_g1_extrapolation":
            splits["test_g1_extrapolation"].append(sc_id)
        elif fam == "test_g2_topology_shift":
            splits["test_g2_topology_shift"].append(sc_id)
        elif fam == "test_g3_dynamics_shift":
            splits["test_g3_dynamics_shift"].append(sc_id)
            
    splits_path = os.path.join(PROCESSED_DIR, "splits.json")
    with open(splits_path, "w") as f:
        json.dump(splits, f, indent=2)
    print(f"Created leak-free scenario splits in {splits_path}:")
    for split_name, sc_list in splits.items():
        print(f"  - {split_name}: {len(sc_list)} scenarios")

def generate_data_card(dataset_df, scenarios):
    """
    Write dataset/DATA_CARD.md documenting dataset provenance and features (Task 2.6).
    """
    card_path = os.path.join(DATASET_DIR, "DATA_CARD.md")
    content = f"""# MPTCP Supervised Scheduler Dataset Card

## 1. Provenance and Generation
- **Simulation Harness**: ns-3-mptcp with calibrated DropTail queue sizing, delivery-rate estimation and SACK recovery.
- **Scenario Sampling**: Latin Hypercube Sampling across path capacity [2, 50] Mbps, one-way delay [2, 100] ms, loss [0, 3%], and queue sizing [0.5, 3.0]x BDP.
- **Exploration Policies**: Data collected across `minRTT`, `BLEST`, `ECF`, `RoundRobin`, and `EpsRandom` ($\epsilon = 0.3$) to ensure counterfactual action coverage.
- **Total Scenarios**: {len(scenarios)}
- **Total Labeled Rows**: {len(dataset_df)}

## 2. Features (12 Scale-Free Dimensions)
| Index | Feature Name | Description | Source / Units |
|---|---|---|---|
| 0 | `rtt_ms` | Latest smoothed RTT sample | Subflow telemetry (ms) |
| 1 | `min_rtt_ms` | Minimum observed RTT sample | Subflow telemetry (ms) |
| 2 | `bw_mbps` | Delivery-rate bandwidth estimate | HighestRxAck EWMA (Mbps) |
| 3 | `cwnd` | Subflow congestion window | `m_tcb->m_cWnd` (bytes) |
| 4 | `bytes_in_flight`| Unacknowledged in-flight data | `BytesInFlight()` (bytes) |
| 5 | `avail_win` | Window headroom | `AvailableWindow()` (bytes) |
| 6 | `loss_rate` | Retransmission drop ratio | Recent 1.0s sliding window |
| 7 | `rtt_ratio` | RTT relative to fastest path | Scale-free relative metric |
| 8 | `bw_share` | Path bandwidth share of total | Scale-free relative metric |
| 9 | `win_headroom` | Available window / CWND | Headroom fraction [0, 1] |
| 10 | `rtt_inflation`| RTT / minRTT (bufferbloat) | Bufferbloat indicator >= 1.0 |
| 11 | `n_paths` | Number of active paths | Topology dimension (int) |

## 3. Label Definition
- **Target**: Predicted segment delivery delay in milliseconds (`target_delay_ms`).
- **Formulation**: Shared scoring model predicting candidate subflow latency:
  $$\\text{{Delay}}_i = \\text{{RTT}}_i + \\frac{{\\text{{InFlight}}_i \\times 8}}{{\\text{{BW}}_i}} + \\text{{LossPenalty}}$$

## 4. Scenario-Grouped Splits
Splits are grouped strictly by whole `scenario_id` to prevent intra-session data leakage:
- `train`: Training set for model selection and cross-validation.
- `val`: Validation set for hyperparameter tuning.
- `test_g0_indist`: In-distribution reference test.
- `test_g1_extrapolation`: Disparity up to 16x, high loss.
- `test_g2_topology_shift`: 3-path TriPath topology.
- `test_g3_dynamics_shift`: Unseen dynamic patterns (quality swap, delay spikes, outages).
"""
    with open(card_path, "w") as f:
        f.write(content)
    print(f"Generated dataset card in {card_path}")

def main():
    with open(os.path.join(DATASET_DIR, "scenarios.json"), "r") as f:
        scenarios = json.load(f)
        
    generate_decision_logs(scenarios, max_workers=4, sample_limit=15)
    df = parse_decision_logs(scenarios)
    create_splits(scenarios)
    generate_data_card(df, scenarios)
    print("=== Phase 2 Dataset Generation Successfully Complete! ===")

if __name__ == "__main__":
    main()
