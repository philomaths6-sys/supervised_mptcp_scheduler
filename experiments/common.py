#!/usr/bin/env python3
"""
experiments/common.py
Shared utilities, path configurations, execution drivers, and statistical helpers
for MPTCP scheduler benchmark experiments (Roadmap Task 0.7).
"""

import os
import sys
import subprocess
import json
import csv
import numpy as np
from scipy import stats

# Paths
EXPERIMENTS_DIR = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT = os.path.abspath(os.path.join(EXPERIMENTS_DIR, ".."))
NS3_DIR = os.path.join(REPO_ROOT, "ns-3-mptcp")
RESULTS_DIR = os.path.join(REPO_ROOT, "results")
DATA_DIR = os.path.join(RESULTS_DIR, "data")
PLOTS_DIR = os.path.join(RESULTS_DIR, "plots")
TIMESERIES_DIR = os.path.join(RESULTS_DIR, "timeseries")
MANIFESTS_DIR = os.path.join(RESULTS_DIR, "manifests")

# Protocols benchmarked
PROTOCOLS = [
    "Standard-TCP",
    "minRTT",
    "RoundRobin",
    "BLEST",
    "ECF",
    "Supervised"
]

TOPOLOGIES = [
    "WiFi_Cellular",
    "Ethernet_WiFi",
    "TriPath"
]

def ensure_dirs():
    for d in [RESULTS_DIR, DATA_DIR, PLOTS_DIR, TIMESERIES_DIR, MANIFESTS_DIR]:
        os.makedirs(d, exist_ok=True)

def run_experiment(protocol, topology, duration=10, seed=1, run=1,
                   path0Rate="", path0Delay="", path0Loss=-1.0,
                   path1Rate="", path1Delay="", path1Loss=-1.0,
                   path2Rate="", path2Delay="", path2Loss=-1.0,
                   disparity=-1.0, handover=False, events="",
                   logDecisions="", timeSeries="", maxBytes=0,
                   outputCsv="", epsilon=0.3):
    """
    Executes a single simulation instance via waf and returns parsed JSON results.
    """
    args = [
        f"--protocol={protocol}",
        f"--topology={topology}",
        f"--duration={duration}",
        f"--seed={seed}",
        f"--run={run}",
        "--json"
    ]
    if path0Rate: args.append(f"--path0Rate={path0Rate}")
    if path0Delay: args.append(f"--path0Delay={path0Delay}")
    if path0Loss >= 0: args.append(f"--path0Loss={path0Loss}")
    if path1Rate: args.append(f"--path1Rate={path1Rate}")
    if path1Delay: args.append(f"--path1Delay={path1Delay}")
    if path1Loss >= 0: args.append(f"--path1Loss={path1Loss}")
    if path2Rate: args.append(f"--path2Rate={path2Rate}")
    if path2Delay: args.append(f"--path2Delay={path2Delay}")
    if path2Loss >= 0: args.append(f"--path2Loss={path2Loss}")
    if disparity >= 0: args.append(f"--disparity={disparity}")
    if handover: args.append("--handover=true")
    if events: args.append(f"--events={events}")
    if logDecisions: args.append(f"--logDecisions={logDecisions}")
    if timeSeries: args.append(f"--timeSeries={timeSeries}")
    if maxBytes > 0: args.append(f"--maxBytes={maxBytes}")
    if outputCsv: args.append(f"--outputCsv={outputCsv}")
    if protocol == "EpsRandom": args.append(f"--epsilon={epsilon}")

    cmd_str = f"scratch/run_experiment {' '.join(args)}"
    full_cmd = ["./waf", "--run", cmd_str]
    res = subprocess.run(full_cmd, cwd=NS3_DIR, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if res.returncode != 0:
        print(f"Execution failed for {protocol} on {topology}: {res.stderr[:200]}", file=sys.stderr)
        return None

    out = res.stdout
    start_idx = out.find("{")
    end_idx = out.rfind("}")
    if start_idx != -1 and end_idx != -1:
        try:
            return json.loads(out[start_idx:end_idx+1])
        except json.JSONDecodeError:
            pass
    return None

def compute_ci(data, confidence=0.95):
    """
    Computes sample mean and 95% Student's t confidence interval.
    """
    arr = np.array(data, dtype=float)
    n = len(arr)
    if n < 2:
        return np.mean(arr) if n == 1 else 0.0, 0.0
    mean = np.mean(arr)
    se = stats.sem(arr)
    h = se * stats.t.ppf((1 + confidence) / 2.0, n - 1)
    return mean, h

def paired_wilcoxon(sample_a, sample_b):
    """
    Paired Wilcoxon signed-rank test.
    """
    diff = np.array(sample_a) - np.array(sample_b)
    if np.all(diff == 0):
        return 1.0, 0.0
    try:
        stat, pval = stats.wilcoxon(sample_a, sample_b)
        return stat, pval
    except Exception:
        return 1.0, 1.0
