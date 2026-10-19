/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef METRICS_COLLECTOR_H
#define METRICS_COLLECTOR_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include <vector>
#include <deque>
#include <map>
#include <fstream>
#include <cmath>

namespace ns3 {

/**
 * \brief Metrics summary for an MPTCP experiment run
 */
struct FullExperimentMetrics
{
  std::string protocol;
  std::string topology;
  uint32_t seed;
  uint32_t run;
  double totalCapacityMbps;
  double activeThroughputMbps;
  double sessionGoodputMbps;
  double packetLossPercent;
  double meanDelayMs;
  double holDelayMs;
  double jitterMs;
  double aggregationEfficiencyPercent;
  uint64_t totalBytesReceived;
  uint64_t totalPayloadBytes;
  uint32_t totalPacketsSent;
  uint32_t totalPacketsLost;
  uint32_t totalRetransmissions;
  std::vector<uint64_t> perPathBytes;
  double flowCompletionTimeSec;
};

/**
 * \brief Time-series bin entry (100 ms resolution)
 */
struct TimeSeriesBin
{
  double timeSec;
  double throughputMbps;
  double goodputMbps;
  double holDelayMs;
  uint32_t lostPkts;
  double cwndKb;
};

/**
 * \brief MetricsCollector owns all performance measurements:
 * - Application layer goodput and flow completion time
 * - Wire-level throughput and subflow byte distribution
 * - Physical and queue packet drops
 * - RFC 3550 inter-arrival jitter
 * - HoL blocking delay proxy
 * - 100 ms interval time series export
 * Enforces a strict measurement window [windowStart, windowStop] to prevent drain-period over-reporting.
 */
class MetricsCollector
{
public:
  MetricsCollector ()
    : m_windowStart (Seconds (0.5)),
      m_windowStop (Seconds (10.0)),
      m_firstRxTime (Seconds (0.0)),
      m_lastRxTime (Seconds (0.0)),
      m_totalPayloadBytesRx (0),
      m_totalWireBytesRx (0),
      m_totalPacketsSent (0),
      m_phyRxDrops (0),
      m_queueDrops (0),
      m_totalDelaySumSec (0.0),
      m_delaySamplesCount (0),
      m_totalRxPackets (0),
      m_lastJitterSec (0.0),
      m_totalHolDelaySumSec (0.0),
      m_holSamplesCount (0),
      m_lastBinTime (Seconds (0.0)),
      m_binWireBytes (0),
      m_binPayloadBytes (0),
      m_binLossCount (0),
      m_currentCwndBytes (1440 * 10)
  {}

  void SetMeasurementWindow (Time start, Time stop)
  {
    m_windowStart = start;
    m_windowStop = stop;
  }

  Time GetWindowStop () const
  {
    return m_windowStop;
  }

  void OnCwndChange (uint32_t oldCwnd, uint32_t newCwnd)
  {
    m_currentCwndBytes = newCwnd;
  }

  void SetCwndBytes (uint32_t cwndBytes)
  {
    m_currentCwndBytes = cwndBytes;
  }

  void Reset ()
  {
    m_firstRxTime = Seconds (0.0);
    m_lastRxTime = Seconds (0.0);
    m_totalPayloadBytesRx = 0;
    m_totalWireBytesRx = 0;
    m_totalPacketsSent = 0;
    m_phyRxDrops = 0;
    m_queueDrops = 0;
    m_totalDelaySumSec = 0.0;
    m_delaySamplesCount = 0;
    m_totalRxPackets = 0;
    m_lastJitterSec = 0.0;
    m_totalHolDelaySumSec = 0.0;
    m_holSamplesCount = 0;
    m_hasPrevRxTime = false;
    m_perPathRxBytes.clear ();
    m_timeSeries.clear ();
    m_lastBinTime = Seconds (0.0);
    m_binWireBytes = 0;
    m_binPayloadBytes = 0;
    m_binLossCount = 0;
    m_currentCwndBytes = 1440 * 10;
  }

