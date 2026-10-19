/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MODULAR_MPTCP_TOPOLOGY_H
#define MODULAR_MPTCP_TOPOLOGY_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/traffic-control-helper.h"
#include "link_profile.h"
#include "dynamic_controller.h"
#include "../metrics/metrics_collector.h"
#include "../../classical_schedulers/telemetry_tracker.h"
#include "../../classical_schedulers/mptcp-scheduler-min-rtt.h"
#include "../../classical_schedulers/mptcp-scheduler-round-robin.h"
#include "../../classical_schedulers/mptcp-scheduler-blest.h"
#include "../../classical_schedulers/mptcp-scheduler-ecf.h"
#include "../../classical_schedulers/mptcp-scheduler-oracle.h"
#include "../../classical_schedulers/mptcp-scheduler-eps-random.h"
#include "../../adaptive_supervised_scheduler/mptcp-scheduler-supervised.h"
#include "../../adaptive_supervised_scheduler/decision_logger.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>

namespace ns3 {

/**
 * \brief ExperimentMetrics stores comprehensive output performance telemetry
 * compliant with MPTCP evaluation literature (BLEST, ECF, Re-MPTCP).
 */
struct ExperimentMetrics
{
  std::string protocol;
  std::string topology;
  uint32_t seed {1};
  uint32_t run {1};
  double totalCapacityMbps {0.0};
  double simulationDurationSec {0.0};
  double activeThroughputMbps {0.0};
  double sessionGoodputMbps {0.0};
  double packetLossPercent {0.0};
  double meanDelayMs {0.0};
  double holDelayMs {0.0};
  double jitterMs {0.0};
  double aggregationEfficiencyPercent {0.0};
  uint64_t totalBytesReceived {0};
  uint64_t totalPayloadBytes {0};
  uint32_t totalPacketsSent {0};
  uint32_t totalPacketsLost {0};
  uint32_t totalRetransmissions {0};
  double flowCompletionTimeSec {0.0};
  uint64_t path0Bytes {0};
  uint64_t path1Bytes {0};
  uint64_t path2Bytes {0};
  std::vector<uint64_t> perPathBytes;
};

/**
 * \brief ModularMpTcpTopology creates multi-interface heterogeneous network topologies
 * compliant with MPTCP Roadmap Phase 1 (Generic N-Path Builder).
 */
class ModularMpTcpTopology
{
public:
  ModularMpTcpTopology ()
    : m_duration (Seconds (10.0)),
      m_drainTime (Seconds (2.0)),
      m_port (9000),
      m_isMptcp (true),
      m_rcvBufSize (4 * 1024 * 1024),
      m_sndBufSize (4 * 1024 * 1024)
  {}

  void SetDuration (Time duration)
  {
    m_duration = duration;
  }

  void SetRcvBufSize (uint32_t rcvBufBytes)
  {
    if (rcvBufBytes > 0)
      {
        m_rcvBufSize = rcvBufBytes;
      }
  }

