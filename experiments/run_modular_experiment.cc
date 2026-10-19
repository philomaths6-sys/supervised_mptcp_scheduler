/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include "../classical_schedulers/telemetry_tracker.h"
#include "../classical_schedulers/mptcp-scheduler-min-rtt.h"
#include "../classical_schedulers/mptcp-scheduler-round-robin.h"
#include "../classical_schedulers/mptcp-scheduler-blest.h"
#include "../classical_schedulers/mptcp-scheduler-ecf.h"
#include "../classical_schedulers/mptcp-scheduler-oracle.h"
#include "../classical_schedulers/mptcp-scheduler-eps-random.h"
#include "../adaptive_supervised_scheduler/mptcp-scheduler-supervised.h"
#include "../adaptive_supervised_scheduler/decision_logger.h"
#include "topologies/link_profile.h"
#include "topologies/dynamic_controller.h"
#include "topologies/modular_mptcp_topology.h"

#include <iostream>
#include <fstream>
#include <string>

using namespace ns3;

NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerMinRtt);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerRoundRobin);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerBlest);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerEcf);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerOracle);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerSupervised);
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerEpsRandom);

int main (int argc, char *argv[])
{
  std::string protocol = "FastestRTT";
  std::string topology = "WiFi_Cellular";
  double duration = 10.0;
  uint32_t seed = 1;
  uint32_t run = 1;

  // Path 0 overrides (defaults set per topology)
  std::string path0Rate = "";
  std::string path0Delay = "";
  double path0Loss = -1.0;

  // Path 1 overrides
  std::string path1Rate = "";
  std::string path1Delay = "";
  double path1Loss = -1.0;

  // Path 2 overrides (for TriPath)
  std::string path2Rate = "";
  std::string path2Delay = "";
  double path2Loss = -1.0;

  double disparity = -1.0; // Negative means use topology default
  bool handover = false;
  std::string eventsFile = "";
  std::string logDecisions = "";
  std::string timeSeries = "";
  uint64_t maxBytes = 0;
  double epsilon = 0.3;
  std::string outputCsv = "";
  bool printJson = false;
  uint32_t rcvBuf = 0;

  CommandLine cmd;
  cmd.AddValue ("protocol", "MPTCP Scheduler (Standard-TCP, FastestRTT, minRTT, RoundRobin, BLEST, ECF, Oracle, Supervised, EpsRandom)", protocol);
  cmd.AddValue ("topology", "Network topology (WiFi_Cellular, Ethernet_WiFi, TriPath)", topology);
  cmd.AddValue ("duration", "Simulation duration in seconds", duration);
  cmd.AddValue ("seed", "RNG seed for simulation repeatability", seed);
  cmd.AddValue ("run", "RNG run index", run);
  cmd.AddValue ("path0Rate", "Data rate override for Path 0", path0Rate);
  cmd.AddValue ("path0Delay", "Delay override for Path 0", path0Delay);
  cmd.AddValue ("path0Loss", "Loss rate override for Path 0", path0Loss);
  cmd.AddValue ("path1Rate", "Data rate override for Path 1", path1Rate);
  cmd.AddValue ("path1Delay", "Delay override for Path 1", path1Delay);
  cmd.AddValue ("path1Loss", "Loss rate override for Path 1", path1Loss);
  cmd.AddValue ("path2Rate", "Data rate override for Path 2", path2Rate);
  cmd.AddValue ("path2Delay", "Delay override for Path 2", path2Delay);
  cmd.AddValue ("path2Loss", "Loss rate override for Path 2", path2Loss);
  cmd.AddValue ("disparity", "Delay disparity ratio between Path 1 and Path 0", disparity);
  cmd.AddValue ("handover", "Enable dynamic fading handover event", handover);
  cmd.AddValue ("events", "Path to dynamic network events CSV file", eventsFile);
  cmd.AddValue ("logDecisions", "Path to decision logger CSV file", logDecisions);
  cmd.AddValue ("timeSeries", "Path to 100ms time-series CSV file", timeSeries);
  cmd.AddValue ("maxBytes", "Total payload bytes for fixed-size flow FCT measurement (0 for bulk)", maxBytes);
  cmd.AddValue ("epsilon", "Exploration rate for EpsRandom scheduler", epsilon);
  cmd.AddValue ("rcvBuf", "Receiver buffer size in bytes (e.g. 131072 for 128KB, 65536 for 64KB)", rcvBuf);
  cmd.AddValue ("outputCsv", "Path to CSV file to append output metrics", outputCsv);
  cmd.AddValue ("json", "Print output in JSON format", printJson);
  cmd.Parse (argc, argv);

  // Set RNG Seed and Run
  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (run);

  ModularMpTcpTopology topo;
  topo.SetDuration (Seconds (duration));
  if (rcvBuf > 0)
    {
      topo.SetRcvBufSize (rcvBuf);
    }

  std::vector<LinkProfile> paths;

  if (topology == "PentaPath" || topology == "FivePath")
    {
      topology = "PentaPath";
      // 5 Interfaces: WiFi6 (20M/15ms), 5G_mmWave (25M/10ms), Ethernet (30M/8ms), CellularLTE (15M/20ms), Satellite (5M/40ms) [Total: 95 Mbps]
      paths.push_back (LinkProfile ("WiFi6",     "20Mbps", MilliSeconds (15), 0.002));
      paths.push_back (LinkProfile ("5G_mmWave", "25Mbps", MilliSeconds (10), 0.0008));
      paths.push_back (LinkProfile ("Ethernet",  "30Mbps", MilliSeconds (8),  0.001));
      paths.push_back (LinkProfile ("CellularLTE","15Mbps", MilliSeconds (20), 0.0015));
      paths.push_back (LinkProfile ("Satellite", "5Mbps",  MilliSeconds (40), 0.003));
    }
  else if (topology == "QuadPath" || topology == "FourPath")
    {
      topology = "QuadPath";
      // 4 Interfaces: WiFi (20M/15ms), Cellular5G (25M/10ms), Ethernet (25M/8ms), CellularLTE (10M/25ms) [Total: 80 Mbps]
      paths.push_back (LinkProfile ("WiFi",       "20Mbps", MilliSeconds (15), 0.002));
      paths.push_back (LinkProfile ("Cellular5G", "25Mbps", MilliSeconds (10), 0.001));
      paths.push_back (LinkProfile ("Ethernet",   "25Mbps", MilliSeconds (8),  0.001));
      paths.push_back (LinkProfile ("CellularLTE","10Mbps", MilliSeconds (25), 0.002));
    }
  else if (topology == "TriPath" || topology == "TriPath_Eth_WiFi_Cell")
    {
      std::string p0R = path0Rate.empty () ? "30Mbps" : path0Rate;
      Time p0D = path0Delay.empty () ? MilliSeconds (10) : Time (path0Delay);
      double p0L = (path0Loss < 0.0) ? 0.002 : path0Loss;

      std::string p1R = path1Rate.empty () ? "15Mbps" : path1Rate;
      Time p1D = path1Delay.empty () ? MilliSeconds (20) : Time (path1Delay);
      double p1L = (path1Loss < 0.0) ? 0.001 : path1Loss;

      std::string p2R = path2Rate.empty () ? "10Mbps" : path2Rate;
      Time p2D = path2Delay.empty () ? MilliSeconds (30) : Time (path2Delay);
      double p2L = (path2Loss < 0.0) ? 0.0005 : path2Loss;

      if (disparity >= 0.0)
        {
          p1D = MilliSeconds (std::max (1.0, p0D.GetMilliSeconds () * disparity));
        }

      paths.push_back (LinkProfile ("Cellular5G", p0R, p0D, p0L));
      paths.push_back (LinkProfile ("WiFi", p1R, p1D, p1L));
      paths.push_back (LinkProfile ("CellularLTE", p2R, p2D, p2L));
    }
  else if (topology == "Ethernet_WiFi")
    {
      std::string p0R = path0Rate.empty () ? "25Mbps" : path0Rate;
      Time p0D = path0Delay.empty () ? MilliSeconds (10) : Time (path0Delay);
      double p0L = (path0Loss < 0.0) ? 0.002 : path0Loss;

      std::string p1R = path1Rate.empty () ? "15Mbps" : path1Rate;
      Time p1D = path1Delay.empty () ? MilliSeconds (25) : Time (path1Delay);
      double p1L = (path1Loss < 0.0) ? 0.0008 : path1Loss;

      if (disparity >= 0.0)
        {
          p1D = MilliSeconds (std::max (1.0, p0D.GetMilliSeconds () * disparity));
        }

      paths.push_back (LinkProfile ("WiFi", p0R, p0D, p0L));
      paths.push_back (LinkProfile ("CellularLTE", p1R, p1D, p1L));
    }
  else
    {
      topology = "WiFi_Cellular";
      std::string p0R = path0Rate.empty () ? "20Mbps" : path0Rate;
      Time p0D = path0Delay.empty () ? MilliSeconds (15) : Time (path0Delay);
      double p0L = (path0Loss < 0.0) ? 0.002 : path0Loss;

      std::string p1R = path1Rate.empty () ? "10Mbps" : path1Rate;
      Time p1D = path1Delay.empty () ? MilliSeconds (30) : Time (path1Delay);
      double p1L = (path1Loss < 0.0) ? 0.0005 : path1Loss;

      if (disparity >= 0.0)
        {
          p1D = MilliSeconds (std::max (1.0, p0D.GetMilliSeconds () * disparity));
        }

      paths.push_back (LinkProfile ("WiFi", p0R, p0D, p0L));
      paths.push_back (LinkProfile ("CellularLTE", p1R, p1D, p1L));
    }

  ExperimentMetrics m = topo.Run (protocol, topology, paths, eventsFile, handover,
                                  maxBytes, seed, run, logDecisions, timeSeries, epsilon);

  double realizedDisparity = (paths.size () > 1 && paths[0].delay.GetMilliSeconds () > 0)
    ? ((double)paths[1].delay.GetMilliSeconds () / (double)paths[0].delay.GetMilliSeconds ())
    : 1.0;

  if (printJson)
    {
      std::cout << "{\n"
                << "  \"protocol\": \"" << m.protocol << "\",\n"
                << "  \"topology\": \"" << m.topology << "\",\n"
                << "  \"seed\": " << seed << ",\n"
                << "  \"run\": " << run << ",\n"
                << "  \"disparity\": " << realizedDisparity << ",\n"
                << "  \"totalCapacityMbps\": " << m.totalCapacityMbps << ",\n"
                << "  \"activeThroughputMbps\": " << m.activeThroughputMbps << ",\n"
                << "  \"sessionGoodputMbps\": " << m.sessionGoodputMbps << ",\n"
                << "  \"packetLossPercent\": " << m.packetLossPercent << ",\n"
                << "  \"meanDelayMs\": " << m.meanDelayMs << ",\n"
                << "  \"holDelayMs\": " << m.holDelayMs << ",\n"
                << "  \"jitterMs\": " << m.jitterMs << ",\n"
                << "  \"aggregationEfficiencyPercent\": " << m.aggregationEfficiencyPercent << ",\n"
                << "  \"flowCompletionTimeSec\": " << m.flowCompletionTimeSec << ",\n"
                << "  \"totalBytesReceived\": " << m.totalBytesReceived << ",\n"
                << "  \"totalPacketsSent\": " << m.totalPacketsSent << ",\n"
                << "  \"totalPacketsLost\": " << m.totalPacketsLost << ",\n"
                << "  \"path0Bytes\": " << m.path0Bytes << ",\n"
                << "  \"path1Bytes\": " << m.path1Bytes << ",\n"
                << "  \"path2Bytes\": " << m.path2Bytes << "\n"
                << "}\n";
    }
  else
    {
      std::cout << "=== MPTCP Benchmark Result ===\n"
                << "Topology:             " << m.topology << " (Cap: " << m.totalCapacityMbps << " Mbps)\n"
                << "Protocol:             " << m.protocol << "\n"
                << "Active Throughput:    " << std::fixed << std::setprecision (2) << m.activeThroughputMbps << " Mbps\n"
                << "Session Goodput:      " << std::fixed << std::setprecision (2) << m.sessionGoodputMbps << " Mbps\n"
                << "Aggregation Eff.:     " << std::fixed << std::setprecision (2) << m.aggregationEfficiencyPercent << " %\n"
                << "Packet Loss Rate:     " << std::fixed << std::setprecision (2) << m.packetLossPercent << " %\n"
                << "Mean Latency:         " << std::fixed << std::setprecision (2) << m.meanDelayMs << " ms\n"
                << "HoL Delay:            " << std::fixed << std::setprecision (2) << m.holDelayMs << " ms\n"
                << "Estimated Jitter:     " << std::fixed << std::setprecision (2) << m.jitterMs << " ms\n"
                << "Flow Completion Time: " << std::fixed << std::setprecision (3) << m.flowCompletionTimeSec << " s\n";
      for (size_t p = 0; p < m.perPathBytes.size (); ++p)
        {
          std::cout << "Path " << p << " Bytes:         " << m.perPathBytes[p]
                    << " (" << std::fixed << std::setprecision (1)
                    << (m.totalBytesReceived > 0 ? (double)m.perPathBytes[p] / m.totalBytesReceived * 100.0 : 0.0) << "%)\n";
        }
      std::cout << "Total Bytes Rx:       " << m.totalBytesReceived << " bytes\n"
                << "==============================\n";
    }

  if (!outputCsv.empty ())
    {
      std::ifstream infile (outputCsv);
      bool writeHeader = !infile.good ();
      infile.close ();

      std::ofstream outfile (outputCsv, std::ios::app);
      if (writeHeader)
        {
          outfile << "protocol,topology,disparity,seed,run,totalCapacityMbps,activeThroughputMbps,sessionGoodputMbps,packetLossPercent,meanDelayMs,holDelayMs,jitterMs,aggregationEfficiencyPercent,flowCompletionTimeSec,totalBytesReceived,totalPacketsSent,totalPacketsLost,path0Bytes,path1Bytes,path2Bytes,handover\n";
        }
      outfile << m.protocol << ","
              << m.topology << ","
              << realizedDisparity << ","
              << seed << ","
              << run << ","
              << m.totalCapacityMbps << ","
              << m.activeThroughputMbps << ","
              << m.sessionGoodputMbps << ","
              << m.packetLossPercent << ","
              << m.meanDelayMs << ","
              << m.holDelayMs << ","
              << m.jitterMs << ","
              << m.aggregationEfficiencyPercent << ","
              << m.flowCompletionTimeSec << ","
              << m.totalBytesReceived << ","
              << m.totalPacketsSent << ","
              << m.totalPacketsLost << ","
              << m.path0Bytes << ","
              << m.path1Bytes << ","
              << m.path2Bytes << ","
              << (handover ? 1 : 0) << "\n";
      outfile.close ();
    }

  std::cout.flush ();
  std::cerr.flush ();
  _Exit (0);
}