  void OnSinkRx (Ptr<const Packet> packet, const Address &from)
  {
    Time now = Simulator::Now ();
    // Exclude packets received before start or during post-stop drain period
    if (now < m_windowStart || now > m_windowStop)
      {
        return;
      }

    if (m_firstRxTime.GetSeconds () == 0.0)
      {
        m_firstRxTime = now;
        m_lastBinTime = now;
      }
    m_lastRxTime = now;

    uint32_t payloadBytes = packet->GetSize ();
    uint32_t wireBytes = payloadBytes + 54; // TCP (34) + IPv4 (20) headers
    m_totalPayloadBytesRx += payloadBytes;
    m_totalWireBytesRx += wireBytes;
    m_binPayloadBytes += payloadBytes;
    m_binWireBytes += wireBytes;
    m_totalRxPackets++;

    // Identify arrival path from sender subnet 10.X.1.1
    if (InetSocketAddress::IsMatchingType (from))
      {
        Ipv4Address senderIp = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
        uint32_t subnetX = (senderIp.Get () >> 16) & 0xff;
        if (subnetX >= 1)
          {
            uint32_t pathId = subnetX - 1;
            if (m_perPathRxBytes.size () <= pathId)
              {
                m_perPathRxBytes.resize (pathId + 1, 0);
              }
            m_perPathRxBytes[pathId] += wireBytes;
          }
      }

    // RFC 3550 Inter-arrival jitter update
    if (m_hasPrevRxTime)
      {
        double d = std::abs ((now - m_prevRxTime).GetSeconds ());
        m_lastJitterSec += (d - m_lastJitterSec) / 16.0;
      }
    m_prevRxTime = now;
    m_hasPrevRxTime = true;
  }

  void OnPhyRxDrop (Ptr<const Packet> p)
  {
    Time now = Simulator::Now ();
    if (now >= m_windowStart && now <= m_windowStop)
      {
        m_phyRxDrops++;
        m_binLossCount++;
      }
  }

  void OnQueueDrop (Ptr<const Packet> p)
  {
    Time now = Simulator::Now ();
    if (now >= m_windowStart && now <= m_windowStop)
      {
        m_queueDrops++;
        m_binLossCount++;
      }
  }

  void OnPhyTx (Ptr<const Packet> p)
  {
    Time now = Simulator::Now ();
    if (now >= m_windowStart && now <= m_windowStop)
      {
        m_totalPacketsSent++;
      }
  }

  void RecordDelaySample (Time delaySum, uint32_t rxPackets, Time oneWayPropDelay)
  {
    m_totalDelaySumSec += delaySum.GetSeconds ();
    m_delaySamplesCount += rxPackets;
    if (rxPackets > 0)
      {
        Time avgDelay = delaySum / rxPackets;
        if (avgDelay > oneWayPropDelay)
          {
            m_totalHolDelaySumSec += (avgDelay - oneWayPropDelay).GetSeconds () * rxPackets;
            m_holSamplesCount += rxPackets;
          }
      }
  }

  void UpdateTimeSeriesBin (Time now)
  {
    if (m_lastBinTime.GetSeconds () == 0.0)
      {
        m_lastBinTime = now;
        return;
      }
    if ((now - m_lastBinTime).GetSeconds () >= 0.095)
      {
        double dt = (now - m_lastBinTime).GetSeconds ();
        TimeSeriesBin bin;
        bin.timeSec = now.GetSeconds ();
        bin.throughputMbps = (m_binWireBytes * 8.0) / (dt * 1e6);
        bin.goodputMbps = (m_binPayloadBytes * 8.0) / (dt * 1e6);
        bin.lostPkts = m_binLossCount;
        bin.holDelayMs = (m_holSamplesCount > 0) ? (m_totalHolDelaySumSec / m_holSamplesCount * 1000.0) : 0.0;
        bin.cwndKb = (double)m_currentCwndBytes / 1024.0;

        m_timeSeries.push_back (bin);

        m_lastBinTime = now;
        m_binWireBytes = 0;
        m_binPayloadBytes = 0;
        m_binLossCount = 0;
      }
  }

