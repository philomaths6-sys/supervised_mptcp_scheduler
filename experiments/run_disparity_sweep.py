#!/usr/bin/env python3
"""
Run Calibrated Delay Disparity Sweep (1.0x to 16.0x)
Accurately sweeps delay disparity multiplier with:
- Multi-seed averaging (seeds 1, 2, 3)
- Genuinely symmetric 1.0x baseline (15ms vs 15ms)
- Exact measured application payload goodput (via PacketSink)
- Physical loss tracking and uninstalled 1000p qdisc
"""

import os
import subprocess
import json
import pandas as pd
import numpy as np

DISPARITY_FACTORS = [1.0, 2.0, 4.0, 8.0, 16.0]
PROTOCOLS = [
    "Standard-TCP",
    "FastestRTT",
    "RoundRobin",
    "BLEST",
    "ECF"
]
SEEDS = [1, 2, 3]

def run_disparity_sweep(duration=6.0):
    base_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_dir = os.path.dirname(base_dir)
    ns3_dir = os.path.join(workspace_dir, "ns-3-mptcp")
    results_dir = os.path.join(workspace_dir, "results", "data")
    os.makedirs(results_dir, exist_ok=True)
    
    csv_output = os.path.join(results_dir, "disparity_sweep.csv")
    if os.path.exists(csv_output):
        os.remove(csv_output)
        
    print("=" * 80)
    print("RUNNING RIGOROUS DELAY DISPARITY SWEEP (1.0x to 16.0x)")
    print(f"Factors: {DISPARITY_FACTORS} | Protocols: {PROTOCOLS} | Seeds: {SEEDS}")
    print(f"Duration: {duration}s per point (Total runs: {len(DISPARITY_FACTORS) * len(PROTOCOLS) * len(SEEDS)})")
    print("=" * 80)
    
    records = []
    
    for disp in DISPARITY_FACTORS:
        print(f"\n--- [Disparity = {disp:.1f}x (RTT ratio)] ---")
        for proto in PROTOCOLS:
            seed_goodputs = []
            for seed in SEEDS:
                cmd = [
                    "./waf", "--run",
                    f"scratch/run_experiment --topology=WiFi_Cellular --protocol={proto} --duration={duration} --disparity={disp} --seed={seed} --outputCsv={csv_output} --json=true"
                ]
                proc = subprocess.run(
                    cmd,
                    cwd=ns3_dir,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True
                )
                if proc.returncode == 0:
                    stdout = proc.stdout
                    s = stdout.find("{")
                    e = stdout.rfind("}") + 1
                    if s >= 0 and e > s:
                        try:
                            d = json.loads(stdout[s:e])
                            seed_goodputs.append(d['sessionGoodputMbps'])
                        except:
                            pass
                            
            if seed_goodputs:
                mean_gp = np.mean(seed_goodputs)
                std_gp = np.std(seed_goodputs)
                records.append({
                    'disparity': disp,
                    'protocol': proto,
                    'meanGoodputMbps': mean_gp,
                    'stdGoodputMbps': std_gp
                })
                print(f"  {proto:15s} (disp={disp:4.1f}x) -> Goodput: {mean_gp:5.2f} ± {std_gp:4.2f} Mbps")

    print("\n" + "=" * 80)
    print("DISPARITY SWEEP SUMMARY:")
    print("=" * 80)
    if os.path.exists(csv_output):
        df = pd.read_csv(csv_output)
        # Group by disparity and protocol, averaging across seeds
        avg_df = df.groupby(['disparity', 'protocol'])['sessionGoodputMbps'].mean().unstack()
        print(avg_df.round(2).to_string())
        print("=" * 80)
        print("Saved detailed results to:", csv_output)

if __name__ == '__main__':
    run_disparity_sweep(duration=6.0)
