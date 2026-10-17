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
    # 1. Static Profile: Wi-Fi + Cellular (Smartphone)
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "wifi_cellular", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 2. Static Profile: Ethernet + Wi-Fi (Workstation)
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "ethernet_wifi", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 3. Static Profile: Triple-Path (Ethernet + Wi-Fi + Cellular)
    {"profile": "triple_path", "scenario": "none", "scheduler": "Standard-TCP", "mptcp": False},
    {"profile": "triple_path", "scenario": "none", "scheduler": "FastestRTT", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "RoundRobin", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "BLEST", "mptcp": True},
    {"profile": "triple_path", "scenario": "none", "scheduler": "ECF", "mptcp": True},

    # 4. Dynamic Generalization: Fading & Mobile Handover (Objective iii)
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
        f'--outputCsv={CSV_PATH}"'
    )
    print(f"\n[RUNNING] Profile: {cfg['profile']} | Scenario: {cfg['scenario']} | Protocol: {cfg['scheduler']}")
    try:
        proc = subprocess.run(cmd, cwd=NS3_DIR, shell=True, capture_output=True, text=True, timeout=60)
        if proc.returncode != 0:
            print(f"[ERROR] Simulation failed with code {proc.returncode}:\n{proc.stderr}")
        else:
            for line in proc.stdout.splitlines():
                if "Effective Goodput" in line or "Total Data Transferred" in line:
                    print("  " + line.strip())
    except subprocess.TimeoutExpired:
        print("[TIMEOUT] Simulation timed out.")

def generate_plots():
    if not os.path.exists(CSV_PATH):
        print(f"[ERROR] CSV {CSV_PATH} not found!")
        return

    df = pd.read_csv(CSV_PATH)
    print(f"\nLoaded {len(df)} experiment rows from {CSV_PATH}")

    # Set seaborn styling
    sns.set_theme(style="whitegrid", font="sans-serif")
    palette = {
        "Standard-TCP": "#7f8c8d",
        "FastestRTT": "#3498db",
        "RoundRobin": "#e67e22",
        "BLEST": "#2ecc71",
        "ECF": "#9b59b6"
    }

    # Plot 1: Goodput Comparison across Device Profiles (Static)
    plt.figure(figsize=(12, 6))
    static_df = df[df["scenario"] == "none"]
    ax1 = sns.barplot(
        data=static_df,
        x="profile",
        y="goodputMbps",
        hue="scheduler",
        palette=palette
    )
    plt.title("Effective Goodput Comparison: Standard-TCP vs Classical Schedulers", fontsize=14, fontweight="bold", pad=15)
    plt.xlabel("Device Network Profile", fontsize=12)
    plt.ylabel("Goodput (Mbps)", fontsize=12)
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10)
    for p in ax1.patches:
        height = p.get_height()
        if height > 0:
            ax1.annotate(f"{height:.2f}",
                         (p.get_x() + p.get_width() / 2., height),
                         ha='center', va='bottom', fontsize=8, xytext=(0, 3),
                         textcoords='offset points')
    plt.tight_layout()
    p1_path = os.path.join(PLOTS_DIR, "goodput_profiles_comparison.png")
    plt.savefig(p1_path, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {p1_path}")

    # Plot 2: Dynamic Generalization Comparison (Fading/Handover & Bufferbloat)
    plt.figure(figsize=(12, 6))
    dyn_df = df[df["profile"] == "wifi_cellular"]
    ax2 = sns.barplot(
        data=dyn_df,
        x="scenario",
        y="goodputMbps",
        hue="scheduler",
        palette=palette
    )
    plt.title("Dynamic Generalization Under Fading, Handover, and Bufferbloat", fontsize=14, fontweight="bold", pad=15)
    plt.xlabel("Dynamic Network Scenario", fontsize=12)
    plt.ylabel("Goodput (Mbps)", fontsize=12)
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10)
    for p in ax2.patches:
        height = p.get_height()
        if height > 0:
            ax2.annotate(f"{height:.2f}",
                         (p.get_x() + p.get_width() / 2., height),
                         ha='center', va='bottom', fontsize=8, xytext=(0, 3),
                         textcoords='offset points')
    plt.tight_layout()
    p2_path = os.path.join(PLOTS_DIR, "dynamic_generalization_comparison.png")
    plt.savefig(p2_path, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {p2_path}")

    # Plot 3: Total Transferred Volume Comparison
    plt.figure(figsize=(12, 6))
    ax3 = sns.barplot(
        data=df,
        x="profile",
        y="totalRxBytes",
        hue="scheduler",
        palette=palette
    )
    plt.title("Total Data Transferred Volume across Profiles and Scenarios", fontsize=14, fontweight="bold", pad=15)
    plt.xlabel("Device Network Profile", fontsize=12)
    plt.ylabel("Total Transferred Bytes", fontsize=12)
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10)
    plt.tight_layout()
    p3_path = os.path.join(PLOTS_DIR, "transferred_volume_comparison.png")
    plt.savefig(p3_path, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {p3_path}")

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
