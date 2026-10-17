#ifndef MODULAR_MPTCP_TOPOLOGY_H
#define MODULAR_MPTCP_TOPOLOGY_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
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
  double durationSeconds;
  double goodputMbps;
};

class ModularMptcpTopology
{
public:
  ModularMptcpTopology (std::vector<PathConfig> pathConfigs,
                        std::string scheduler = "FastestRTT",
                        DynamicScenarioType dynamicScenario = SCENARIO_STATIC)
    : m_paths (pathConfigs),
      m_schedulerType (scheduler),
      m_dynamicScenario (dynamicScenario),
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
    // Enable MPTCP
    Config::SetDefault ("ns3::TcpSocketBase::EnableMpTcp", BooleanValue (true));
    Config::SetDefault ("ns3::MpTcpSocketBase::PathManagerMode", EnumValue (MpTcpSocketBase::FullMesh));

    if (m_schedulerType == "FastestRTT")
      {
        Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
      }
    else if (m_schedulerType == "RoundRobin")
      {
        Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerRoundRobin::GetTypeId ()));
      }
    else
      {
        Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
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

    Simulator::Stop (Seconds (m_simDuration));
    Simulator::Run ();

    Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (0));
    uint64_t totalBytes = sink ? sink->GetTotalRx () : 0;
    double activeTime = m_appStopTime - m_appStartTime;
    double goodput = (totalBytes * 8.0) / (activeTime * 1e6);

    Simulator::Destroy ();

    TopologyResult res;
    res.scheduler = m_schedulerType;
    res.totalRxBytes = totalBytes;
    res.durationSeconds = activeTime;
    res.goodputMbps = goodput;
    return res;
  }

private:
  std::vector<PathConfig> m_paths;
  std::string m_schedulerType;
  DynamicScenarioType m_dynamicScenario;
  double m_simDuration;
  double m_appStartTime;
  double m_appStopTime;
  uint32_t m_transferSize;
};

} // namespace ns3

#endif // MODULAR_MPTCP_TOPOLOGY_H
