/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef TELEMETRY_SCHEDULER_H
#define TELEMETRY_SCHEDULER_H

#include "ns3/mptcp-scheduler.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/mptcp-subflow.h"
#include "ns3/log.h"
#include "telemetry_tracker.h"
#include "../adaptive_supervised_scheduler/decision_logger.h"

namespace ns3 {

/**
 * \brief TelemetryScheduler
 * Base scheduler class for classical and learned MPTCP schedulers.
 * Implements lazy registration of subflow telemetry callbacks upon establishment
 * and provides RefreshAll() to update socket telemetry prior to decision logic.
 */
class TelemetryScheduler : public MpTcpScheduler
{
public:
  TelemetryScheduler () : m_meta (0), m_registered (0) {}
  virtual ~TelemetryScheduler () {}

  virtual void SetMeta (Ptr<MpTcpSocketBase> meta)
  {
    m_meta = meta;
    m_registered = 0;
  }

  virtual Ptr<MpTcpSubflow> GetSubflowToUseForEmptyPacket ()
  {
    NS_ASSERT (m_meta && m_meta->GetNActiveSubflows () > 0);
    return m_meta->GetSubflow (0);
  }

protected:
  // Subflows only exist after the handshake, so register lazily on first use.
  void EnsureRegistered ()
  {
    if (!m_meta) return;
    int n = m_meta->GetNActiveSubflows ();
    for (int i = m_registered; i < n; ++i)
      {
        // connects the RTT, cwnd and ack traces
        TelemetryTracker::Get ().RegisterSubflow (i, m_meta->GetSubflow (i));
      }
    m_registered = n;
  }

  // Call at the start of every GenerateMapping().
  void RefreshAll ()
  {
    EnsureRegistered ();
    for (int i = 0; i < m_registered; ++i)
      {
        TelemetryTracker::Get ().UpdateFromSocket (i, m_meta->GetSubflow (i));
      }
  }

  void MaybeLogDecision (SequenceNumber64 dsn, uint16_t length, int chosenSubflow, int nActiveSubflows)
  {
    if (DecisionLogger::Get ().IsEnabled ())
      {
        DecisionLogger::Get ().LogDecision (Simulator::Now (), dsn, length, chosenSubflow, nActiveSubflows);
      }
  }

  Ptr<MpTcpSocketBase> m_meta;
  int m_registered;
};

} // namespace ns3

#endif /* TELEMETRY_SCHEDULER_H */
