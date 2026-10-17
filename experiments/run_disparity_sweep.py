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
CSV_PATH = os.path.join(RESULTS_DIR, "disparity_sweep_results.csv")

os.makedirs(PLOTS_DIR, exist_ok=True)

# Standard Research Delay Disparity Parameters
# Fixed Low-RTT Path: 20 Mbps, 10 ms (RTT ~20 ms)
# Varying High-RTT Path: 20 Mbps, Delay in {10ms, 20ms, 40ms, 80ms, 120ms, 160ms}
# Total Link Capacity: 40 Mbps
DELAY_POINTS = [
    {"p1_delay": "10ms", "ratio": 1.0,  "desc": "1x (Symmetric 10ms)"},
    {"p1_delay": "20ms", "ratio": 2.0,  "desc": "2x (20ms vs 10ms)"},
    {"p1_delay": "40ms", "ratio": 4.0,  "desc": "4x (40ms vs 10ms)"},
    {"p1_delay": "80ms", "ratio": 8.0,  "desc": "8x (80ms vs 10ms)"},
    {"p1_delay": "120ms", "ratio": 12.0, "desc": "12x (120ms vs 10ms)"},
    {"p1_delay": "160ms", "ratio": 16.0, "desc": "16x (160ms vs 10ms)"},
]

SCHEDULERS = [
    {"name": "Standard-TCP", "mptcp": False, "sched": "FastestRTT"},
    {"name": "RoundRobin",   "mptcp": True,  "sched": "RoundRobin"},
    {"name": "FastestRTT",   "mptcp": True,  "sched": "FastestRTT"},
    {"name": "BLEST",        "mptcp": True,  "sched": "BLEST"},
    {"name": "ECF",          "mptcp": True,  "sched": "ECF"},
]

def run_point(p1_delay, ratio, sched_cfg):
    mptcp_flag = "1" if sched_cfg["mptcp"] else "0"
    sched_name = sched_cfg["sched"]
    label = sched_cfg["name"]

    cmd = (
        f'./waf --run "scratch/run_modular_experiment '
        f'--profile=custom '
        f'--p0Bw=20Mbps --p0Delay=10ms --p0Loss=0 '
        f'--p1Bw=20Mbps --p1Delay={p1_delay} --p1Loss=0 '
        f'--scenario=none '
        f'--scheduler={sched_name} '
        f'--mptcp={mptcp_flag} '
        f'--duration=5.0 '
        f'--transferBytes=0 '
        f'--outputCsv={CSV_PATH}"'
    )
    print(f"[RUNNING SWEEP] Disparity: {ratio}x ({p1_delay}) | Protocol: {label}")
    try:
        proc = subprocess.run(cmd, cwd=NS3_DIR, shell=True, capture_output=True, text=True, timeout=60)
        if proc.returncode != 0:
            print(f"  [ERROR] Execution failed:\n{proc.stderr}")
        else:
            for line in proc.stdout.splitlines():
                if any(k in line for k in ["Active Throughput", "Session Goodput", "Measured Avg RTT"]):
                    print("  " + line.strip())
    except subprocess.TimeoutExpired:
        print("  [TIMEOUT]")

def run_all_sweep():
    # Remove old CSV if exists
    if os.path.exists(CSV_PATH):
        os.remove(CSV_PATH)

    for pt in DELAY_POINTS:
        for sc in SCHEDULERS:
            run_point(pt["p1_delay"], pt["ratio"], sc)

