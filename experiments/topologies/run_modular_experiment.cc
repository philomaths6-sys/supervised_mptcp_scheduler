#include "ns3/core-module.h"
#include "modular_mptcp_topology.h"
#include "link_profile.h"
#include "dynamic_controller.h"

#include <iostream>
#include <fstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RunModularExperiment");

int main (int argc, char *argv[])
{
  std::string profileStr = "wifi_cellular"; // wifi_cellular, ethernet_wifi, triple_path, custom
  std::string dynamicScenarioStr = "none";  // none, bufferbloat, fading_handover, outage
  std::string schedulerStr = "FastestRTT";  // FastestRTT, RoundRobin, BLEST, ECF, SupervisedML
  bool enableMptcp = true;
  double duration = 5.0;
  uint32_t transferBytes = 3000000; // 3 MB
  std::string outputCsv = "";

  // Custom path parameters if profile == "custom" (Canonical 5 Mbps Ethernet + 1 Mbps WiFi)
  std::string p0Bw = "5Mbps", p0Delay = "10ms";
  double p0Loss = 0.0;
  std::string p1Bw = "1Mbps", p1Delay = "30ms";
  double p1Loss = 0.0;

  CommandLine cmd;
  cmd.AddValue ("profile", "Device profile: wifi_cellular, ethernet_wifi, triple_path, custom", profileStr);
  cmd.AddValue ("scenario", "Dynamic scenario: none, bufferbloat, fading_handover, outage", dynamicScenarioStr);
  cmd.AddValue ("scheduler", "Scheduler: FastestRTT, RoundRobin, BLEST, ECF, SupervisedML", schedulerStr);
  cmd.AddValue ("mptcp", "Enable MPTCP (true for MPTCP, false for Standard-TCP)", enableMptcp);
  cmd.AddValue ("duration", "Simulation duration in seconds", duration);
  cmd.AddValue ("transferBytes", "Total transfer bytes (0 for continuous)", transferBytes);
  cmd.AddValue ("outputCsv", "Output CSV filepath", outputCsv);

  cmd.AddValue ("p0Bw", "Path 0 Bandwidth (for custom profile)", p0Bw);
  cmd.AddValue ("p0Delay", "Path 0 Delay (for custom profile)", p0Delay);
  cmd.AddValue ("p0Loss", "Path 0 Loss Rate (for custom profile)", p0Loss);
  cmd.AddValue ("p1Bw", "Path 1 Bandwidth (for custom profile)", p1Bw);
  cmd.AddValue ("p1Delay", "Path 1 Delay (for custom profile)", p1Delay);
  cmd.AddValue ("p1Loss", "Path 1 Loss Rate (for custom profile)", p1Loss);
  cmd.Parse (argc, argv);

  // Select profile
  DeviceProfileType profileType = PROFILE_WIFI_CELLULAR;
  if (profileStr == "ethernet_wifi")
    {
      profileType = PROFILE_ETHERNET_WIFI;
    }
  else if (profileStr == "triple_path")
    {
      profileType = PROFILE_TRIPLE_PATH;
    }
  else if (profileStr == "custom")
    {
      profileType = PROFILE_CUSTOM;
    }

  std::vector<PathConfig> paths;
  if (profileType == PROFILE_CUSTOM)
    {
      paths.push_back (PathConfig ("Path0", p0Bw, p0Delay, p0Loss, 250));
      paths.push_back (PathConfig ("Path1", p1Bw, p1Delay, p1Loss, 150));
    }
  else
    {
      paths = LinkProfileManager::GetProfile (profileType);
    }

  // Select dynamic scenario
  DynamicScenarioType scenarioType = SCENARIO_STATIC;
  if (dynamicScenarioStr == "fading_handover")
    {
      scenarioType = SCENARIO_FADING_HANDOVER;
    }
  else if (dynamicScenarioStr == "bufferbloat")
    {
      scenarioType = SCENARIO_BUFFERBLOAT;
    }
  else if (dynamicScenarioStr == "outage")
    {
      scenarioType = SCENARIO_OUTAGE_RECOVERY;
    }

  ModularMptcpTopology topology (paths, schedulerStr, scenarioType, enableMptcp);
  topology.SetSimulationDuration (duration);
  topology.SetAppTime (0.5, duration - 0.5);
  topology.SetTransferSize (transferBytes);

  TopologyResult res = topology.Run ();
  res.profileName = profileStr;
  res.scenarioName = dynamicScenarioStr;

  std::cout << "\n======================================================\n";
  std::cout << "               MODULAR MPTCP BENCHMARK                \n";
  std::cout << "======================================================\n";
  std::cout << "Profile                 : " << res.profileName << " (" << paths.size () << " subflows)\n";
  for (size_t i = 0; i < paths.size (); ++i)
    {
      std::cout << "  - Subflow " << i << " [" << paths[i].name << "]: "
                << paths[i].bandwidth << ", " << paths[i].delay
                << ", Loss: " << (paths[i].lossRate * 100) << "%\n";
    }
  std::cout << "Dynamic Scenario        : " << res.scenarioName << "\n";
  std::cout << "Protocol / Scheduler    : " << res.scheduler << "\n";
  std::cout << "Duration Session        : " << res.durationSeconds << " s\n";
  std::cout << "Active Transfer Time    : " << res.activeTransferSec << " s\n";
  std::cout << "Total Data Transferred  : " << res.totalRxBytes << " bytes ("
            << (res.totalRxBytes / 1024.0 / 1024.0) << " MB)\n";
  std::cout << "Active Throughput       : " << res.throughputMbps << " Mbps\n";
  std::cout << "Session Goodput         : " << res.goodputMbps << " Mbps\n";
  std::cout << "Measured Avg RTT        : " << res.avgLatencyMs << " ms\n";
  std::cout << "Total Packets Tx / Rx   : " << res.totalTxPackets << " / " << res.totalRxPackets << "\n";
  std::cout << "Retransmissions / Lost  : " << res.retransmissions << " (" << res.packetLossRate << "% loss rate)\n";
  std::cout << "======================================================\n\n";

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
          csvOut << "profile,scenario,scheduler,numPaths,totalRxBytes,durationSec,activeSec,throughputMbps,goodputMbps,latencyMs,txPackets,rxPackets,lostPackets,retransmissions,lossRatePercent\n";
        }
      csvOut << res.profileName << "," << res.scenarioName << ","
             << res.scheduler << "," << paths.size () << ","
             << res.totalRxBytes << "," << res.durationSeconds << ","
             << res.activeTransferSec << ","
             << res.throughputMbps << "," << res.goodputMbps << ","
             << res.avgLatencyMs << ","
             << res.totalTxPackets << "," << res.totalRxPackets << ","
             << res.lostPackets << "," << res.retransmissions << ","
             << res.packetLossRate << "\n";
      csvOut.close ();
    }

  return 0;
}
