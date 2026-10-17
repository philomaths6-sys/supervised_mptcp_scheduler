import os
import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

PROJECT_ROOT = "/home/sk/Desktop/sk_Desktop/mptcp_scheduler"
RESULTS_DIR = os.path.join(PROJECT_ROOT, "results")
PLOTS_DIR = os.path.join(RESULTS_DIR, "plots")
CSV_PATH = os.path.join(RESULTS_DIR, "benchmark_results.csv")
DISPARITY_CSV = os.path.join(RESULTS_DIR, "disparity_sweep_results.csv")
ARTIFACT_DIR = "/home/sk/.gemini/antigravity-ide/brain/252413c5-c7a6-4f8f-9e08-3af330efb55d"

os.makedirs(PLOTS_DIR, exist_ok=True)

# Cohesive modern styling
sns.set_theme(style="whitegrid", font="sans-serif")
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'figure.titlesize': 15,
})

PALETTE = {
    "Standard-TCP": "#7f8c8d",      # Gray (Baseline)
    "FastestRTT": "#2980b9",        # Deep Blue
    "RoundRobin": "#d35400",        # Deep Orange
    "BLEST": "#27ae60",             # Emerald Green
    "ECF": "#8e44ad"                # Royal Purple
}

PROFILE_NAME_MAP = {
    "custom": "WiFi 1M + Eth 5M\n(Canonical)",
    "wifi_cellular": "WiFi 15M +\nCellular 5M",
    "ethernet_wifi": "Ethernet 25M +\nWiFi 10M",
    "triple_path": "Triple-Path\n(20M + 10M + 5M)"
}

SCENARIO_NAME_MAP = {
    "none": "Static Baseline",
    "fading_handover": "Fading / Handover",
    "bufferbloat": "Bufferbloat Queue"
}

