/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef LINK_PROFILE_H
#define LINK_PROFILE_H

#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include <string>
#include <algorithm>

namespace ns3 {

/**
 * \brief LinkProfile represents a network interface path configuration (Wi-Fi, LTE, Ethernet, 5G)
 * with calibrated Bandwidth-Delay Product (BDP) queue sizing as documented in research literature
 * (Hurtig et al. BLEST 2016, Lim et al. ECF 2017, Paasch et al. 2014).
 */
struct LinkProfile
{
  std::string name;
  DataRate dataRate;
  Time delay;
  double lossRate;
  uint32_t queueSizePackets;

  LinkProfile ()
    : name ("Default"),
      dataRate ("10Mbps"),
      delay (MilliSeconds (20)),
      lossRate (0.0),
      queueSizePackets (64)
  {}

  LinkProfile (std::string n, std::string rate, Time d, double loss = 0.0, uint32_t qSize = 0)
    : name (n),
      dataRate (rate),
      delay (d),
      lossRate (loss)
  {
    if (qSize > 0)
      {
        queueSizePackets = qSize;
      }
    else
      {
        // Calibrate BDP = (Rate * RTT) / (MSS * 8) with 1.5x multiplier to allow full CWND growth
        // without inducing chronic bufferbloat.
        double rateBps = DataRate (rate).GetBitRate ();
        double rttSec = (d * 2).GetSeconds ();
        double bdpPackets = (rateBps * rttSec) / (1440.0 * 8.0);
        queueSizePackets = std::max (20u, static_cast<uint32_t> (bdpPackets * 1.5));
      }
  }

  static LinkProfile CreateEthernet (std::string rate = "25Mbps", Time delay = MilliSeconds (5), double loss = 0.0001)
  {
    return LinkProfile ("Ethernet", rate, delay, loss);
  }

  static LinkProfile CreateWiFi (std::string rate = "15Mbps", Time delay = MilliSeconds (15), double loss = 0.005)
  {
    return LinkProfile ("WiFi", rate, delay, loss);
  }

  static LinkProfile CreateCellularLTE (std::string rate = "6Mbps", Time delay = MilliSeconds (45), double loss = 0.015)
  {
    return LinkProfile ("CellularLTE", rate, delay, loss);
  }

  static LinkProfile CreateCellular5G (std::string rate = "10Mbps", Time delay = MilliSeconds (50), double loss = 0.015)
  {
    return LinkProfile ("Cellular5G", rate, delay, loss);
  }
};

} // namespace ns3

#endif /* LINK_PROFILE_H */
