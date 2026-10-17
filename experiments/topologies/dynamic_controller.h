#ifndef DYNAMIC_CONTROLLER_H
#define DYNAMIC_CONTROLLER_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/error-model.h"
#include <iostream>
#include <string>

namespace ns3 {

enum DynamicScenarioType
{
  SCENARIO_STATIC,           // No dynamic changes
  SCENARIO_BUFFERBLOAT,      // Dynamic cross-traffic and latency surge
  SCENARIO_FADING_HANDOVER,  // Wi-Fi signal fading, rate drop, latency spike
  SCENARIO_OUTAGE_RECOVERY   // Sudden link loss and later restoration
};

class DynamicController
{
public:
  static void ApplyRateChange (NetDeviceContainer dev, std::string newRate, std::string pathName)
  {
    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice> (dev.Get (0));
    if (p2pDev)
      {
        p2pDev->SetDataRate (DataRate (newRate));
        std::cout << "[DYNAMIC EVENT] t=" << Simulator::Now ().GetSeconds ()
                  << "s: " << pathName << " throttled to " << newRate << std::endl;
      }
  }

  static void ApplyLossChange (NetDeviceContainer dev, double newLoss, std::string pathName)
  {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorRate", DoubleValue (newLoss));
    em->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET));
    dev.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    std::cout << "[DYNAMIC EVENT] t=" << Simulator::Now ().GetSeconds ()
              << "s: " << pathName << " packet loss rate set to " << (newLoss * 100) << "%" << std::endl;
  }

  static void ScheduleScenario (DynamicScenarioType scenario,
                                NetDeviceContainer primaryDev,
                                NetDeviceContainer secondaryDev,
                                double startTime = 2.0)
  {
    switch (scenario)
      {
      case SCENARIO_FADING_HANDOVER:
        // At t=startTime: Primary (e.g. Wi-Fi) fades from 50Mbps -> 5Mbps, loss jumps to 5%
        Simulator::Schedule (Seconds (startTime),
                             &DynamicController::ApplyRateChange, primaryDev, "5Mbps", "Primary(WiFi)");
        Simulator::Schedule (Seconds (startTime),
                             &DynamicController::ApplyLossChange, primaryDev, 0.05, "Primary(WiFi)");
        // At t=startTime + 1.5s: Primary link temporarily drops
        Simulator::Schedule (Seconds (startTime + 1.5),
                             &DynamicController::ApplyRateChange, primaryDev, "1Kbps", "Primary(WiFi)");
        break;

      case SCENARIO_BUFFERBLOAT:
        // Bottleneck bandwidth drops causing queue congestion
        Simulator::Schedule (Seconds (startTime),
                             &DynamicController::ApplyRateChange, primaryDev, "8Mbps", "Primary(WiFi)");
        Simulator::Schedule (Seconds (startTime + 1.0),
                             &DynamicController::ApplyRateChange, secondaryDev, "5Mbps", "Secondary(Cellular)");
        break;

      case SCENARIO_OUTAGE_RECOVERY:
        // Outage at t=startTime
        Simulator::Schedule (Seconds (startTime),
                             &DynamicController::ApplyRateChange, primaryDev, "1Kbps", "Primary(WiFi)");
        // Recovery at t=startTime + 1.5s
        Simulator::Schedule (Seconds (startTime + 1.5),
                             &DynamicController::ApplyRateChange, primaryDev, "50Mbps", "Primary(WiFi)");
        break;

      case SCENARIO_STATIC:
      default:
        break;
      }
  }
};

} // namespace ns3

#endif // DYNAMIC_CONTROLLER_H
