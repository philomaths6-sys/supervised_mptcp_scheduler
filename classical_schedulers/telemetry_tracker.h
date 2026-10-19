/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef TELEMETRY_TRACKER_H
#define TELEMETRY_TRACKER_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mptcp-subflow.h"
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>

namespace ns3 {

/**
 * \brief Subflow telemetry state
 */
struct SubflowTelemetry
{
  int subflowId;
  Time rtt;
  Time minRtt;
  bool minRttSampled;
  uint32_t cwnd;
  uint32_t bytesInFlight;
  uint32_t availableWindow;
  uint32_t segSize;
  double bandwidthBps;
  double deliveryRateBps;
  uint32_t txPackets;
  uint32_t retransmissions;
  double lossRate;

  // Internal state for delivery rate estimation
  SequenceNumber32 lastAck;
  Time lastAckTime;
  uint32_t recentTx;
  uint32_t recentLoss;
  Time lastLossWindow;

  SubflowTelemetry ()
    : subflowId (0),
      rtt (MilliSeconds (20)),
      minRtt (MilliSeconds (20)),
      minRttSampled (false),
      cwnd (1440 * 10),
      bytesInFlight (0),
      availableWindow (1440 * 10),
      segSize (1440),
      bandwidthBps (1250000.0),
      deliveryRateBps (0.0),
      txPackets (0),
      retransmissions (0),
      lossRate (0.0),
      lastAck (0),
      lastAckTime (Seconds (0.0)),
      recentTx (0),
      recentLoss (0),
      lastLossWindow (Seconds (0.0))
  {}
};

/**
 * \brief TelemetryTracker
 * Tracks real-time transport telemetry using subflow accessor methods and trace callbacks.
 */
class TelemetryTracker
{
public:
  static TelemetryTracker& Get ()
  {
    static TelemetryTracker instance;
    return instance;
  }

  void Reset ()
  {
    m_subflows.clear ();
    m_registered.clear ();
  }

  static void OnRttTrace (int id, Time oldRtt, Time newRtt)
  {
    TelemetryTracker::Get ().UpdateRtt (id, newRtt);
  }

  static void OnCwndTrace (int id, uint32_t oldCwnd, uint32_t newCwnd)
  {
    TelemetryTracker::Get ().UpdateCwnd (id, newCwnd);
  }

  static void OnAckTrace (int id, SequenceNumber32 oldSeq, SequenceNumber32 newSeq)
  {
    TelemetryTracker::Get ().UpdateAck (id, oldSeq, newSeq);
  }

  static void OnCongStateTrace (int id, TcpSocketState::TcpCongState_t oldState, TcpSocketState::TcpCongState_t newState)
  {
    if (newState == TcpSocketState::CA_RECOVERY || newState == TcpSocketState::CA_LOSS)
      {
        TelemetryTracker::Get ().RecordLoss (id);
      }
  }

  void UpdateRtt (int id, Time newRtt)
  {
    if (newRtt.GetMilliSeconds () > 0)
      {
        SubflowTelemetry& t = m_subflows[id];
        t.rtt = newRtt;
        if (!t.minRttSampled || newRtt < t.minRtt)
          {
            t.minRtt = newRtt;
            t.minRttSampled = true;
          }
        if (t.deliveryRateBps <= 0.0 && t.rtt.GetSeconds () > 0.0001 && t.cwnd > 0)
          {
            t.bandwidthBps = (double)t.cwnd * 8.0 / t.rtt.GetSeconds ();
          }
      }
  }

  void UpdateCwnd (int id, uint32_t newCwnd)
  {
    SubflowTelemetry& t = m_subflows[id];
    t.cwnd = newCwnd;
    if (t.deliveryRateBps <= 0.0 && t.rtt.GetSeconds () > 0.0001 && t.cwnd > 0)
      {
        t.bandwidthBps = (double)t.cwnd * 8.0 / t.rtt.GetSeconds ();
      }
  }

  void UpdateAck (int id, SequenceNumber32 oldSeq, SequenceNumber32 newSeq)
  {
    if (newSeq > oldSeq)
      {
        Time now = Simulator::Now ();
        SubflowTelemetry& t = m_subflows[id];
        uint32_t ackedBytes = (uint32_t)(newSeq - oldSeq);

        if (t.lastAckTime.GetSeconds () > 0.0)
          {
            double dt = (now - t.lastAckTime).GetSeconds ();
            if (dt > 0.001) // at least 1 ms to prevent noise
              {
                double instRate = (ackedBytes * 8.0) / dt;
                // EWMA smoothing over ~100 ms window
                double alpha = std::min (1.0, dt / 0.100);
                if (t.deliveryRateBps <= 0.0)
                  {
                    t.deliveryRateBps = instRate;
                  }
                else
                  {
                    t.deliveryRateBps = (1.0 - alpha) * t.deliveryRateBps + alpha * instRate;
                  }
                t.bandwidthBps = std::max (t.deliveryRateBps, (double)t.cwnd * 8.0 / std::max (0.001, t.rtt.GetSeconds ()));
              }
          }
        t.lastAck = newSeq;
        t.lastAckTime = now;
      }
  }

