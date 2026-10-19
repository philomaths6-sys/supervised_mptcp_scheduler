# Presentation: Adaptive Multipath Transport Scheduling in Heterogeneous Networks

**NITK Surathkal — Department of Computer Science & Engineering**  
**Course Project**: Advanced Computer Networks  
**Authors**: Saurabh Kumar (241CS151), Shikhar (241CS153), Vikash Patel (241CS163)  

---

## Slide 1: Motivation & Problem Formulation

### Multipath TCP (RFC 8684) Promise:
- Combine heterogeneous access networks simultaneously (e.g., Wi-Fi + 5G/LTE).
- Boost aggregate throughput and provide seamless mobility/failover.

### The Heterogeneity Pitfall:
- **Delay Disparity**: Packet sent over a 45 ms LTE path arrives much later than packets on a 15 ms Wi-Fi path.
- **Head-of-Line (HoL) Blocking**: Out-of-order packets fill the receiver buffer; the meta-socket freezes waiting for missing packets.
- **MinRTT Failure**: Blindly sends on the first available path once minRTT window is full, triggering severe HoL collapse.

---

## Slide 2: The Baseline Audit — Why Did Prior Work Fail?

### Critical Defects Found & Repaired (Phase 0):
1. **Subflow Recovery Bug**: `TcpL4Protocol::CreateSocket` lacked `socket->SetRecoveryAlgorithm(recovery)`. Subflows collapsed to 1 MSS on any packet loss!
2. **Missing SACK**: Default was non-SACK Reno; any drop caused RTO freeze. Enabling RFC 6675 SACK restored dual-path throughput from 17.39 Mbps to **28.97 Mbps**.
3. **Dead Telemetry**: `TelemetryTracker::RegisterSubflow()` had zero callers; minRTT and BLEST ran on static constants and produced identical bits.
4. **Inflated Metrics**: Replaced raw link sniffers with true `PacketSink` in-order goodput measurement.

*All 4 sanity tests verified and passing in `experiments/sanity/run_sanity_suite.py`.*

---

## Slide 3: Architecture of the Supervised Scheduler

### Scale-Free 12D Feature Extractor
Relative normalized features enable zero-shot transfer across unseen speeds and delays:
- Latency disparity ratio ($RTT_i / \min RTT$)
- Available window headroom ratio ($availWin_i / cwnd_i$)
- Relative in-flight queue backlog ($inFlight_i / cwnd_i$)
- Bufferbloat inflation index ($RTT_i / minRTT_i$)
- Windowed empirical loss rate estimate

### Decision Policy & Guardrails
- **Continuous Delay Prediction**: Evaluates expected delivery latency per eligible subflow.
- **Disparity Wait Rule**: If fast path window is exhausted, but sending on the slow path will arrive later than waiting for the fast path, **defer transmission**.
- **Initial Probing**: Probes each path during startup to seed telemetry.
- **Safety Fallback**: Reverts to minRTT if any feature is non-finite.

---

## Slide 4: Offline Training & In-Kernel C++ Export

### Dataset & Model Selection:
- **396,098 labeled simulation decisions** across 77 LHS scenarios (`dataset/processed/dataset.csv`).
- Grouped 5-fold cross-validation by scenario (`GroupKFold`) — zero data leakage!
- **Random Forest (5 Trees, Depth 6)**:
  - Validation MAE: **23.01 ms**
  - Validation $R^2$: **0.893**

### Flat Array C++ Header Export:
- Exported via `ml/export_cpp.py` to `adaptive_supervised_scheduler/model_generated.h`.
- **Zero runtime ML dependencies** (no PyTorch, no ONNX, no TensorFlow).
- **Size**: **19.87 KB** (< 100 KB budget).
- **Inference Latency**: **1.2 $\mu$s per packet** (< 5 $\mu$s budget).
- **Equivalence**: Verified against scikit-learn with max error $< 1.5 \times 10^{-5}$.

---

## Slide 5: Objective (ii) — Comparative Evaluation Matrix

*Across 700 simulations over 10 RNG seeds with 95% Student's $t$ Confidence Intervals:*

### Multi-Topology Benchmark Highlights:
- **Ethernet + Wi-Fi (25M/5ms + 15M/20ms)**:
  - `minRTT`: 2.28 Mbps | 0.38% loss (severe HoL collapse)
  - `BLEST`: 11.62 Mbps | 0.13% loss
  - `Supervised`: **16.13 Mbps** | **0.01% loss** (**+608.2% vs minRTT**, $p = 0.00195$)
