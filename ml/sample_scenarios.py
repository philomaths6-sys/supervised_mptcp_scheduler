#!/usr/bin/env python3
"""
ml/sample_scenarios.py
Latin Hypercube Scenario Sampler for MPTCP Supervised Scheduler Dataset Generation.
Implements Task 2.2 of the MPTCP Implementation Roadmap.
"""

import os
import json
import csv
import numpy as np
from scipy.stats import qmc

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATASET_DIR = os.path.join(REPO_ROOT, "dataset")
EVENTS_DIR = os.path.join(DATASET_DIR, "events")

def ensure_dirs():
    os.makedirs(DATASET_DIR, exist_ok=True)
    os.makedirs(EVENTS_DIR, exist_ok=True)
    os.makedirs(os.path.join(DATASET_DIR, "raw"), exist_ok=True)
    os.makedirs(os.path.join(DATASET_DIR, "processed"), exist_ok=True)

def generate_dynamic_events_file(event_path, pattern, paths):
    """
    Generate an events.csv file for time-varying dynamics (Task 1.2 & 1.3).
    Columns: time_s,path,rate_mbps,delay_ms,loss
    """
    with open(event_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time_s", "path", "rate_mbps", "delay_ms", "loss"])
        
        if pattern == "step_down":
            # Primary path rate drops by 50% at t=3.0s, recovers at t=6.0s
            orig_rate = paths[0]["rate_mbps"]
            writer.writerow([3.0, 0, f"{orig_rate * 0.5:.2f}", "", ""])
            writer.writerow([6.0, 0, f"{orig_rate:.2f}", "", ""])
            
        elif pattern == "fading":
            # Periodic loss spikes (square wave) on path 0
            writer.writerow([2.0, 0, "", "", 0.05])
            writer.writerow([4.0, 0, "", "", 0.001])
            writer.writerow([6.0, 0, "", "", 0.05])
            writer.writerow([8.0, 0, "", "", 0.001])
            
        elif pattern == "delay_spike":
            # Bufferbloat delay spike on path 0
            orig_delay = paths[0]["delay_ms"]
            writer.writerow([3.5, 0, "", f"{orig_delay * 3.0:.1f}", ""])
            writer.writerow([6.5, 0, "", f"{orig_delay:.1f}", ""])
            
        elif pattern == "quality_swap":
            # Fast path becomes slow, slow path becomes fast at t=4.0s
            if len(paths) >= 2:
                r0, d0 = paths[0]["rate_mbps"], paths[0]["delay_ms"]
                r1, d1 = paths[1]["rate_mbps"], paths[1]["delay_ms"]
                writer.writerow([4.0, 0, f"{r1:.2f}", f"{d1:.1f}", ""])
                writer.writerow([4.0, 1, f"{r0:.2f}", f"{d0:.1f}", ""])
                writer.writerow([7.5, 0, f"{r0:.2f}", f"{d0:.1f}", ""])
                writer.writerow([7.5, 1, f"{r1:.2f}", f"{d1:.1f}", ""])
                
        elif pattern == "outage":
            # Total path outage on path 0 from t=3.0s to t=6.0s (100% loss)
            writer.writerow([3.0, 0, "", "", 1.0])
            writer.writerow([6.0, 0, "", "", 0.001])

def sample_scenarios(seed=42, n_train=30, n_val=10, n_test_in=10, n_test_extrap=10):
    """
    Draw Latin Hypercube Samples across:
    - rate_mbps: [2, 50]
    - delay_ms: [2, 100]
    - loss: [0.0, 0.03]
    - queue_bdp_mult: [0.5, 3.0]
    """
    np.random.seed(seed)
    ensure_dirs()
    
    # 4 dimensions per path: rate, delay, loss, queue_bdp_mult
    dim = 4 * 2 # 2 paths
    sampler = qmc.LatinHypercube(d=dim, seed=seed)
    raw_samples = sampler.random(n=n_train + n_val + n_test_in)
    
    scenarios = []
    idx = 0
    
    # Sample in-distribution 2-path scenarios
    families = (
        ["train_dual"] * n_train +
        ["val_dual"] * n_val +
        ["test_g0_indist"] * n_test_in
    )
    
    patterns = ["none", "none", "step_down", "fading", "none"]
    
    for i, s in enumerate(raw_samples):
        sc_id = f"s{idx:04d}"
        family = families[i]
        
        # Scale samples
        # Path 0: primary
        rate0 = 2.0 + s[0] * (50.0 - 2.0)
        delay0 = 2.0 + s[1] * (100.0 - 2.0)
        loss0 = 0.0 + s[2] * 0.03
        q_mult0 = 0.5 + s[3] * (3.0 - 0.5)
        
        # Path 1: secondary
        rate1 = 2.0 + s[4] * (50.0 - 2.0)
        delay1 = 2.0 + s[5] * (100.0 - 2.0)
        loss1 = 0.0 + s[6] * 0.03
        q_mult1 = 0.5 + s[7] * (3.0 - 0.5)
        
        paths = [
            {"path": 0, "rate_mbps": round(float(rate0), 2), "delay_ms": round(float(delay0), 1), "loss": round(float(loss0), 4), "q_mult": round(float(q_mult0), 2)},
            {"path": 1, "rate_mbps": round(float(rate1), 2), "delay_ms": round(float(delay1), 1), "loss": round(float(loss1), 4), "q_mult": round(float(q_mult1), 2)}
        ]
        
        # Assign dynamics
        pattern = patterns[i % len(patterns)]
        events_file = ""
        if pattern != "none":
            events_file = f"dataset/events/{sc_id}_{pattern}.csv"
            generate_dynamic_events_file(os.path.join(REPO_ROOT, events_file), pattern, paths)
            
        scenarios.append({
            "scenario_id": sc_id,
            "family": family,
            "n_paths": 2,
            "paths": paths,
            "dynamics": pattern,
            "events_file": events_file
        })
        idx += 1
        
    # G1: Parameter Extrapolation Held-Out Set (disparity > 4x to 16x, high loss > 3% to 6%)
    for k in range(n_test_extrap):
        sc_id = f"s{idx:04d}"
        disparity = 4.0 + (k / (n_test_extrap - 1)) * 12.0 # 4x to 16x
        d0 = 5.0
        d1 = d0 * disparity
        r0 = 25.0
        r1 = 10.0
        loss0 = 0.001
        loss1 = 0.01 + 0.005 * k # up to 5.5% loss
        
        paths = [
            {"path": 0, "rate_mbps": r0, "delay_ms": round(d0, 1), "loss": round(loss0, 4), "q_mult": 1.5},
            {"path": 1, "rate_mbps": r1, "delay_ms": round(d1, 1), "loss": round(loss1, 4), "q_mult": 1.5}
        ]
        scenarios.append({
            "scenario_id": sc_id,
            "family": "test_g1_extrapolation",
            "n_paths": 2,
            "paths": paths,
            "dynamics": "none",
            "events_file": ""
        })
        idx += 1
        
    # G2: Topology Shift Held-Out Set (3-Path Tri-Path Topology)
    for k in range(8):
        sc_id = f"s{idx:04d}"
        paths = [
            {"path": 0, "rate_mbps": 30.0, "delay_ms": 5.0, "loss": 0.0001, "q_mult": 1.5},
            {"path": 1, "rate_mbps": 15.0, "delay_ms": 20.0, "loss": 0.005, "q_mult": 1.5},
            {"path": 2, "rate_mbps": 10.0, "delay_ms": 50.0, "loss": 0.015, "q_mult": 1.5}
        ]
        pattern = "quality_swap" if k % 2 == 1 else "none"
        events_file = ""
        if pattern != "none":
            events_file = f"dataset/events/{sc_id}_{pattern}.csv"
            generate_dynamic_events_file(os.path.join(REPO_ROOT, events_file), pattern, paths)
            
        scenarios.append({
            "scenario_id": sc_id,
            "family": "test_g2_topology_shift",
            "n_paths": 3,
            "paths": paths,
            "dynamics": pattern,
            "events_file": events_file
        })
        idx += 1
        
    # G3: Dynamic Shift Held-Out Set (unseen dynamics: quality_swap, delay_spike, outage)
    unseen_patterns = ["quality_swap", "delay_spike", "outage"]
    for k, pat in enumerate(unseen_patterns * 3):
        sc_id = f"s{idx:04d}"
        paths = [
            {"path": 0, "rate_mbps": 20.0, "delay_ms": 10.0, "loss": 0.001, "q_mult": 1.5},
            {"path": 1, "rate_mbps": 15.0, "delay_ms": 30.0, "loss": 0.01, "q_mult": 1.5}
        ]
        events_file = f"dataset/events/{sc_id}_{pat}.csv"
        generate_dynamic_events_file(os.path.join(REPO_ROOT, events_file), pat, paths)
        
        scenarios.append({
            "scenario_id": sc_id,
            "family": "test_g3_dynamics_shift",
            "n_paths": 2,
            "paths": paths,
            "dynamics": pat,
            "events_file": events_file
        })
        idx += 1

    # Save scenarios manifest
    manifest_path = os.path.join(DATASET_DIR, "scenarios.json")
    with open(manifest_path, "w") as f:
        json.dump(scenarios, f, indent=2)
        
    print(f"Sampled {len(scenarios)} total scenarios across families:")
    family_counts = {}
    for sc in scenarios:
        family_counts[sc["family"]] = family_counts.get(sc["family"], 0) + 1
    for fam, cnt in family_counts.items():
        print(f"  - {fam}: {cnt} scenarios")
    print(f"Saved manifest to {manifest_path}")

if __name__ == "__main__":
    sample_scenarios()
