#ifndef MODULAR_MPTCP_TOPOLOGY_H
#define MODULAR_MPTCP_TOPOLOGY_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/error-model.h"
#include "ns3/mptcp-scheduler-blest.h"
#include "ns3/mptcp-scheduler-ecf.h"
#include "link_profile.h"
#include "dynamic_controller.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

namespace ns3 {

struct TopologyResult
{
  std::string scheduler;
  std::string profileName;
  std::string scenarioName;
  uint64_t totalRxBytes;
  uint64_t totalTxBytes;
  uint32_t totalTxPackets;
  uint32_t totalRxPackets;
  uint32_t lostPackets;
  uint32_t retransmissions;
  double durationSeconds;
  double activeTransferSec;
  double throughputMbps;
  double goodputMbps;
  double avgLatencyMs;
  double packetLossRate;
};

class ModularMptcpTopology
{
public:
  ModularMptcpTopology (std::vector<PathConfig> pathConfigs,
                        std::string scheduler = "FastestRTT",
                        DynamicScenarioType dynamicScenario = SCENARIO_STATIC,
                        bool enableMptcp = true)
    : m_paths (pathConfigs),
      m_schedulerType (scheduler),
      m_dynamicScenario (dynamicScenario),
      m_enableMptcp (enableMptcp),
      m_simDuration (6.0),
      m_appStartTime (0.5),
      m_appStopTime (5.5),
      m_transferSize (5000000) // 5 MB
  {
  }

  void SetSimulationDuration (double duration) { m_simDuration = duration; }
  void SetAppTime (double start, double stop) { m_appStartTime = start; m_appStopTime = stop; }
  void SetTransferSize (uint32_t bytes) { m_transferSize = bytes; }

