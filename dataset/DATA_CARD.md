# MPTCP Supervised Scheduler Dataset Card

## 1. Provenance and Generation
- **Simulation Harness**: ns-3-mptcp with calibrated DropTail queue sizing, delivery-rate estimation and SACK recovery.
- **Scenario Sampling**: Latin Hypercube Sampling across path capacity [2, 50] Mbps, one-way delay [2, 100] ms, loss [0, 3%], and queue sizing [0.5, 3.0]x BDP.
- **Exploration Policies**: Data collected across `minRTT`, `BLEST`, `ECF`, `RoundRobin`, and `EpsRandom` ($\epsilon = 0.3$) to ensure counterfactual action coverage.
- **Total Scenarios**: 77
- **Total Labeled Rows**: 396098

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
  $$\text{Delay}_i = \text{RTT}_i + \frac{\text{InFlight}_i \times 8}{\text{BW}_i} + \text{LossPenalty}$$

## 4. Scenario-Grouped Splits
Splits are grouped strictly by whole `scenario_id` to prevent intra-session data leakage:
- `train`: Training set for model selection and cross-validation.
- `val`: Validation set for hyperparameter tuning.
- `test_g0_indist`: In-distribution reference test.
- `test_g1_extrapolation`: Disparity up to 16x, high loss.
- `test_g2_topology_shift`: 3-path TriPath topology.
- `test_g3_dynamics_shift`: Unseen dynamic patterns (quality swap, delay spikes, outages).
