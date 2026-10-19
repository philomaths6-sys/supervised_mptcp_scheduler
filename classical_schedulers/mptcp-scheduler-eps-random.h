/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_EPS_RANDOM_H
#define MPTCP_SCHEDULER_EPS_RANDOM_H

#include "telemetry_scheduler.h"
#include "../adaptive_supervised_scheduler/decision_logger.h"
#include <vector>
#include <cstdlib>

namespace ns3 {

/**
 * \brief MpTcpSchedulerEpsRandom
 * Epsilon-random exploration policy for Phase 2 dataset collection.
 * With probability epsilon, selects a uniformly random eligible subflow;
 * with probability (1 - epsilon), exploits the minRTT eligible subflow.
 */
class MpTcpSchedulerEpsRandom : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerEpsRandom")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerEpsRandom> ();
    return tid;
  }

  MpTcpSchedulerEpsRandom ()
    : TelemetryScheduler (),
      m_epsilon (0.3)
  {}

  virtual ~MpTcpSchedulerEpsRandom () {}

  void SetEpsilon (double eps)
  {
    m_epsilon = std::max (0.0, std::min (1.0, eps));
  }

  virtual bool GenerateMapping (int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
  {
    if (!m_meta || m_meta->GetNActiveSubflows () == 0)
      {
        return false;
      }

    uint32_t metaWin = m_meta->AvailableWindow ();
    if (metaWin == 0)
      {
        return false;
      }

    RefreshAll ();

    uint32_t pending = m_meta->GetPendingDataSize ();
    if (pending == 0)
      {
        return false;
      }

    int nSubflows = m_meta->GetNActiveSubflows ();
    std::vector<int> eligible;

    for (int i = 0; i < nSubflows; ++i)
      {
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        uint32_t sfWin = sf->AvailableWindow ();
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;

        if (sfWin >= mss)
          {
            eligible.push_back (i);
          }
      }

    if (eligible.empty ())
      {
        return false;
      }

    int chosen = -1;
    double r = (double)rand () / (double)RAND_MAX;

    if (r < m_epsilon)
      {
        // Explore: random eligible subflow
        int pick = rand () % eligible.size ();
        chosen = eligible[pick];
      }
    else
      {
        // Exploit: minRTT eligible subflow
        Time lowestRtt = Time::Max ();
        for (int i : eligible)
          {
            SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);
            if (tel.rtt < lowestRtt)
              {
                lowestRtt = tel.rtt;
                chosen = i;
              }
          }
      }

    if (chosen < 0) chosen = eligible[0];

    Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (chosen);
    activeSubflowArrayId = chosen;
    dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());

    uint16_t mss = sf->GetSegSize ();
    if (mss == 0) mss = 1440;
    uint32_t sfWin = sf->AvailableWindow ();
    uint32_t chosenLen = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));

    length = static_cast<uint16_t> (chosenLen);

    if (DecisionLogger::Get ().IsEnabled ())
      {
        DecisionLogger::Get ().LogDecision (Simulator::Now (), dsn, length, chosen, nSubflows);
      }

    return length > 0;
  }

private:
  double m_epsilon;
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_EPS_RANDOM_H */
