/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_BLEST_H
#define MPTCP_SCHEDULER_BLEST_H

#include "telemetry_scheduler.h"

namespace ns3 {

/**
 * \brief MpTcpSchedulerBlest
 * Blocking Estimation-based MPTCP Scheduler (Ferlin et al., IFIP Networking 2016).
 * Prevents Head-of-Line (HoL) blocking by estimating whether sending on a slower subflow
 * will cause the receiver buffer to exhaust before the faster subflow can clear it.
 */
class MpTcpSchedulerBlest : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerBlest")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerBlest> ();
    return tid;
  }

  MpTcpSchedulerBlest ()
    : TelemetryScheduler (),
      m_lambda (1.0)
  {}

  virtual ~MpTcpSchedulerBlest () {}

  bool IsSlowSubflowBlocked (int fastId, int slowId)
  {
    SubflowTelemetry fastTel = TelemetryTracker::Get ().GetTelemetry (fastId);
    SubflowTelemetry slowTel = TelemetryTracker::Get ().GetTelemetry (slowId);

    if (slowTel.rtt <= fastTel.rtt || fastTel.rtt.GetSeconds () <= 0.001)
      {
        return false;
      }

    Ptr<MpTcpSubflow> fastSf = m_meta->GetSubflow (fastId);
    Ptr<MpTcpSubflow> slowSf = m_meta->GetSubflow (slowId);

    double rttRatio = slowTel.rtt.GetSeconds () / fastTel.rtt.GetSeconds ();
    uint32_t fastMss = fastTel.segSize > 0 ? fastTel.segSize : 1440;
    uint32_t cwndSegments = fastTel.cwnd / fastMss;

    double X = (double)fastMss * ((double)cwndSegments + (rttRatio - 1.0) / 2.0) * rttRatio;
    uint32_t metaAvail = m_meta->AvailableWindow ();
    uint32_t slowInFlight = slowSf->BytesInFlight ();
    double freeWindow = (metaAvail > slowInFlight) ? (metaAvail - slowInFlight) : 0.0;

    // Ferlin et al. (2016) blocking condition:
    // Bound free window by receiver headroom or fast-path BDP to avoid false unblocking with oversized buffers
    double fastBdp = (fastTel.bandwidthBps > 1000.0)
      ? (fastTel.bandwidthBps * fastTel.rtt.GetSeconds () / 8.0) : (double)(fastTel.cwnd);
    double effectiveWindow = std::min (freeWindow, std::max (fastBdp * 2.0, (double)fastTel.cwnd * 2.0));

    // Skip slow subflow if lambda * X exceeds effective headroom or under severe disparity
    bool blocked = (m_lambda * X > effectiveWindow) || (rttRatio > 1.8 && X > fastBdp);

    if (blocked)
      {
        m_lambda = std::min (2.0, m_lambda + 0.05); // Adapt up on blocking
      }
    else
      {
        m_lambda = std::max (0.5, m_lambda - 0.01); // Decay down
      }

    return blocked;
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

    int fastId = 0;
    Time minRtt = Time::Max ();
    uint32_t bestWin = 0;
    for (int i = 0; i < nSubflows; ++i)
      {
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        uint32_t sfWin = sf->AvailableWindow ();
        if (tel.rtt < minRtt || (tel.rtt == minRtt && sfWin > bestWin))
          {
            minRtt = tel.rtt;
            bestWin = sfWin;
            fastId = i;
          }
      }

    Ptr<MpTcpSubflow> fastSf = m_meta->GetSubflow (fastId);
    uint16_t fastMss = fastSf->GetSegSize ();
    if (fastMss == 0) fastMss = 1440;

    // If fast subflow has available window, always prefer it
    if (fastSf->AvailableWindow () >= fastMss)
      {
        return fastId;
      }

    // 2. Fast subflow is full. Evaluate slower subflows using BLEST condition
    for (int i = 0; i < nSubflows; ++i)
      {
        if (i == fastId) continue;
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;

        if (sf->AvailableWindow () < mss)
          {
            continue;
          }

        if (!IsSlowSubflowBlocked (fastId, i))
          {
            return i; // Can transmit without inducing buffer exhaustion
          }
      }

    return -1; // Wait for fast subflow
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

private:
  double m_lambda;
};

// Backwards compatibility alias for custom naming
typedef MpTcpSchedulerBlest MpTcpSchedulerBlestCustom;

} // namespace ns3

#endif /* MPTCP_SCHEDULER_BLEST_H */
