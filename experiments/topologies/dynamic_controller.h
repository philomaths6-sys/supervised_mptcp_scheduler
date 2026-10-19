/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef DYNAMIC_CONTROLLER_H
#define DYNAMIC_CONTROLLER_H

#include "ns3/core-module.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/error-model.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace ns3 {

/**
 * \brief DynamicController schedules dynamic channel variations:
 * - Runtime delay changes (PointToPointChannel Delay attribute)
 * - Runtime bandwidth changes (PointToPointNetDevice DataRate attribute)
 * - Runtime packet error rate changes (ReceiveErrorModel)
 * - Loading dynamic event scripts from CSV files (--events=events.csv)
 */
class DynamicController
{
public:
  static void ApplyRateChange (Ptr<NetDevice> dev, DataRate newRate)
  {
    Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice> (dev);
    if (p2p)
      {
        p2p->SetDataRate (newRate);
        NS_LOG_INFO ("DynamicController: Changed DataRate to " << newRate);
      }
  }

  static void ApplyDelayChange (Ptr<PointToPointChannel> chan, Time newDelay)
  {
    if (chan)
      {
        chan->SetAttribute ("Delay", TimeValue (newDelay));
        NS_LOG_INFO ("DynamicController: Changed Delay to " << newDelay);
      }
  }

  static void ApplyErrorRate (Ptr<NetDevice> dev, double errorRate)
  {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorRate", DoubleValue (errorRate));
    em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
    dev->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    NS_LOG_INFO ("DynamicController: Applied error rate " << errorRate);
  }

  static void ScheduleDynamicHandover (Ptr<NetDevice> dev, Time eventTime, DataRate degradedRate, Time restoreTime, DataRate restoredRate)
  {
    Simulator::Schedule (eventTime, &DynamicController::ApplyRateChange, dev, degradedRate);
    Simulator::Schedule (restoreTime, &DynamicController::ApplyRateChange, dev, restoredRate);
  }

  static void ScheduleFadingSpike (Ptr<NetDevice> dev, Time eventTime, double lossRate, Time restoreTime, double originalLossRate = 0.0)
  {
    Simulator::Schedule (eventTime, &DynamicController::ApplyErrorRate, dev, lossRate);
    Simulator::Schedule (restoreTime, &DynamicController::ApplyErrorRate, dev, originalLossRate);
  }

  static void ScheduleLinkBreak (Ptr<NetDevice> srcDev,
                                 Ptr<NetDevice> dstDev,
                                 Ptr<PointToPointChannel> chan,
                                 Time breakStart,
                                 Time breakEnd,
                                 DataRate degradedRate,
                                 Time degradedDelay,
                                 double degradedLoss,
                                 DataRate originalRate,
                                 Time originalDelay,
                                 double originalLoss)
  {
    if (srcDev)
      Simulator::Schedule (breakStart, &DynamicController::ApplyRateChange, srcDev, degradedRate);
    if (chan)
      Simulator::Schedule (breakStart, &DynamicController::ApplyDelayChange, chan, degradedDelay);
    if (dstDev)
      Simulator::Schedule (breakStart, &DynamicController::ApplyErrorRate, dstDev, degradedLoss);

    if (srcDev)
      Simulator::Schedule (breakEnd, &DynamicController::ApplyRateChange, srcDev, originalRate);
    if (chan)
      Simulator::Schedule (breakEnd, &DynamicController::ApplyDelayChange, chan, originalDelay);
    if (dstDev)
      Simulator::Schedule (breakEnd, &DynamicController::ApplyErrorRate, dstDev, originalLoss);
  }

  /**
   * \brief Load and schedule dynamic network events from CSV file
   * Schema: time_s,path,rate_mbps,delay_ms,loss
   */
  static bool LoadEventsFile (const std::string& filename,
                             const std::vector<Ptr<NetDevice>>& devices,
                             const std::vector<Ptr<PointToPointChannel>>& channels)
  {
    if (filename.empty ()) return true;

    std::ifstream file (filename);
    if (!file.is_open ())
      {
        NS_LOG_WARN ("DynamicController: Could not open events file " << filename);
        return false;
      }

    std::string line;
    // Skip header line
    if (std::getline (file, line))
      {
        // header read
      }

    while (std::getline (file, line))
      {
        if (line.empty () || line[0] == '#') continue;

        std::stringstream ss (line);
        std::string timeStr, pathStr, rateStr, delayStr, lossStr;

        std::getline (ss, timeStr, ',');
        std::getline (ss, pathStr, ',');
        std::getline (ss, rateStr, ',');
        std::getline (ss, delayStr, ',');
        std::getline (ss, lossStr, ',');

        auto trim = [] (std::string& s) {
          while (!s.empty () && (s.back () == '\r' || s.back () == '\n' || s.back () == ' ' || s.back () == '\t'))
            s.pop_back ();
          while (!s.empty () && (s.front () == ' ' || s.front () == '\t'))
            s.erase (s.begin ());
        };
        trim (timeStr); trim (pathStr); trim (rateStr); trim (delayStr); trim (lossStr);

        if (timeStr.empty () || pathStr.empty ()) continue;

        try
          {
            double tSec = std::stod (timeStr);
            size_t pathIdx = std::stoul (pathStr);

            if (pathIdx >= devices.size ()) continue;

            Time eventTime = Seconds (tSec);

            if (!rateStr.empty ())
              {
                double mbps = std::stod (rateStr);
                DataRate rate (std::to_string (mbps) + "Mbps");
                Simulator::Schedule (eventTime, &DynamicController::ApplyRateChange, devices[pathIdx], rate);
              }

            if (!delayStr.empty () && pathIdx < channels.size ())
              {
                double ms = std::stod (delayStr);
                Time delay = MilliSeconds (ms);
                Simulator::Schedule (eventTime, &DynamicController::ApplyDelayChange, channels[pathIdx], delay);
              }

            if (!lossStr.empty ())
              {
                double loss = std::stod (lossStr);
                Simulator::Schedule (eventTime, &DynamicController::ApplyErrorRate, devices[pathIdx], loss);
              }
          }
        catch (const std::exception& e)
          {
            // Skip malformed rows gracefully
          }
      }

    return true;
  }
};

} // namespace ns3

#endif /* DYNAMIC_CONTROLLER_H */