  TopologyResult Run ()
  {
    // Enable or Disable MPTCP
    Config::SetDefault ("ns3::TcpSocketBase::EnableMpTcp", BooleanValue (m_enableMptcp));
    Config::SetDefault ("ns3::MpTcpSocketBase::PathManagerMode", EnumValue (MpTcpSocketBase::FullMesh));

    if (m_enableMptcp)
      {
        if (m_schedulerType == "FastestRTT")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
          }
        else if (m_schedulerType == "RoundRobin")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerRoundRobin::GetTypeId ()));
          }
        else if (m_schedulerType == "BLEST")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerBlest::GetTypeId ()));
          }
        else if (m_schedulerType == "ECF")
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerEcf::GetTypeId ()));
          }
        else
          {
            Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
          }
      }

    // Nodes: Node 0 = Client Sender, Node 1 = Server Receiver
    NodeContainer endHosts;
    endHosts.Create (2);

    InternetStackHelper internet;
    internet.Install (endHosts);

    uint32_t numPaths = m_paths.size ();
    std::vector<NetDeviceContainer> devices;
    Ipv4AddressHelper ipv4;

    Ipv4Address sinkAddress;

    // Build independent paths
    for (uint32_t i = 0; i < numPaths; ++i)
      {
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute ("DataRate", StringValue (m_paths[i].bandwidth));
        p2p.SetChannelAttribute ("Delay", StringValue (m_paths[i].delay));
        p2p.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
                      QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, m_paths[i].queueSizePackets)));

        NetDeviceContainer dev = p2p.Install (endHosts.Get (0), endHosts.Get (1));
        devices.push_back (dev);

        // Apply error model if loss > 0
        if (m_paths[i].lossRate > 0.0)
          {
            Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
            em->SetAttribute ("ErrorRate", DoubleValue (m_paths[i].lossRate));
            em->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET));
            dev.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
          }

        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        ipv4.SetBase (subnet.str ().c_str (), "255.255.255.0");
        Ipv4InterfaceContainer ifCont = ipv4.Assign (dev);

        if (i == 0)
          {
            sinkAddress = ifCont.GetAddress (1);
          }
      }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    // Install Sink on Server (Node 1)
    uint16_t port = 8080;
    PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer sinkApp = sinkHelper.Install (endHosts.Get (1));
    sinkApp.Start (Seconds (0.0));
    sinkApp.Stop (Seconds (m_simDuration));

    // Install BulkSend Application on Client (Node 0)
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", InetSocketAddress (sinkAddress, port));
    if (m_transferSize > 0)
      {
        sourceHelper.SetAttribute ("MaxBytes", UintegerValue (m_transferSize));
      }
    ApplicationContainer sourceApp = sourceHelper.Install (endHosts.Get (0));
    sourceApp.Start (Seconds (m_appStartTime));
    sourceApp.Stop (Seconds (m_appStopTime));

    // Schedule dynamic events if enabled
    if (m_dynamicScenario != SCENARIO_STATIC && devices.size () >= 2)
      {
        DynamicController::ScheduleScenario (m_dynamicScenario, devices[0], devices[1], 2.0);
      }

    // Install FlowMonitor to measure real packet transmission, lost packets, and latency
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (Seconds (m_simDuration));
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    uint64_t totalTxBytes = 0;
    uint64_t totalRxBytes = 0;
    uint32_t totalTxPackets = 0;
    uint32_t totalRxPackets = 0;
    uint32_t totalLostPackets = 0;
    double totalDelaySec = 0.0;
    uint64_t rxDelayCount = 0;
    Time firstRxTime = Seconds (999999.0);
    Time lastRxTime = Seconds (0.0);

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator it = stats.begin (); it != stats.end (); ++it)
      {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (it->first);
        if (t.destinationPort == port)
          {
            totalTxBytes += it->second.txBytes;
            totalRxBytes += it->second.rxBytes;
            totalTxPackets += it->second.txPackets;
            totalRxPackets += it->second.rxPackets;
            totalLostPackets += it->second.lostPackets;
            totalDelaySec += it->second.delaySum.GetSeconds ();
            rxDelayCount += it->second.rxPackets;

            if (it->second.rxPackets > 0)
              {
                if (it->second.timeFirstRxPacket < firstRxTime)
                  {
                    firstRxTime = it->second.timeFirstRxPacket;
                  }
                if (it->second.timeLastRxPacket > lastRxTime)
                  {
                    lastRxTime = it->second.timeLastRxPacket;
                  }
              }
          }
      }

    Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (0));
    uint64_t sinkRxBytes = sink ? sink->GetTotalRx () : 0;
    double activeTime = m_appStopTime - m_appStartTime;
    double transferDuration = activeTime;
    if (lastRxTime > firstRxTime)
      {
        transferDuration = (lastRxTime - firstRxTime).GetSeconds ();
      }
    if (transferDuration <= 0.001)
      {
        transferDuration = activeTime;
      }

    double throughput = (totalRxBytes > 0) ? (totalRxBytes * 8.0) / (transferDuration * 1e6) : 0.0;
    double goodput = (sinkRxBytes * 8.0) / (activeTime * 1e6);

    double measuredRttMs = 0.0;
    if (rxDelayCount > 0)
      {
        // 2-way RTT estimate from one-way packet latency + queueing
        measuredRttMs = (totalDelaySec / (double)rxDelayCount) * 1000.0 * 2.0;
      }

    double lossRate = (totalTxPackets > 0) ? (totalLostPackets * 100.0 / (double)totalTxPackets) : 0.0;

    Simulator::Destroy ();

    TopologyResult res;
    res.scheduler = m_enableMptcp ? m_schedulerType : "Standard-TCP";
    res.totalRxBytes = sinkRxBytes;
    res.totalTxBytes = totalTxBytes;
    res.totalTxPackets = totalTxPackets;
    res.totalRxPackets = totalRxPackets;
    res.lostPackets = totalLostPackets;
    res.retransmissions = totalLostPackets;
    res.durationSeconds = activeTime;
    res.activeTransferSec = transferDuration;
    res.throughputMbps = throughput;
    res.goodputMbps = goodput;
    res.avgLatencyMs = measuredRttMs;
    res.packetLossRate = lossRate;
    return res;
  }

private:
  std::vector<PathConfig> m_paths;
  std::string m_schedulerType;
  DynamicScenarioType m_dynamicScenario;
  bool m_enableMptcp;
  double m_simDuration;
  double m_appStartTime;
  double m_appStopTime;
  uint32_t m_transferSize;
};

} // namespace ns3

#endif // MODULAR_MPTCP_TOPOLOGY_H
