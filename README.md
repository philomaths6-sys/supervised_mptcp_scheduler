# Adaptive Supervised MPTCP Scheduler in Heterogeneous Networks

A lightweight supervised-learning-based Multipath TCP (MPTCP) packet scheduler implemented in ns-3, evaluated under heterogeneous and dynamic network conditions against classical schedulers (minRTT, RoundRobin, BLEST, and ECF).

## Project Structure

- `classical_schedulers/`: Classical and heuristic MPTCP schedulers (minRTT / FastestRTT, RoundRobin, and upcoming BLEST & ECF).
- `adaptive_supervised_scheduler/`: Our lightweight supervised ML scheduler implementation and C++ inference engine.
- `experiments/topologies/`: Parametric ns-3 simulation topologies (heterogeneous paths, dynamic variations, loss, and queueing).
- `dataset/`: Training and validation datasets extracted from heterogeneous network traces.
- `ml/`: Model training, hyperparameter optimization, and weights exporter (Decision Tree, Random Forest, Lightweight MLP).
- `results/`: Benchmarking metrics (goodput, throughput, HoL blocking, retransmissions, flow completion time) and plots.

## Quick Start: Running Heterogeneous Topology

```bash
cd ns-3-mptcp
./waf --run "scratch/heterogeneous_topology --path0Bw=20Mbps --path0Delay=10ms --path1Bw=10Mbps --path1Delay=50ms --scheduler=FastestRTT"
```