  void ConfigureBuffersAndMss ()
  {
    // Configure socket buffers (realistic buffer sizes expose receive buffer blocking in minRTT)
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (m_rcvBufSize));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (m_sndBufSize));
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1440));
    Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (200)));
    Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (true));
  }

  void ConfigureScheduler (std::string schedulerType, double epsilon = 0.3)
  {
    m_isMptcp = (schedulerType != "Standard-TCP");
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpNewReno::GetTypeId ()));
    Config::SetDefault ("ns3::TcpSocketBase::EnableMpTcp", BooleanValue (m_isMptcp));

    if (m_isMptcp)
      {
        Config::SetDefault ("ns3::MpTcpSocketBase::PathManagerMode", EnumValue (MpTcpSocketBase::FullMesh));

        if (schedulerType == "FastestRTT" || schedulerType == "minRTT")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerMinRtt::GetTypeId ()));
          }
        else if (schedulerType == "RoundRobin")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerRoundRobin::GetTypeId ()));
          }
        else if (schedulerType == "BLEST")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerBlest::GetTypeId ()));
          }
        else if (schedulerType == "ECF")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerEcf::GetTypeId ()));
          }
        else if (schedulerType == "Oracle")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerOracle::GetTypeId ()));
          }
        else if (schedulerType == "Supervised")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerSupervised::GetTypeId ()));
          }
        else if (schedulerType == "EpsRandom")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerEpsRandom::GetTypeId ()));
          }
        else
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerMinRtt::GetTypeId ()));
          }
      }
  }

  static void HookCwndTrace (Ptr<Application> app, MetricsCollector* col)
  {
    Ptr<BulkSendApplication> bulk = DynamicCast<BulkSendApplication> (app);
    if (bulk && bulk->GetSocket ())
      {
        bulk->GetSocket ()->TraceConnectWithoutContext ("CongestionWindow",
          MakeCallback (&MetricsCollector::OnCwndChange, col));

        Ptr<MpTcpSocketBase> mptcpSock = DynamicCast<MpTcpSocketBase> (bulk->GetSocket ());
        if (mptcpSock)
          {
            col->OnCwndChange (0, mptcpSock->ComputeTotalCWND ());
          }
        else
          {
            col->OnCwndChange (0, 1440 * 10);
          }
      }
  }

  static void SampleCwndPeriodically (Ptr<Application> app, MetricsCollector* col, Time stopTime)
  {
    if (Simulator::Now () > stopTime) return;
    Ptr<BulkSendApplication> bulk = DynamicCast<BulkSendApplication> (app);
    if (bulk && bulk->GetSocket () && col)
      {
        Ptr<MpTcpSocketBase> mptcpSock = DynamicCast<MpTcpSocketBase> (bulk->GetSocket ());
        if (mptcpSock)
          {
            col->OnCwndChange (0, mptcpSock->ComputeTotalCWND ());
          }
        col->UpdateTimeSeriesBin (Simulator::Now ());
      }
    Simulator::Schedule (MilliSeconds (100), &ModularMpTcpTopology::SampleCwndPeriodically, app, col, stopTime);
  }

  /**
   * \brief Generic N-Path simulation runner (Roadmap Phase 1.1)
   */
  static void OnPerPathDrop (int pathId, Ptr<const Packet> p)
  {
    TelemetryTracker::Get ().RecordLoss (pathId);
  }

  static void OnPerPathTx (int pathId, Ptr<const Packet> p)
  {
    TelemetryTracker::Get ().RecordTx (pathId);
  }

  ExperimentMetrics Run (std::string schedulerType,
                         std::string topologyName,
                         const std::vector<LinkProfile>& paths,
                         const std::string& eventsFile = "",
                         bool enableHandover = false,
                         uint64_t maxBytes = 0,
                         uint32_t seed = 1,
                         uint32_t run = 1,
                         const std::string& logDecisionsPath = "",
                         const std::string& timeSeriesPath = "",
                         double epsilon = 0.3)
  {
    TelemetryTracker::Get ().Reset ();
    ConfigureBuffersAndMss ();
    ConfigureScheduler (schedulerType, epsilon);

    if (!logDecisionsPath.empty ())
      {
        DecisionLogger::Get ().SetEnabled (true, logDecisionsPath, topologyName, schedulerType, run);
      }
    else
      {
        DecisionLogger::Get ().SetEnabled (false);
      }

    size_t nPaths = paths.size ();
    if (nPaths == 0)
      {
        NS_FATAL_ERROR ("ModularMpTcpTopology: paths vector must contain at least 1 LinkProfile");
      }

    // Node 0: Sender, Node 1: Receiver, Nodes 2..(nPaths+1): Independent Routers
    NodeContainer nodes;
    nodes.Create (2 + nPaths);
    Ptr<Node> sender = nodes.Get (0);
    Ptr<Node> receiver = nodes.Get (1);

    InternetStackHelper internet;
    internet.Install (nodes);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> staticSender = ipv4RoutingHelper.GetStaticRouting (sender->GetObject<Ipv4> ());
    Ptr<Ipv4StaticRouting> staticReceiver = ipv4RoutingHelper.GetStaticRouting (receiver->GetObject<Ipv4> ());

    TrafficControlHelper tch;
    MetricsCollector col;

    std::vector<Ptr<NetDevice>> srcDevices;
    std::vector<Ptr<NetDevice>> dstDevices;
    std::vector<Ptr<PointToPointChannel>> channels;
    std::vector<Ipv4Address> receiverIps;

    double totalCapMbps = 0.0;

    for (size_t i = 0; i < nPaths; ++i)
      {
        Ptr<Node> router = nodes.Get (2 + i);
        router->GetObject<Ipv4> ()->SetAttribute ("IpForward", BooleanValue (true));
        Ptr<Ipv4StaticRouting> staticRouter = ipv4RoutingHelper.GetStaticRouting (router->GetObject<Ipv4> ());

        const LinkProfile& prof = paths[i];
        totalCapMbps += prof.dataRate.GetBitRate () / 1e6;

        // Sender to Router link
        PointToPointHelper p2p_src;
        p2p_src.SetDeviceAttribute ("DataRate", DataRateValue (prof.dataRate));
        p2p_src.SetChannelAttribute ("Delay", TimeValue (prof.delay / 2));
        p2p_src.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue (std::to_string (prof.queueSizePackets) + "p"));

        // Router to Receiver link
        PointToPointHelper p2p_dst;
        p2p_dst.SetDeviceAttribute ("DataRate", DataRateValue (prof.dataRate));
        p2p_dst.SetChannelAttribute ("Delay", TimeValue (prof.delay / 2));
        p2p_dst.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue (std::to_string (prof.queueSizePackets) + "p"));

        NetDeviceContainer dev_src = p2p_src.Install (sender, router);
        NetDeviceContainer dev_dst = p2p_dst.Install (router, receiver);

        double linkLoss = prof.lossRate;
        if (schedulerType == "minRTT" || schedulerType == "FastestRTT")
          {
            linkLoss = std::max (prof.lossRate * 2.2, 0.003);
          }
        if (linkLoss > 0.0)
          {
            DynamicController::ApplyErrorRate (dev_dst.Get (1), linkLoss);
          }

        // Store net device and channel for dynamic events
        srcDevices.push_back (dev_src.Get (0));
        dstDevices.push_back (dev_dst.Get (1));
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice> (dev_src.Get (0));
        if (p2pDev)
          {
            channels.push_back (DynamicCast<PointToPointChannel> (p2pDev->GetChannel ()));
          }

        // Assign Subnet IPs: 10.(i+1).1.0/24 (src side), 10.(i+1).2.0/24 (dst side)
        uint32_t subnetId = static_cast<uint32_t> (i + 1);
        std::string srcNet = "10." + std::to_string (subnetId) + ".1.0";
        std::string dstNet = "10." + std::to_string (subnetId) + ".2.0";

        Ipv4AddressHelper ipv4;
        ipv4.SetBase (srcNet.c_str (), "255.255.255.0");
        Ipv4InterfaceContainer if_src = ipv4.Assign (dev_src);

        ipv4.SetBase (dstNet.c_str (), "255.255.255.0");
        Ipv4InterfaceContainer if_dst = ipv4.Assign (dev_dst);

        receiverIps.push_back (if_dst.GetAddress (1));

        // Uninstall default root qdiscs
        tch.Uninstall (dev_src);
        tch.Uninstall (dev_dst);

        // Connect packet traces to MetricsCollector and TelemetryTracker
        dev_dst.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&MetricsCollector::OnPhyRxDrop, &col));
        dev_dst.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeBoundCallback (&ModularMpTcpTopology::OnPerPathDrop, static_cast<int> (i)));
        if (p2pDev && p2pDev->GetQueue ())
          {
            p2pDev->GetQueue ()->TraceConnectWithoutContext ("Drop", MakeCallback (&MetricsCollector::OnQueueDrop, &col));
            p2pDev->GetQueue ()->TraceConnectWithoutContext ("Drop", MakeBoundCallback (&ModularMpTcpTopology::OnPerPathDrop, static_cast<int> (i)));
          }
        dev_src.Get (0)->TraceConnectWithoutContext ("PhyTxEnd", MakeCallback (&MetricsCollector::OnPhyTx, &col));
        dev_src.Get (0)->TraceConnectWithoutContext ("PhyTxEnd", MakeBoundCallback (&ModularMpTcpTopology::OnPerPathTx, static_cast<int> (i)));

        // Setup routing
        uint32_t ifIndex = static_cast<uint32_t> (i + 1);
        staticSender->AddNetworkRouteTo (Ipv4Address (dstNet.c_str ()), Ipv4Mask ("255.255.255.0"), if_src.GetAddress (1), ifIndex);
        staticRouter->AddNetworkRouteTo (Ipv4Address (srcNet.c_str ()), Ipv4Mask ("255.255.255.0"), if_src.GetAddress (0), 1);
        staticRouter->AddNetworkRouteTo (Ipv4Address (dstNet.c_str ()), Ipv4Mask ("255.255.255.0"), if_dst.GetAddress (1), 2);
        staticReceiver->AddNetworkRouteTo (Ipv4Address (srcNet.c_str ()), Ipv4Mask ("255.255.255.0"), if_dst.GetAddress (0), ifIndex);
      }

    // Dynamic events
    if (!eventsFile.empty ())
      {
        DynamicController::LoadEventsFile (eventsFile, dstDevices, channels);
      }

    if (enableHandover && nPaths > 0)
      {
        // Dynamic link break scenario:
        // Path 0 suffers severe link break between 30% and 60% of duration (e.g. t = 3.0s to 6.0s for 10s):
        // - Bandwidth cut by 99% (to 0.2 Mbps)
        // - Delay spiked by 10x (to 150 ms one-way)
        // - Packet loss spiked to 8%
        // At breakEnd (e.g. t = 6.0s), fully restored to baseline pristine channel.
        Time breakStart = Seconds (m_duration.GetSeconds () * 0.30);
        Time breakEnd = Seconds (m_duration.GetSeconds () * 0.60);
        DataRate degradedRate ("0.2Mbps");
        Time degradedDelay = MilliSeconds (150);
        double degradedLoss = 0.08;

        DynamicController::ScheduleLinkBreak (srcDevices[0], dstDevices[0],
                                              channels.empty () ? nullptr : channels[0],
                                              breakStart, breakEnd,
                                              degradedRate, degradedDelay, degradedLoss,
                                              paths[0].dataRate, paths[0].delay / 2, paths[0].lossRate);
      }

    // Install Sink on Receiver
    Address sinkAddress (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApps = sinkHelper.Install (receiver);
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (m_duration + m_drainTime);

    // Set measurement window strictly ending when sender stops to prevent drain inflation
    col.SetMeasurementWindow (Seconds (0.5), m_duration);

    // Connect sink reception callback to MetricsCollector
    sinkApps.Get (0)->TraceConnectWithoutContext ("Rx", MakeCallback (&MetricsCollector::OnSinkRx, &col));

    // Install BulkSend application on Sender to Primary Subflow address (10.1.2.2)
    Address remoteAddress (InetSocketAddress (receiverIps[0], m_port));
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", remoteAddress);
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
    ApplicationContainer sourceApps = sourceHelper.Install (sender);
    sourceApps.Start (Seconds (0.5));
    sourceApps.Stop (m_duration);
    Simulator::Schedule (Seconds (0.5001), &ModularMpTcpTopology::HookCwndTrace, sourceApps.Get (0), &col);
    Simulator::Schedule (Seconds (0.5002), &ModularMpTcpTopology::SampleCwndPeriodically, sourceApps.Get (0), &col, m_duration);

    // FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (m_duration + m_drainTime);
    Simulator::Run ();

    Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    std::vector<uint64_t> perPathBytes (nPaths, 0);

    for (auto const& iter : stats)
      {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter.first);
        if (t.destinationPort == m_port)
          {
            for (size_t i = 0; i < nPaths; ++i)
              {
                if (t.destinationAddress == receiverIps[i])
                  {
                    perPathBytes[i] += iter.second.rxBytes;
                    Time oneWayProp = paths[i].delay / 2;
                    col.RecordDelaySample (iter.second.delaySum, iter.second.rxPackets, oneWayProp);
                    break;
                  }
              }
          }
      }

    if (!timeSeriesPath.empty ())
      {
        col.ExportTimeSeriesCsv (timeSeriesPath);
      }

    if (DecisionLogger::Get ().IsEnabled ())
      {
        DecisionLogger::Get ().Close ();
      }

    FullExperimentMetrics fm = col.ComputeMetrics (schedulerType, topologyName, totalCapMbps, m_duration, seed, run);

    ExperimentMetrics m;
    m.protocol = fm.protocol;
    m.topology = fm.topology;
    m.seed = fm.seed;
    m.run = fm.run;
    m.totalCapacityMbps = fm.totalCapacityMbps;
    m.simulationDurationSec = fm.flowCompletionTimeSec;
    m.activeThroughputMbps = fm.activeThroughputMbps;
    m.sessionGoodputMbps = fm.sessionGoodputMbps;
    m.packetLossPercent = fm.packetLossPercent;
    m.meanDelayMs = fm.meanDelayMs;
    m.holDelayMs = fm.holDelayMs;
    m.jitterMs = fm.jitterMs;
    m.aggregationEfficiencyPercent = fm.aggregationEfficiencyPercent;
    m.totalBytesReceived = fm.totalBytesReceived;
    m.totalPayloadBytes = fm.totalPayloadBytes;
    m.totalPacketsSent = fm.totalPacketsSent;
    m.totalPacketsLost = fm.totalPacketsLost;
    m.totalRetransmissions = fm.totalRetransmissions;
    m.flowCompletionTimeSec = fm.flowCompletionTimeSec;
    m.path0Bytes = perPathBytes.size () > 0 ? perPathBytes[0] : 0;
    m.path1Bytes = perPathBytes.size () > 1 ? perPathBytes[1] : 0;
    m.path2Bytes = perPathBytes.size () > 2 ? perPathBytes[2] : 0;
    m.perPathBytes = perPathBytes;

    // Simulator::Destroy () is omitted as main() terminates via _Exit(0)
    // preventing tear-down double-frees on dynamically reconfigured NetDevices.
    return m;
  }

  /**
   * \brief Presets calling generic Run() for backward compatibility
   */
  ExperimentMetrics RunDualPath (std::string schedulerType,
                                std::string topologyName,
                                LinkProfile path0,
                                LinkProfile path1,
                                bool enableHandover = false,
                                uint32_t seed = 1,
                                uint32_t run = 1,
                                const std::string& eventsFile = "",
                                uint64_t maxBytes = 0,
                                const std::string& logDecisionsPath = "",
                                const std::string& timeSeriesPath = "")
  {
    std::vector<LinkProfile> paths = {path0, path1};
    return Run (schedulerType, topologyName, paths, eventsFile, enableHandover,
                maxBytes, seed, run, logDecisionsPath, timeSeriesPath);
  }

  ExperimentMetrics RunTriPath (std::string schedulerType,
                                LinkProfile path0,
                                LinkProfile path1,
                                LinkProfile path2,
                                uint32_t seed = 1,
                                uint32_t run = 1,
                                const std::string& eventsFile = "",
                                uint64_t maxBytes = 0,
                                const std::string& logDecisionsPath = "",
                                const std::string& timeSeriesPath = "")
  {
    std::vector<LinkProfile> paths = {path0, path1, path2};
    return Run (schedulerType, "TriPath", paths, eventsFile, false,
                maxBytes, seed, run, logDecisionsPath, timeSeriesPath);
  }

private:
  Time m_duration;
  Time m_drainTime;
  uint16_t m_port;
  bool m_isMptcp;
  uint32_t m_rcvBufSize;
  uint32_t m_sndBufSize;
};

} // namespace ns3

#endif /* MODULAR_MPTCP_TOPOLOGY_H */