- **Tri-Path Edge Gateway (30M/5ms + 15M/20ms + 10M/50ms)**:
  - `minRTT`: 2.33 Mbps | 0.38% loss
  - `BLEST`: 12.51 Mbps | 0.12% loss
  - `Supervised`: **19.83 Mbps** | **0.01% loss** (**+751.4% vs minRTT**, $p = 0.00195$)
- **Mobile Wi-Fi (12M/15ms) + LTE (6M/45ms)**:
  - `minRTT`: 0.87 Mbps | 1.06% loss
  - `Supervised`: **3.28 Mbps** | **0.64% loss** (**+275.4% vs minRTT**, $p = 0.00195$)

### TCP Fairness & Head-of-Line Blocking Inversion Explained:
- **Standard-TCP is stock ns-3 `TcpSocketBase`/`TcpNewReno`**: NOT a custom TCP implementation.
- **Why Standard-TCP gets ~24 Mbps**: It runs **exclusively on Path 0** (pristine Ethernet, 0.01% loss, 5ms delay). On Path 1 (Wi-Fi, 0.8% loss), Standard-TCP gets only **2.17 Mbps**!
- **Why Naive MPTCP Collapses**: Spilling packets onto the lossy secondary path stalls the receiver queue, triggering Zero-Window advertisements that halt the 25 Mbps Ethernet link.
- **Supervised ML Fix**: Model predicts completion delay and defers to the fast path, eliminating HoL blocking.

---

## Slide 6: Delay Disparity Robustness (1.0x to 16.0x)

### How Schedulers Behave as Latency Gap Widens:
- **1.0x Disparity (15ms vs 15ms)**: Both `minRTT` and `Supervised` aggregate cleanly at 4.22 Mbps.
- **4.0x Disparity (15ms vs 60ms)**: `minRTT` collapses to 0.87 Mbps due to receiver buffer exhaustion. `Supervised` sustains **3.28 Mbps** (3.77x gain).
- **16.0x Disparity (5ms vs 80ms)**: `Supervised` sustains **5.41 Mbps** by waiting for the 5 ms path and preventing HoL stall.

---

## Slide 7: Objective (iii) — Generalization Study (G0–G4)

*Evaluating on completely unseen out-of-distribution conditions:*

| Dimension | Scenario | Supervised | minRTT | Supervised Gain |
| :--- | :--- | :---: | :---: | :---: |
| **G0** | In-Distribution Wi-Fi + Cell | 3.25 Mbps | 0.99 Mbps | **+229.8%** |
| **G1** | Extreme Disparity (16x) | 5.41 Mbps | 3.49 Mbps | **+55.0%** |
| **G1** | High Packet Loss (4.5%) | 1.01 Mbps | 0.41 Mbps | **+144.4%** |
| **G3** | Dynamic Link Outage | 2.88 Mbps | 0.93 Mbps | **+210.3%** |
| **G4** | Large Flow Transfer (5 MB) | 3.42 Mbps | 0.91 Mbps | **+277.1%** |

---

## Slide 8: Mechanistic Insight — What Features Matter?

### Permutation Feature Importance Ranking:
1. **RTT Ratio to fastest path**: **+87.78 ms (+1231.0% MAE surge)**  
   *Dominant factor in determining out-of-order arrival and HoL penalty.*
2. **Inflight Queue Ratio**: **+47.48 ms (+665.8% MAE surge)**  
   *Crucial for detecting transient buffer backlog.*
3. **Min RTT Ratio**: **+17.27 ms (+242.2% MAE surge)**  
   *Reflects physical channel propagation limit.*
4. **Total Connection Window**: **+0.81 ms (+11.4% MAE surge)**
5. **Loss Rate Estimate**: **+0.60 ms (+8.4% MAE surge)**

---

## Slide 9: Conclusion & Reproducibility

### Key Takeaways:
1. Supervised learning with scale-free relative features successfully avoids HoL blocking without manual heuristic tuning.
2. Flat C++ array export enables in-kernel decision tree inference in $< 1.5\,\mu\text{s}$ with zero ML runtime overhead.
3. Rigorous validation over 10 seeds demonstrates statistically significant improvements ($p < 0.01$) over standard minRTT and BLEST.

### Reproducibility:
- Run all pipeline stages with a single command:
  ```bash
  ./scripts/run_all.sh
  ```
- All artifacts, data cards, logs, and plots are preserved in the repository.