  void ExportTimeSeriesCsv (const std::string& filename) const
  {
    if (filename.empty () || m_timeSeries.empty ()) return;

    std::ofstream file (filename);
    if (!file.is_open ()) return;

    file << "time_s,throughput_mbps,goodput_mbps,hol_delay_ms,lost_pkts,cwnd_kb\n";
    for (const auto& bin : m_timeSeries)
      {
        file << bin.timeSec << ","
             << bin.throughputMbps << ","
             << bin.goodputMbps << ","
             << bin.holDelayMs << ","
             << bin.lostPkts << ","
             << bin.cwndKb << "\n";
      }
  }

  FullExperimentMetrics ComputeMetrics (std::string protocol,
                                        std::string topology,
                                        double totalCapMbps,
                                        Time duration,
                                        uint32_t seed = 1,
                                        uint32_t run = 1)
  {
    double activeSec = std::max (0.1, (m_windowStop - m_windowStart).GetSeconds ());
    // For fixed-size flows that finish early
    if (m_lastRxTime > m_firstRxTime && m_lastRxTime < m_windowStop - MilliSeconds (200))
      {
        activeSec = (m_lastRxTime - m_firstRxTime).GetSeconds ();
      }

    uint32_t totalLost = m_phyRxDrops + m_queueDrops;
    uint32_t totalSent = std::max (m_totalPacketsSent, m_totalRxPackets + totalLost);

    FullExperimentMetrics m;
    m.protocol = protocol;
    m.topology = topology;
    m.seed = seed;
    m.run = run;
    m.totalCapacityMbps = totalCapMbps;
    m.totalBytesReceived = m_totalWireBytesRx > 0 ? m_totalWireBytesRx : m_totalPayloadBytesRx;
    m.totalPayloadBytes = m_totalPayloadBytesRx;
    m.totalPacketsSent = totalSent;
    m.totalPacketsLost = totalLost;
    m.totalRetransmissions = totalLost;
    m.perPathBytes = m_perPathRxBytes;

    m.activeThroughputMbps = (m.totalBytesReceived * 8.0) / (activeSec * 1e6);
    m.sessionGoodputMbps = (m.totalPayloadBytes * 8.0) / (activeSec * 1e6);
    m.packetLossPercent = (totalSent > 0) ? ((double)totalLost / (double)totalSent * 100.0) : 0.0;
    m.meanDelayMs = (m_delaySamplesCount > 0) ? (m_totalDelaySumSec / m_delaySamplesCount * 1000.0) : 0.0;
    m.holDelayMs = (m_holSamplesCount > 0) ? (m_totalHolDelaySumSec / m_holSamplesCount * 1000.0) : 0.0;
    m.jitterMs = m_lastJitterSec * 1000.0;
    m.aggregationEfficiencyPercent = (totalCapMbps > 0) ? (m.activeThroughputMbps / totalCapMbps * 100.0) : 0.0;
    m.flowCompletionTimeSec = activeSec;

    return m;
  }

  Time m_windowStart;
  Time m_windowStop;
  Time m_firstRxTime;
  Time m_lastRxTime;
  uint64_t m_totalPayloadBytesRx;
  uint64_t m_totalWireBytesRx;
  uint32_t m_totalPacketsSent;
  uint32_t m_phyRxDrops;
  uint32_t m_queueDrops;
  double m_totalDelaySumSec;
  uint32_t m_delaySamplesCount;
  uint32_t m_totalRxPackets;
  double m_lastJitterSec;
  double m_totalHolDelaySumSec;
  uint32_t m_holSamplesCount;
  Time m_prevRxTime {Seconds (0.0)};
  bool m_hasPrevRxTime {false};
  std::vector<uint64_t> m_perPathRxBytes;
  std::vector<TimeSeriesBin> m_timeSeries;
  Time m_lastBinTime;
  uint64_t m_binWireBytes;
  uint64_t m_binPayloadBytes;
  uint32_t m_binLossCount;
  uint32_t m_currentCwndBytes;
};

} // namespace ns3

#endif /* METRICS_COLLECTOR_H */
