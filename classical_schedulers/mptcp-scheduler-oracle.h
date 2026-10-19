/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_ORACLE_H
#define MPTCP_SCHEDULER_ORACLE_H

#include "telemetry_scheduler.h"

namespace ns3 {

/**
 * \brief MpTcpSchedulerOracle
 * Privileged oracle scheduler with perfect ground-truth knowledge of link rates and propagation delays.
 * Selects the subflow with the earliest absolute arrival time at the receiver.
 * Serves as an empirical upper-bound performance ceiling and oracle label generator.
 */
class MpTcpSchedulerOracle : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerOracle")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerOracle> ();
    return tid;
  }

  MpTcpSchedulerOracle () : TelemetryScheduler () {}
  virtual ~MpTcpSchedulerOracle () {}

  double EstimateOracleArrival (int id, uint32_t segBytes)
  {
    SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (id);
    Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (id);

    // Privileged ground-truth delivery calculation
    double rateBps = std::max (1000.0, tel.bandwidthBps);
    double propDelaySec = tel.minRttSampled ? (tel.minRtt.GetSeconds () / 2.0) : (tel.rtt.GetSeconds () / 2.0);
    double queueDelaySec = (double)(sf->BytesInFlight () + segBytes) * 8.0 / rateBps;

    return Simulator::Now ().GetSeconds () + propDelaySec + queueDelaySec;
  }

  int SelectSubflow ()
  {
    int nSubflows = m_meta->GetNActiveSubflows ();
    if (nSubflows == 0) return -1;
    if (nSubflows == 1)
      {
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (0);
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;
        return (sf->AvailableWindow () >= mss) ? 0 : -1;
      }

    int bestId = -1;
    double earliestArrival = 1e12;

    for (int i = 0; i < nSubflows; ++i)
      {
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;

        if (sf->AvailableWindow () < mss)
          {
            continue;
          }

        double arr = EstimateOracleArrival (i, mss);
        if (arr < earliestArrival)
          {
            earliestArrival = arr;
            bestId = i;
          }
      }

    return bestId;
  }

  virtual bool GenerateMapping (int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
  {
    if (!m_meta || m_meta->GetNActiveSubflows () == 0)
      {
        return false;
      }

    RefreshAll ();

    uint32_t pending = m_meta->GetPendingDataSize ();
    if (pending == 0)
      {
        return false;
      }

    uint32_t metaWin = m_meta->AvailableWindow ();
    if (metaWin == 0)
      {
        return false;
      }

    int selected = SelectSubflow ();
    if (selected < 0)
      {
        return false;
      }

    Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (selected);
    uint32_t sfWin = sf->AvailableWindow ();
    uint16_t mss = sf->GetSegSize ();
    if (mss == 0) mss = 1440;

    activeSubflowArrayId = selected;
    dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());
    length = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));
    TelemetryTracker::Get ().RecordTx (selected);
    MaybeLogDecision (dsn, length, selected, m_meta->GetNActiveSubflows ());
    return (length > 0);
  }
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_ORACLE_H */
