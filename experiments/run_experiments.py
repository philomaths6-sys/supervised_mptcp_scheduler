import subprocess
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

PROJECT_ROOT = "/home/sk/Desktop/sk_Desktop/mptcp_scheduler"
NS3_DIR = os.path.join(PROJECT_ROOT, "ns-3-mptcp")
RESULTS_DIR = os.path.join(PROJECT_ROOT, "results")
PLOTS_DIR = os.path.join(RESULTS_DIR, "plots")
CSV_PATH = os.path.join(RESULTS_DIR, "benchmark_results.csv")

os.makedirs(PLOTS_DIR, exist_ok=True)

# Full matrix of schedulers comparing Standard-TCP, minRTT, RoundRobin, BLEST, and ECF
TEST_MATRIX = [
    # 0. Canonical Asymmetric Benchmark: Ethernet 5 Mbps + Wi-Fi 1 Mbps (Research Baseline)
    {"profile": "custom", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "custom", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "custom", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "custom", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "custom", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 1. Static Profile: Wi-Fi (15 Mbps) + Cellular (5 Mbps)
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 2. Static Profile: Ethernet (25 Mbps) + Wi-Fi (10 Mbps)
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 3. Static Profile: Triple-Path (Ethernet 20 Mbps + Wi-Fi 10 Mbps + Cellular 5 Mbps)
    {"profile": "triple_path", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "triple_path", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 4. Dynamic Generalization: Fading & Mobile Handover
    {"profile": "wifi_cellular", "scenario": "fading_handover", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "wifi_cellular", "scenario": "fading_handover", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "fading_handover", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "fading_handover", "scheduler": "BLEST", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "fading_handover", "scheduler": "ECF", "mptcp": True},

    # 5. Dynamic Congestion: Bufferbloat & Queue Delay Spikes
    {"profile": "wifi_cellular", "scenario": "bufferbloat", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "wifi_cellular", "scenario": "bufferbloat", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "bufferbloat", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "bufferbloat", "scheduler": "BLEST", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "bufferbloat", "scheduler": "ECF", "mptcp": True},
]

def run_simulation(cfg):
    mptcp_flag = "1" if cfg["mptcp"] else "0"
    sched = cfg["scheduler"] if cfg["mptcp"] else "FastestRTT"
    
    cmd = (
        f'./waf --run "scratch/run_modular_experiment '
        f'--profile={cfg["profile"]} '
        f'--scenario={cfg["scenario"]} '
        f'--scheduler={sched} '
        f'--mptcp={mptcp_flag} '
        f'--duration=5.0 '
        f'--transferBytes=0 '
        f'--outputCsv={CSV_PATH}"'
    )
    print(f"\n[RUNNING] Profile: {cfg['profile']} | Scenario: {cfg['scenario']} | Protocol: {cfg['scheduler']}")
    try:
        proc = subprocess.run(cmd, cwd=NS3_DIR, shell=True, capture_output=True, text=True, timeout=120)
        if proc.returncode != 0:
            print(f"[ERROR] Simulation failed with code {proc.returncode}:\n{proc.stderr}")
        else:
            for line in proc.stdout.splitlines():
                if any(k in line for k in ["Active Throughput", "Session Goodput", "Measured Avg RTT", "Retransmissions / Lost"]):
                    print("  " + line.strip())
    except subprocess.TimeoutExpired:
        print("[TIMEOUT] Simulation timed out.")

def generate_plots():
    import plot_benchmark_results
    plot_benchmark_results.main()

def main():
    if os.path.exists(CSV_PATH):
        os.remove(CSV_PATH)

    print("==========================================================================")
    print(" STARTING BENCHMARK: Standard-TCP, minRTT, RoundRobin, BLEST, and ECF")
    print("==========================================================================")
    for cfg in TEST_MATRIX:
        run_simulation(cfg)

    print("\nGenerating comprehensive comparative plots...")
    generate_plots()
    print("\nBenchmark completed!")

if __name__ == "__main__":
    main()