def generate_canonical_plots(df):
    canon_df = df[(df["profile"] == "custom") & (df["scenario"] == "none")].copy()
    if canon_df.empty:
        print("[WARN] No canonical data found!")
        return

    # Order schedulers
    sched_order = ["Standard-TCP", "FastestRTT", "RoundRobin", "BLEST", "ECF"]
    canon_df["scheduler"] = pd.Categorical(canon_df["scheduler"], categories=sched_order, ordered=True)
    canon_df = canon_df.sort_values("scheduler")

    # -------------------------------------------------------------------------
    # Plot 1: Standalone Canonical Aggregation Throughput Chart
    # -------------------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(10, 6.5))
    bars = ax.bar(
        canon_df["scheduler"],
        canon_df["throughputMbps"],
        color=[PALETTE[s] for s in canon_df["scheduler"]],
        edgecolor="black",
        linewidth=1.2,
        width=0.55
    )

    # Reference lines
    ax.axhline(5.0, color="#c0392b", linestyle="--", linewidth=2.0, label="Single-Path TCP Ceiling (5.0 Mbps)")
    ax.axhline(6.0, color="#27ae60", linestyle=":", linewidth=2.0, label="Theoretical Capacity Sum $C_1 + C_2$ (6.0 Mbps)")

    ax.set_title("Canonical MPTCP Throughput Aggregation: WiFi 1 Mbps + Ethernet 5 Mbps",
                 fontsize=14, fontweight="bold", pad=15)
    ax.set_ylabel("Active Sustained Throughput (Mbps)", fontsize=12, fontweight="bold")
    ax.set_xlabel("Protocol / Scheduler Algorithm", fontsize=12, fontweight="bold")
    ax.set_ylim(0, 7.0)

    # Annotate bars
    for bar, sched in zip(bars, canon_df["scheduler"]):
        height = bar.get_height()
        status_text = "AGGREGATED (>5M)" if height > 5.0 else "CAPPED (<5M)"
        status_color = "#1b5e20" if height > 5.0 else "#b71c1c"
        
        ax.annotate(f"{height:.2f} Mbps\n[{status_text}]",
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 6),
                    textcoords="offset points",
                    ha='center', va='bottom',
                    fontsize=10, fontweight="bold",
                    color=status_color)

    ax.legend(loc="upper left", frameon=True, facecolor="#ffffff", framealpha=0.9)
    plt.tight_layout()

    out_p1 = os.path.join(PLOTS_DIR, "canonical_wifi_ethernet_aggregation.png")
    plt.savefig(out_p1, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p1}")

    # -------------------------------------------------------------------------
    # Plot 2: 4-Panel Canonical Metrics Dashboard (Throughput, Goodput, Loss, RTT)
    # -------------------------------------------------------------------------
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Panel A: Active Throughput
    ax_a = axes[0, 0]
    bars_a = ax_a.bar(canon_df["scheduler"], canon_df["throughputMbps"],
                      color=[PALETTE[s] for s in canon_df["scheduler"]], edgecolor="black", width=0.55)
    ax_a.axhline(5.0, color="#c0392b", linestyle="--", linewidth=1.8, label="5.0 Mbps Ceiling")
    ax_a.axhline(6.0, color="#27ae60", linestyle=":", linewidth=1.8, label="6.0 Mbps Capacity")
    ax_a.set_title("A. Active Sustained Throughput (Mbps)", fontweight="bold")
    ax_a.set_ylabel("Throughput (Mbps)")
    ax_a.set_ylim(0, 7.0)
    ax_a.legend(loc="upper left", fontsize=9)
    for b in bars_a:
        h = b.get_height()
        ax_a.annotate(f"{h:.2f}", (b.get_x() + b.get_width() / 2, h),
                      xytext=(0, 4), textcoords="offset points", ha='center', va='bottom', fontweight="bold")

    # Panel B: Session Goodput
    ax_b = axes[0, 1]
    bars_b = ax_b.bar(canon_df["scheduler"], canon_df["goodputMbps"],
                      color=[PALETTE[s] for s in canon_df["scheduler"]], edgecolor="black", width=0.55)
    ax_b.set_title("B. Session Goodput (Mbps)", fontweight="bold")
    ax_b.set_ylabel("Goodput (Mbps)")
    ax_b.set_ylim(0, 5.5)
    for b in bars_b:
        h = b.get_height()
        ax_b.annotate(f"{h:.2f}", (b.get_x() + b.get_width() / 2, h),
                      xytext=(0, 4), textcoords="offset points", ha='center', va='bottom', fontweight="bold")

    # Panel C: Packet Loss Rate (%)
    ax_c = axes[1, 0]
    bars_c = ax_c.bar(canon_df["scheduler"], canon_df["lossRatePercent"],
                      color=[PALETTE[s] for s in canon_df["scheduler"]], edgecolor="black", width=0.55)
    ax_c.set_title("C. Packet Drop / Retransmission Rate (%)", fontweight="bold")
    ax_c.set_ylabel("Loss Rate (%)")
    ax_c.set_ylim(0, 13.0)
    for b in bars_c:
        h = b.get_height()
        ax_c.annotate(f"{h:.2f}%", (b.get_x() + b.get_width() / 2, h),
                      xytext=(0, 4), textcoords="offset points", ha='center', va='bottom', fontweight="bold")

    # Panel D: Measured Avg RTT (ms)
    ax_d = axes[1, 1]
    bars_d = ax_d.bar(canon_df["scheduler"], canon_df["latencyMs"],
                      color=[PALETTE[s] for s in canon_df["scheduler"]], edgecolor="black", width=0.55)
    ax_d.set_title("D. Measured Average Latency (RTT ms)", fontweight="bold")
    ax_d.set_ylabel("Avg RTT (ms)")
    ax_d.set_ylim(0, 1750)
    for b in bars_d:
        h = b.get_height()
        ax_d.annotate(f"{h:.1f}", (b.get_x() + b.get_width() / 2, h),
                      xytext=(0, 4), textcoords="offset points", ha='center', va='bottom', fontweight="bold")

    plt.suptitle("Canonical WiFi 1 Mbps + Ethernet 5 Mbps: Classical MPTCP vs Standard-TCP Benchmark",
                 fontsize=15, fontweight="bold", y=0.99)
    plt.tight_layout()

    out_p2 = os.path.join(PLOTS_DIR, "canonical_metrics_dashboard.png")
    plt.savefig(out_p2, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p2}")