def plot_disparity_results():
    if not os.path.exists(CSV_PATH):
        print(f"[ERROR] CSV {CSV_PATH} not found!")
        return

    df = pd.read_csv(CSV_PATH)
    print(f"\nLoaded {len(df)} sweep records.")

    # Match each run to ratio based on order
    total_points = len(DELAY_POINTS)
    num_sched = len(SCHEDULERS)
    
    ratios = []
    for pt in DELAY_POINTS:
        for _ in range(num_sched):
            ratios.append(pt["ratio"])

    if len(df) == len(ratios):
        df["disparity_ratio"] = ratios
    else:
        # Fallback if partial
        df["disparity_ratio"] = [DELAY_POINTS[i // num_sched]["ratio"] for i in range(len(df))]

    # Calculate Aggregation Efficiency (Capacity = 40 Mbps for MPTCP, 20 Mbps for Single-Path)
    # Relative to total multi-link capacity (40 Mbps)
    df["aggregation_efficiency"] = (df["goodputMbps"] / 40.0) * 100.0
    # Relative speedup over Standard-TCP baseline
    tcp_baseline = df[df["scheduler"] == "Standard-TCP"].set_index("disparity_ratio")["goodputMbps"]

    sns.set_theme(style="whitegrid", font="sans-serif")
    palette = {
        "Standard-TCP": "#7f8c8d",
        "FastestRTT": "#2980b9",
        "RoundRobin": "#e67e22",
        "BLEST": "#27ae60",
        "ECF": "#8e44ad"
    }
    markers = {
        "Standard-TCP": "o",
        "FastestRTT": "s",
        "RoundRobin": "^",
        "BLEST": "D",
        "ECF": "P"
    }

    # =========================================================================
    # Chart 1: Goodput vs Delay Disparity (ACM CoNEXT / IEEE Benchmark Line Chart)
    # =========================================================================
    plt.figure(figsize=(10, 6), dpi=300)
    for sched_name in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        subset = df[df["scheduler"] == sched_name]
        if not subset.empty:
            plt.plot(subset["disparity_ratio"], subset["goodputMbps"], 
                     marker=markers.get(sched_name, 'o'), markersize=8, linewidth=2.5,
                     label=sched_name, color=palette.get(sched_name, "#333"))

    # Reference theoretical lines
    plt.axhline(y=40.0, color="#c0392b", linestyle="--", alpha=0.8, label="Ideal Aggregation (C1 + C2 = 40 Mbps)")
    plt.axhline(y=20.0, color="#7f8c8d", linestyle=":", alpha=0.8, label="Single-Path Cap (C1 = 20 Mbps)")

    plt.title("MPTCP Schedulers: Goodput vs. Delay Disparity ($RTT_{slow} / RTT_{fast}$)\n"
              "Evaluation over Heterogeneous Paths [Path 1: 20Mbps/10ms, Path 2: 20Mbps/varies]",
              fontsize=13, fontweight='bold', pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight='bold')
    plt.ylabel("Session Goodput (Mbps)", fontsize=11, fontweight='bold')
    plt.xticks([1, 2, 4, 8, 12, 16], ["1x\n(10ms)", "2x\n(20ms)", "4x\n(40ms)", "8x\n(80ms)", "12x\n(120ms)", "16x\n(160ms)"])
    plt.ylim(0, 44)
    plt.legend(frameon=True, facecolor="white", edgecolor="#ccc", loc="lower left", fontsize=10)
    plt.tight_layout()
    chart1_path = os.path.join(PLOTS_DIR, "mptcp_delay_disparity_goodput_curve.png")
    plt.savefig(chart1_path)
    plt.close()
    print(f"[SAVED] {chart1_path}")

    # =========================================================================
    # Chart 2: Aggregation Efficiency (%) vs Delay Disparity
    # =========================================================================
    plt.figure(figsize=(10, 6), dpi=300)
    for sched_name in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        subset = df[df["scheduler"] == sched_name]
        if not subset.empty:
            plt.plot(subset["disparity_ratio"], subset["aggregation_efficiency"], 
                     marker=markers.get(sched_name, 'o'), markersize=8, linewidth=2.5,
                     label=sched_name, color=palette.get(sched_name, "#333"))

    plt.axhline(y=100.0, color="#c0392b", linestyle="--", alpha=0.8, label="100% Full Capacity Aggregation")
    plt.axhline(y=50.0, color="#7f8c8d", linestyle=":", alpha=0.8, label="50% Single-Path Equivalent")

    plt.title("Throughput Aggregation Efficiency ($\eta = \\frac{\\text{Goodput}}{C_1 + C_2} \\times 100\\%$)\n"
              "How Schedulers Preserve Bandwidth under Increasing Path Latency Asymmetry",
              fontsize=13, fontweight='bold', pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight='bold')
    plt.ylabel("Aggregation Efficiency (%)", fontsize=11, fontweight='bold')
    plt.xticks([1, 2, 4, 8, 12, 16], ["1x", "2x", "4x", "8x", "12x", "16x"])
    plt.ylim(0, 110)
    plt.legend(frameon=True, facecolor="white", edgecolor="#ccc", loc="lower left", fontsize=10)
    plt.tight_layout()
    chart2_path = os.path.join(PLOTS_DIR, "mptcp_aggregation_efficiency_curve.png")
    plt.savefig(chart2_path)
    plt.close()
    print(f"[SAVED] {chart2_path}")

    # =========================================================================
    # Chart 3: Measured Packet Latency (RTT) Inflation vs Delay Disparity
    # =========================================================================
    plt.figure(figsize=(10, 6), dpi=300)
    for sched_name in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        subset = df[df["scheduler"] == sched_name]
        if not subset.empty:
            plt.plot(subset["disparity_ratio"], subset["latencyMs"], 
                     marker=markers.get(sched_name, 'o'), markersize=8, linewidth=2.5,
                     label=sched_name, color=palette.get(sched_name, "#333"))

    plt.title("Measured Packet Latency & HoL Reordering Penalty vs. Delay Disparity\n"
              "Demonstrating Bufferbloat & Head-of-Line Queue Delay Across Schedulers",
              fontsize=13, fontweight='bold', pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight='bold')
    plt.ylabel("Measured Average Packet RTT (ms)", fontsize=11, fontweight='bold')
    plt.xticks([1, 2, 4, 8, 12, 16], ["1x", "2x", "4x", "8x", "12x", "16x"])
    plt.legend(frameon=True, facecolor="white", edgecolor="#ccc", loc="upper left", fontsize=10)
    plt.tight_layout()
    chart3_path = os.path.join(PLOTS_DIR, "mptcp_latency_inflation_curve.png")
    plt.savefig(chart3_path)
    plt.close()
    print(f"[SAVED] {chart3_path}")

    # =========================================================================
    # Chart 4: Multi-Metric Research Dashboard (4-Panel Grid)
    # =========================================================================
    fig, axes = plt.subplots(2, 2, figsize=(16, 12), dpi=300)

    # Subplot 1: Goodput
    ax = axes[0, 0]
    for s in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        sub = df[df["scheduler"] == s]
        if not sub.empty:
            ax.plot(sub["disparity_ratio"], sub["goodputMbps"], marker=markers.get(s, 'o'),
                    label=s, color=palette.get(s, "#333"), linewidth=2)
    ax.axhline(y=40.0, color="#c0392b", linestyle="--", alpha=0.6, label="Ideal (40 Mbps)")
    ax.set_title("A. Session Goodput (Mbps)", fontsize=12, fontweight='bold')
    ax.set_xlabel("Delay Disparity Ratio", fontweight='bold')
    ax.set_ylabel("Goodput (Mbps)", fontweight='bold')
    ax.legend(fontsize=8)

    # Subplot 2: Aggregation Efficiency
    ax = axes[0, 1]
    for s in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        sub = df[df["scheduler"] == s]
        if not sub.empty:
            ax.plot(sub["disparity_ratio"], sub["aggregation_efficiency"], marker=markers.get(s, 'o'),
                    label=s, color=palette.get(s, "#333"), linewidth=2)
    ax.axhline(y=100.0, color="#c0392b", linestyle="--", alpha=0.6, label="100% Aggregation")
    ax.set_title("B. Throughput Aggregation Efficiency (%)", fontsize=12, fontweight='bold')
    ax.set_xlabel("Delay Disparity Ratio", fontweight='bold')
    ax.set_ylabel("Efficiency (%)", fontweight='bold')
    ax.legend(fontsize=8)

    # Subplot 3: Measured Latency
    ax = axes[1, 0]
    for s in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        sub = df[df["scheduler"] == s]
        if not sub.empty:
            ax.plot(sub["disparity_ratio"], sub["latencyMs"], marker=markers.get(s, 'o'),
                    label=s, color=palette.get(s, "#333"), linewidth=2)
    ax.set_title("C. End-to-End Latency / RTT (ms)", fontsize=12, fontweight='bold')
    ax.set_xlabel("Delay Disparity Ratio", fontweight='bold')
    ax.set_ylabel("RTT (ms)", fontweight='bold')
    ax.legend(fontsize=8)

    # Subplot 4: Packets Delivered vs Loss / Reordering
    ax = axes[1, 1]
    for s in ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]:
        sub = df[df["scheduler"] == s]
        if not sub.empty:
            ax.plot(sub["disparity_ratio"], sub["rxPackets"], marker=markers.get(s, 'o'),
                    label=s, color=palette.get(s, "#333"), linewidth=2)
    ax.set_title("D. Total Delivered Packets", fontsize=12, fontweight='bold')
    ax.set_xlabel("Delay Disparity Ratio", fontweight='bold')
    ax.set_ylabel("Packets Received", fontweight='bold')
    ax.legend(fontsize=8)

    plt.suptitle("Multipath TCP Scheduler Comparative Performance Matrix\n"
                 "Sensitivity Analysis Across Increasing Network Latency Asymmetry ($RTT_2 / RTT_1$)",
                 fontsize=15, fontweight='bold', y=0.99)
    plt.tight_layout()
    chart4_path = os.path.join(PLOTS_DIR, "mptcp_research_disparity_dashboard.png")
    plt.savefig(chart4_path)
    plt.close()
    print(f"[SAVED] {chart4_path}")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--plot-only":
        plot_disparity_results()
    else:
        run_all_sweep()
        plot_disparity_results()
