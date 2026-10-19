/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_ROUND_ROBIN_H
#define MPTCP_SCHEDULER_ROUND_ROBIN_H

#include "telemetry_scheduler.h"

namespace ns3 {

/**
 * \brief MpTcpSchedulerRoundRobin
 * Canonical Round-Robin MPTCP scheduler built on public APIs with Silly Window Syndrome prevention.
 */
class MpTcpSchedulerRoundRobin : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerRoundRobin")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerRoundRobin> ();
    return tid;
  }

  MpTcpSchedulerRoundRobin ()
    : TelemetryScheduler (),
      m_lastIdx (-1)
  {}

  virtual ~MpTcpSchedulerRoundRobin () {}

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

    int nSubflows = m_meta->GetNActiveSubflows ();
    int attempts = 0;

    while (attempts < nSubflows)
      {
        attempts++;
        m_lastIdx = (m_lastIdx + 1) % nSubflows;
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (m_lastIdx);

        uint32_t sfWin = sf->AvailableWindow ();
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;

        if (sfWin < mss)
          {
            continue;
          }

        activeSubflowArrayId = m_lastIdx;
        dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());
        length = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));
        TelemetryTracker::Get ().RecordTx (m_lastIdx);
        MaybeLogDecision (dsn, length, m_lastIdx, nSubflows);
        return (length > 0);
      }

    return false;
  }

private:
  int m_lastIdx;
};

// Backwards compatibility alias
typedef MpTcpSchedulerRoundRobin MpTcpSchedulerRoundRobinCustom;

} // namespace ns3

#endif /* MPTCP_SCHEDULER_ROUND_ROBIN_H */