def generate_multi_profile_plots(df):
    static_df = df[df["scenario"] == "none"].copy()
    static_df["profile_label"] = static_df["profile"].map(PROFILE_NAME_MAP)
    order_labels = [
        "WiFi 1M + Eth 5M\n(Canonical)",
        "WiFi 15M +\nCellular 5M",
        "Ethernet 25M +\nWiFi 10M",
        "Triple-Path\n(20M + 10M + 5M)"
    ]

    # -------------------------------------------------------------------------
    # Plot 3: Throughput across all device profiles
    # -------------------------------------------------------------------------
    plt.figure(figsize=(13, 6.5))
    ax = sns.barplot(
        data=static_df,
        x="profile_label",
        order=order_labels,
        y="throughputMbps",
        hue="scheduler",
        palette=PALETTE
    )
    plt.title("Active Sustained Throughput (Mbps) Across Device Profiles", fontsize=14, fontweight="bold", pad=15)
    plt.xlabel("Device Network Profile", fontsize=12, fontweight="bold")
    plt.ylabel("Measured Active Throughput (Mbps)", fontsize=12, fontweight="bold")
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10, loc="upper left")

    for p in ax.patches:
        h = p.get_height()
        if h > 0:
            ax.annotate(f"{h:.1f}", (p.get_x() + p.get_width() / 2., h),
                        ha='center', va='bottom', fontsize=8.5, xytext=(0, 3),
                        textcoords='offset points', fontweight="bold")

    plt.tight_layout()
    out_p3 = os.path.join(PLOTS_DIR, "throughput_profiles_comparison.png")
    plt.savefig(out_p3, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p3}")

    # -------------------------------------------------------------------------
    # Plot 4: Dynamic Generalization (WiFi + Cellular)
    # -------------------------------------------------------------------------
    dyn_df = df[df["profile"] == "wifi_cellular"].copy()
    dyn_df["scenario_label"] = dyn_df["scenario"].map(SCENARIO_NAME_MAP)
    scenario_order = ["Static Baseline", "Fading / Handover", "Bufferbloat Queue"]

    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=dyn_df,
        x="scenario_label",
        order=scenario_order,
        y="goodputMbps",
        hue="scheduler",
        palette=PALETTE
    )
    plt.title("Goodput Resilience Under Mobile Fading, Handover, and Bufferbloat (WiFi + Cellular)",
              fontsize=13, fontweight="bold", pad=15)
    plt.xlabel("Dynamic Network Condition", fontsize=12, fontweight="bold")
    plt.ylabel("Session Goodput (Mbps)", fontsize=12, fontweight="bold")
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10, loc="upper right")

    for p in ax.patches:
        h = p.get_height()
        if h > 0:
            ax.annotate(f"{h:.2f}", (p.get_x() + p.get_width() / 2., h),
                        ha='center', va='bottom', fontsize=9, xytext=(0, 3),
                        textcoords='offset points', fontweight="bold")

    plt.tight_layout()
    out_p4 = os.path.join(PLOTS_DIR, "dynamic_generalization_comparison.png")
    plt.savefig(out_p4, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p4}")

    # -------------------------------------------------------------------------
    # Plot 5: Latency Comparison Under Dynamic Conditions
    # -------------------------------------------------------------------------
    plt.figure(figsize=(12, 6))
    ax = sns.barplot(
        data=dyn_df,
        x="scenario_label",
        order=scenario_order,
        y="latencyMs",
        hue="scheduler",
        palette=PALETTE
    )
    plt.title("Measured End-to-End Latency (Avg RTT ms) Under Dynamic Conditions",
              fontsize=13, fontweight="bold", pad=15)
    plt.xlabel("Dynamic Network Condition", fontsize=12, fontweight="bold")
    plt.ylabel("Measured Avg RTT (ms)", fontsize=12, fontweight="bold")
    plt.legend(title="Protocol / Scheduler", title_fontsize="11", fontsize=10, loc="upper left")

    for p in ax.patches:
        h = p.get_height()
        if h > 0:
            ax.annotate(f"{h:.1f}", (p.get_x() + p.get_width() / 2., h),
                        ha='center', va='bottom', fontsize=9, xytext=(0, 3),
                        textcoords='offset points', fontweight="bold")

    plt.tight_layout()
    out_p5 = os.path.join(PLOTS_DIR, "measured_latency_rtt_comparison.png")
    plt.savefig(out_p5, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p5}")

    # -------------------------------------------------------------------------
    # Plot 6: Comprehensive 4-Panel Dashboard
    # -------------------------------------------------------------------------
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))

    # A: Active Throughput
    sns.barplot(data=static_df, x="profile_label", order=order_labels, y="throughputMbps", hue="scheduler", palette=PALETTE, ax=axes[0, 0])
    axes[0, 0].set_title("A. Active Sustained Throughput (Mbps)", fontweight="bold")
    axes[0, 0].set_xlabel("Device Profile")
    axes[0, 0].set_ylabel("Throughput (Mbps)")
    axes[0, 0].tick_params(axis='x', rotation=10)

    # B: Goodput under Dynamic Conditions
    sns.barplot(data=dyn_df, x="scenario_label", order=scenario_order, y="goodputMbps", hue="scheduler", palette=PALETTE, ax=axes[0, 1])
    axes[0, 1].set_title("B. Dynamic Handover & Fading Goodput (Mbps)", fontweight="bold")
    axes[0, 1].set_xlabel("Dynamic Scenario")
    axes[0, 1].set_ylabel("Goodput (Mbps)")

    # C: Latency
    sns.barplot(data=dyn_df, x="scenario_label", order=scenario_order, y="latencyMs", hue="scheduler", palette=PALETTE, ax=axes[1, 0])
    axes[1, 0].set_title("C. Measured Average Round-Trip Time (ms)", fontweight="bold")
    axes[1, 0].set_xlabel("Dynamic Scenario")
    axes[1, 0].set_ylabel("Avg RTT (ms)")

    # D: Retransmissions & Drops
    sns.barplot(data=dyn_df, x="scenario_label", order=scenario_order, y="retransmissions", hue="scheduler", palette=PALETTE, ax=axes[1, 1])
    axes[1, 1].set_title("D. Packet Retransmissions & Drops", fontweight="bold")
    axes[1, 1].set_xlabel("Dynamic Scenario")
    axes[1, 1].set_ylabel("Retransmissions Count")

    plt.suptitle("MPTCP Classical Schedulers vs Standard-TCP: Comprehensive Performance Dashboard",
                 fontsize=16, fontweight="bold", y=0.99)
    plt.tight_layout()
    out_p6 = os.path.join(PLOTS_DIR, "mptcp_comprehensive_metrics_dashboard.png")
    plt.savefig(out_p6, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p6}")

