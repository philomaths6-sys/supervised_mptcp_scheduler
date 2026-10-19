/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_MIN_RTT_H
#define MPTCP_SCHEDULER_MIN_RTT_H

#include "telemetry_scheduler.h"

namespace ns3 {

/**
 * \brief MpTcpSchedulerMinRtt
 * Canonical minRTT / FastestRTT scheduler built on public APIs.
 * Schedules packets to the subflow with the lowest observed RTT that has available window >= MSS.
 */
class MpTcpSchedulerMinRtt : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerMinRtt")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerMinRtt> ();
    return tid;
  }

  MpTcpSchedulerMinRtt () : TelemetryScheduler () {}
  virtual ~MpTcpSchedulerMinRtt () {}

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

    int bestId = -1;
    Time lowestRtt = Time::Max ();
    int nSubflows = m_meta->GetNActiveSubflows ();

    uint32_t bestWin = 0;
    for (int i = 0; i < nSubflows; ++i)
      {
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);

        uint32_t sfWin = sf->AvailableWindow ();
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;

        // Require at least 1 MSS available window (avoid SWS)
        if (sfWin < mss)
          {
            continue;
          }

        if (tel.rtt < lowestRtt || (tel.rtt == lowestRtt && sfWin > bestWin))
          {
            lowestRtt = tel.rtt;
            bestWin = sfWin;
            bestId = i;
          }
      }

    if (bestId < 0)
      {
        return false;
      }

    Ptr<MpTcpSubflow> bestSf = m_meta->GetSubflow (bestId);
    uint32_t sfWin = bestSf->AvailableWindow ();
    uint16_t mss = bestSf->GetSegSize ();
    if (mss == 0) mss = 1440;

    activeSubflowArrayId = bestId;
    dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());
    length = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));

    TelemetryTracker::Get ().RecordTx (bestId);
    MaybeLogDecision (dsn, length, bestId, nSubflows);
    return (length > 0);
  }
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_MIN_RTT_H */
