#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("HeterogeneousMptcpTopology");

// Helper function for dynamic throttling
static void ChangeDataRate (NetDeviceContainer dev, std::string newRate)
{
  Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice> (dev.Get (0));
  if (p2pDev)
    {
      p2pDev->SetDataRate (DataRate (newRate));
      std::cout << "[DYNAMIC EVENT] At " << Simulator::Now ().GetSeconds ()
                << "s: Path 0 bandwidth throttled to " << newRate << std::endl;
    }
}

int main (int argc, char *argv[])
{
  // Path 0 parameters (e.g. Fast / Low Latency path, e.g., 5G / Ethernet)
  std::string path0Bw = "20Mbps";
  std::string path0Delay = "10ms";
  double path0Loss = 0.0; // loss rate (0.0 to 1.0)
  uint32_t path0QueueDiscSize = 100; // in packets

  // Path 1 parameters (e.g. Slower / High Latency path, e.g., Wi-Fi / LTE / Satellite)
  std::string path1Bw = "10Mbps";
  std::string path1Delay = "50ms";
  double path1Loss = 0.0;
  uint32_t path1QueueDiscSize = 100;

  // Dynamic conditions (Objective iii: evaluate generalization)
  bool enableDynamicChanges = false;
  double dynamicEventTime = 2.0; // seconds
  std::string path0BwPost = "5Mbps"; // sudden drop

  // Simulation and Traffic parameters
  bool enableMptcp = true;
  std::string schedulerType = "FastestRTT"; // FastestRTT, RoundRobin, BLEST, ECF, SupervisedML
  double simDuration = 5.0; // seconds
  double appStartTime = 0.5;
  double appStopTime = 4.5;
  uint32_t transferSizeBytes = 3000000; // 3 MB Bulk transfer (0 for continuous)
  std::string outputCsv = ""; // Optional CSV output path for dataset/metrics

  CommandLine cmd;
  cmd.AddValue ("path0Bw", "Path 0 Bandwidth", path0Bw);
  cmd.AddValue ("path0Delay", "Path 0 Propagation Delay", path0Delay);
  cmd.AddValue ("path0Loss", "Path 0 Packet Loss Rate", path0Loss);
  cmd.AddValue ("path1Bw", "Path 1 Bandwidth", path1Bw);
  cmd.AddValue ("path1Delay", "Path 1 Propagation Delay", path1Delay);
  cmd.AddValue ("path1Loss", "Path 1 Packet Loss Rate", path1Loss);
  cmd.AddValue ("enableDynamic", "Enable dynamic network variation during run", enableDynamicChanges);
  cmd.AddValue ("dynamicTime", "Time at which dynamic changes occur", dynamicEventTime);
  cmd.AddValue ("path0BwPost", "Path 0 Bandwidth after dynamic event", path0BwPost);
  cmd.AddValue ("mptcp", "Enable MPTCP", enableMptcp);
  cmd.AddValue ("scheduler", "Scheduler: FastestRTT, RoundRobin, BLEST, ECF, SupervisedML", schedulerType);
  cmd.AddValue ("simDuration", "Total simulation duration in seconds", simDuration);
  cmd.AddValue ("transferSize", "Transfer size in bytes (0 for continuous flow)", transferSizeBytes);
  cmd.AddValue ("outputCsv", "CSV filename to write metrics", outputCsv);
  cmd.Parse (argc, argv);

  // Configure MPTCP defaults
  Config::SetDefault ("ns3::TcpSocketBase::EnableMpTcp", BooleanValue (enableMptcp));
  Config::SetDefault ("ns3::MpTcpSocketBase::PathManagerMode", EnumValue (MpTcpSocketBase::FullMesh));

  if (schedulerType == "FastestRTT")
    {
      Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
    }
  else if (schedulerType == "RoundRobin")
    {
      Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerRoundRobin::GetTypeId ()));
    }
  else
    {
      Config::SetDefault ("ns3::MpTcpSocketBase::Scheduler", TypeIdValue (MpTcpSchedulerFastestRTT::GetTypeId ()));
    }

  // Node 0: Sender, Node 1: Receiver
  NodeContainer nodes;
  nodes.Create (2);

  // Configure Point-to-Point Path 0
  PointToPointHelper p2p0;
  p2p0.SetDeviceAttribute ("DataRate", StringValue (path0Bw));
  p2p0.SetChannelAttribute ("Delay", StringValue (path0Delay));
  p2p0.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, path0QueueDiscSize)));

  // Configure Point-to-Point Path 1
  PointToPointHelper p2p1;
  p2p1.SetDeviceAttribute ("DataRate", StringValue (path1Bw));
  p2p1.SetChannelAttribute ("Delay", StringValue (path1Delay));
  p2p1.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, path1QueueDiscSize)));

  NetDeviceContainer dev0 = p2p0.Install (nodes.Get (0), nodes.Get (1));
  NetDeviceContainer dev1 = p2p1.Install (nodes.Get (0), nodes.Get (1));

  // Configure Error Rate (Loss) Models if specified
  if (path0Loss > 0.0)
    {
      Ptr<RateErrorModel> em0 = CreateObject<RateErrorModel> ();
      em0->SetAttribute ("ErrorRate", DoubleValue (path0Loss));
      em0->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET));
      dev0.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em0));
    }

  if (path1Loss > 0.0)
    {
      Ptr<RateErrorModel> em1 = CreateObject<RateErrorModel> ();
      em1->SetAttribute ("ErrorRate", DoubleValue (path1Loss));
      em1->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET));
      dev1.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em1));
    }

  // Install Internet Stack
  InternetStackHelper internet;
  internet.Install (nodes);

  // Assign IP addresses to Subflows
  // Subflow 0: 10.1.1.0/24
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if0 = ipv4.Assign (dev0);

  // Subflow 1: 10.1.2.0/24
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if1 = ipv4.Assign (dev1);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Setup Packet Sink on Receiver (Node 1)
  uint16_t port = 8080;
  Address sinkAddress (InetSocketAddress (if0.GetAddress (1), port));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (nodes.Get (1));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simDuration));

  // Setup BulkSend Application on Sender (Node 0)
  BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", sinkAddress);
  if (transferSizeBytes > 0)
    {
      sourceHelper.SetAttribute ("MaxBytes", UintegerValue (transferSizeBytes));
    }
  ApplicationContainer sourceApp = sourceHelper.Install (nodes.Get (0));
  sourceApp.Start (Seconds (appStartTime));
  sourceApp.Stop (Seconds (appStopTime));

  // Schedule Dynamic Variations if enabled
  if (enableDynamicChanges)
    {
      Simulator::Schedule (Seconds (dynamicEventTime), &ChangeDataRate, dev0, path0BwPost);
    }

  Simulator::Stop (Seconds (simDuration));
  Simulator::Run ();

  // Collect performance metrics
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (0));
  uint64_t totalBytesReceived = sink ? sink->GetTotalRx () : 0;
  double activeTime = appStopTime - appStartTime;
  double goodputMbps = (totalBytesReceived * 8.0) / (activeTime * 1e6);

  std::cout << "\n================ SIMULATION RESULTS ================\n";
  std::cout << "Scheduler               : " << schedulerType << "\n";
  std::cout << "Path 0 Config           : " << path0Bw << ", " << path0Delay << ", Loss: " << (path0Loss * 100) << "%\n";
  std::cout << "Path 1 Config           : " << path1Bw << ", " << path1Delay << ", Loss: " << (path1Loss * 100) << "%\n";
  std::cout << "Total Bytes Received    : " << totalBytesReceived << " bytes\n";
  std::cout << "Effective Goodput       : " << goodputMbps << " Mbps\n";
  std::cout << "====================================================\n" << std::endl;

  // Optionally output to CSV
  if (!outputCsv.empty ())
    {
      bool writeHeader = false;
      std::ifstream checkFile (outputCsv.c_str ());
      if (!checkFile.good ())
        {
          writeHeader = true;
        }
      checkFile.close ();

      std::ofstream csvOut (outputCsv.c_str (), std::ios::app);
      if (writeHeader)
        {
          csvOut << "scheduler,path0Bw,path0Delay,path0Loss,path1Bw,path1Delay,path1Loss,totalBytes,goodputMbps\n";
        }
      csvOut << schedulerType << ","
             << path0Bw << "," << path0Delay << "," << path0Loss << ","
             << path1Bw << "," << path1Delay << "," << path1Loss << ","
             << totalBytesReceived << "," << goodputMbps << "\n";
      csvOut.close ();
    }

  Simulator::Destroy ();
  return 0;
}