  void RegisterSubflow (int id, Ptr<MpTcpSubflow> sf, double linkRateBps = 0.0)
  {
    SubflowTelemetry& t = m_subflows[id];
    t.subflowId = id;
    t.segSize = sf->GetSegSize ();
    if (t.segSize == 0) t.segSize = 1440;
    
    Time rttEst = sf->GetRttEstimate ();
    if (rttEst.GetMilliSeconds () > 0)
      {
        t.rtt = rttEst;
        t.minRtt = sf->GetMinRtt ();
        t.minRttSampled = true;
      }
    t.cwnd = sf->GetCwnd ();
    t.bytesInFlight = sf->BytesInFlight ();
    t.availableWindow = sf->AvailableWindow ();

    if (linkRateBps > 0.0)
      {
        t.bandwidthBps = linkRateBps;
      }
    else if (t.rtt.GetSeconds () > 0.0001 && t.cwnd > 0)
      {
        t.bandwidthBps = (double)t.cwnd * 8.0 / t.rtt.GetSeconds ();
      }

    // Connect public trace sources
    sf->TraceConnectWithoutContext ("RTT", MakeBoundCallback (&TelemetryTracker::OnRttTrace, id));
    sf->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&TelemetryTracker::OnCwndTrace, id));
    sf->TraceConnectWithoutContext ("HighestRxAck", MakeBoundCallback (&TelemetryTracker::OnAckTrace, id));
    sf->TraceConnectWithoutContext ("CongState", MakeBoundCallback (&TelemetryTracker::OnCongStateTrace, id));
  }

  void UpdateFromSocket (int id, Ptr<MpTcpSubflow> sf)
  {
    if (m_registered.find (id) == m_registered.end ())
      {
        RegisterSubflow (id, sf);
        m_registered.insert (id);
      }

    SubflowTelemetry& t = m_subflows[id];
    Time rttEst = sf->GetRttEstimate ();
    if (rttEst.GetMilliSeconds () > 0)
      {
        t.rtt = rttEst;
      }
    Time minRtt = sf->GetMinRtt ();
    if (minRtt.GetMilliSeconds () > 0 && minRtt != Time::Max ())
      {
        t.minRtt = minRtt;
        t.minRttSampled = true;
      }
    uint32_t cwnd = sf->GetCwnd ();
    if (cwnd > 0)
      {
        t.cwnd = cwnd;
      }
    t.bytesInFlight = sf->BytesInFlight ();
    t.availableWindow = sf->AvailableWindow ();
    uint16_t mss = sf->GetSegSize ();
    if (mss > 0) t.segSize = mss;

    if (t.deliveryRateBps > 0.0)
      {
        t.bandwidthBps = std::max (t.deliveryRateBps, (double)t.cwnd * 8.0 / std::max (0.001, t.rtt.GetSeconds ()));
      }
    else if (t.rtt.GetSeconds () > 0.0001 && t.cwnd > 0)
      {
        t.bandwidthBps = (double)t.cwnd * 8.0 / t.rtt.GetSeconds ();
      }
  }

  SubflowTelemetry GetTelemetry (int id)
  {
    auto it = m_subflows.find (id);
    if (it != m_subflows.end ())
      {
        return it->second;
      }
    return SubflowTelemetry ();
  }

  void RecordTx (int id)
  {
    SubflowTelemetry& t = m_subflows[id];
    t.txPackets++;
    t.lossRate = (t.txPackets > 0) ? ((double)t.retransmissions / (double)t.txPackets) : 0.0;
  }

  void RecordLoss (int id)
  {
    SubflowTelemetry& t = m_subflows[id];
    t.retransmissions++;
    t.lossRate = (t.txPackets > 0) ? ((double)t.retransmissions / (double)t.txPackets) : 0.0;
  }

private:
  TelemetryTracker () {}

  std::map<int, SubflowTelemetry> m_subflows;
  std::set<int> m_registered;
};

} // namespace ns3

#endif /* TELEMETRY_TRACKER_H */