def generate_disparity_plots():
    if not os.path.exists(DISPARITY_CSV):
        print(f"[WARN] Disparity CSV {DISPARITY_CSV} not found!")
        return

    df = pd.read_csv(DISPARITY_CSV)
    delay_points = [1.0, 2.0, 4.0, 8.0, 12.0, 16.0]
    num_sched = 5
    ratios = []
    for d in delay_points:
        for _ in range(num_sched):
            ratios.append(d)

    if len(df) == len(ratios):
        df["disparity_ratio"] = ratios
    else:
        df["disparity_ratio"] = [delay_points[i // num_sched] for i in range(len(df))]

    # Aggregation Efficiency
    df["aggregation_efficiency"] = (df["goodputMbps"] / 40.0) * 100.0

    # -------------------------------------------------------------------------
    # Plot 7: Disparity Dashboard (4-panel)
    # -------------------------------------------------------------------------
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))

    # A: Goodput vs Disparity
    for sched, grp in df.groupby("scheduler"):
        axes[0, 0].plot(grp["disparity_ratio"], grp["goodputMbps"], marker='o', linewidth=2.5,
                        label=sched, color=PALETTE.get(sched, "#333333"))
    axes[0, 0].axhline(20.0, color="#c0392b", linestyle="--", linewidth=1.8, label="Single-Path TCP Ceiling (20 Mbps)")
    axes[0, 0].axhline(40.0, color="#27ae60", linestyle=":", linewidth=1.8, label="Ideal Multi-Path Sum (40 Mbps)")
    axes[0, 0].set_title("A. Sustained Goodput vs Delay Disparity", fontweight="bold")
    axes[0, 0].set_xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)")
    axes[0, 0].set_ylabel("Goodput (Mbps)")
    axes[0, 0].legend()

    # B: Aggregation Efficiency
    for sched, grp in df.groupby("scheduler"):
        axes[0, 1].plot(grp["disparity_ratio"], grp["aggregation_efficiency"], marker='s', linewidth=2.5,
                        label=sched, color=PALETTE.get(sched, "#333333"))
    axes[0, 1].axhline(50.0, color="#c0392b", linestyle="--", linewidth=1.8, label="50% (Single Path Only)")
    axes[0, 1].axhline(100.0, color="#27ae60", linestyle=":", linewidth=1.8, label="100% (Perfect Sum)")
    axes[0, 1].set_title("B. Throughput Aggregation Efficiency (%)", fontweight="bold")
    axes[0, 1].set_xlabel("Delay Disparity Ratio")
    axes[0, 1].set_ylabel("Efficiency (%)")
    axes[0, 1].legend()

    # C: Latency Inflation
    for sched, grp in df.groupby("scheduler"):
        axes[1, 0].plot(grp["disparity_ratio"], grp["latencyMs"], marker='^', linewidth=2.5,
                        label=sched, color=PALETTE.get(sched, "#333333"))
    axes[1, 0].set_title("C. End-to-End Latency vs Delay Disparity", fontweight="bold")
    axes[1, 0].set_xlabel("Delay Disparity Ratio")
    axes[1, 0].set_ylabel("Avg RTT (ms)")
    axes[1, 0].legend()

    # D: Retransmissions
    for sched, grp in df.groupby("scheduler"):
        axes[1, 1].plot(grp["disparity_ratio"], grp["retransmissions"], marker='d', linewidth=2.5,
                        label=sched, color=PALETTE.get(sched, "#333333"))
    axes[1, 1].set_title("D. Packet Retransmissions & Drops vs Disparity", fontweight="bold")
    axes[1, 1].set_xlabel("Delay Disparity Ratio")
    axes[1, 1].set_ylabel("Retransmission Count")
    axes[1, 1].legend()

    plt.suptitle("Research Delay Disparity Sweep: Head-of-Line Blocking & Aggregation Dynamics",
                 fontsize=16, fontweight="bold", y=0.99)
    plt.tight_layout()
    out_p7 = os.path.join(PLOTS_DIR, "mptcp_research_disparity_dashboard.png")
    plt.savefig(out_p7, dpi=300)
    plt.close()
    print(f"[SAVED PLOT] {out_p7}")

    # Also save standalone curves
    plt.figure(figsize=(10, 6))
    for sched, grp in df.groupby("scheduler"):
        plt.plot(grp["disparity_ratio"], grp["goodputMbps"], marker='o', linewidth=2.5,
                 label=sched, color=PALETTE.get(sched, "#333333"))
    plt.axhline(20.0, color="#c0392b", linestyle="--", linewidth=1.8, label="Single-Path TCP Ceiling (20 Mbps)")
    plt.axhline(40.0, color="#27ae60", linestyle=":", linewidth=1.8, label="Ideal Multi-Path Sum (40 Mbps)")
    plt.title("MPTCP Goodput Degradation Under Delay Disparity (HoL Blocking)", fontsize=13, fontweight="bold", pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight="bold")
    plt.ylabel("Session Goodput (Mbps)", fontsize=11, fontweight="bold")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS_DIR, "mptcp_delay_disparity_goodput_curve.png"), dpi=300)
    plt.close()

    plt.figure(figsize=(10, 6))
    for sched, grp in df.groupby("scheduler"):
        plt.plot(grp["disparity_ratio"], grp["aggregation_efficiency"], marker='s', linewidth=2.5,
                 label=sched, color=PALETTE.get(sched, "#333333"))
    plt.axhline(50.0, color="#c0392b", linestyle="--", linewidth=1.8, label="50% (Single Path Only)")
    plt.axhline(100.0, color="#27ae60", linestyle=":", linewidth=1.8, label="100% (Ideal Aggregation)")
    plt.title("MPTCP Aggregation Efficiency Across Delay Disparity", fontsize=13, fontweight="bold", pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight="bold")
    plt.ylabel("Aggregation Efficiency (%)", fontsize=11, fontweight="bold")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS_DIR, "mptcp_aggregation_efficiency_curve.png"), dpi=300)
    plt.close()

    plt.figure(figsize=(10, 6))
    for sched, grp in df.groupby("scheduler"):
        plt.plot(grp["disparity_ratio"], grp["latencyMs"], marker='^', linewidth=2.5,
                 label=sched, color=PALETTE.get(sched, "#333333"))
    plt.title("MPTCP Measured Latency Inflation Under Delay Disparity", fontsize=13, fontweight="bold", pad=12)
    plt.xlabel("Delay Disparity Ratio ($RTT_{slow} / RTT_{fast}$)", fontsize=11, fontweight="bold")
    plt.ylabel("Measured Avg RTT (ms)", fontsize=11, fontweight="bold")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS_DIR, "mptcp_latency_inflation_curve.png"), dpi=300)
    plt.close()

def main():
    if not os.path.exists(CSV_PATH):
        print(f"[ERROR] {CSV_PATH} does not exist!")
        sys.exit(1)

    df = pd.read_csv(CSV_PATH)
    print(f"Loaded {len(df)} rows from {CSV_PATH}")

    print("Generating canonical aggregation charts...")
    generate_canonical_plots(df)

    print("Generating multi-profile and dynamic generalization charts...")
    generate_multi_profile_plots(df)

    print("Generating delay disparity research charts...")
    generate_disparity_plots()

    # Copy all generated plots to artifact directory
    if os.path.exists(ARTIFACT_DIR):
        import shutil
        for fname in os.listdir(PLOTS_DIR):
            if fname.endswith(".png"):
                src = os.path.join(PLOTS_DIR, fname)
                dst = os.path.join(ARTIFACT_DIR, fname)
                shutil.copy2(src, dst)
        print(f"Copied all plots to {ARTIFACT_DIR}")

    print("All plots generated successfully!")

if __name__ == "__main__":
    main()
