/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_ECF_H
#define MPTCP_SCHEDULER_ECF_H

#include "telemetry_scheduler.h"

namespace ns3 {

/**
 * \brief MpTcpSchedulerEcf
 * Earliest Completion First (ECF) MPTCP Scheduler (Lim et al., ACM CoNEXT 2017).
 * Compares the estimated completion time (ECT) to drain queued data over the fast path alone
 * against the ECT when splitting onto the slower subflow, utilizing a hysteresis threshold
 * to prevent unnecessary path oscillations.
 */
class MpTcpSchedulerEcf : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerEcf")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerEcf> ();
    return tid;
  }

  MpTcpSchedulerEcf ()
    : TelemetryScheduler (),
      m_hysteresis (false)
  {}

  virtual ~MpTcpSchedulerEcf () {}

  double EstimateCompletionTime (int subflowId, uint32_t additionalBytes = 0)
  {
    SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (subflowId);
    Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (subflowId);

    double rate = (tel.bandwidthBps > 1000.0) ? (tel.bandwidthBps / 8.0) : (1250000.0); // bytes/sec
    double inFlightBytes = (double)sf->BytesInFlight () + additionalBytes;
    double queueDelaySec = inFlightBytes / rate;
    double rttSec = tel.rtt.GetSeconds ();

    return rttSec + queueDelaySec;
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

    // 1. Identify fast subflow (lowest smoothed RTT)
    int fastId = 0;
    Time minRtt = Time::Max ();
    for (int i = 0; i < nSubflows; ++i)
      {
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);
        if (tel.rtt < minRtt)
          {
            minRtt = tel.rtt;
            fastId = i;
          }
      }

    Ptr<MpTcpSubflow> fastSf = m_meta->GetSubflow (fastId);
    uint16_t fastMss = fastSf->GetSegSize ();
    if (fastMss == 0) fastMss = 1440;

    // Fast path has available window: always schedule on fast path
    if (fastSf->AvailableWindow () >= fastMss)
      {
        m_hysteresis = false;
        return fastId;
      }

    // 2. Fast path is full. Estimate whether waiting for fast path is faster than slow path
    double ectFastWait = EstimateCompletionTime (fastId, fastMss);

    int bestSlowId = -1;
    double lowestSlowEct = 1e9;

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

        double ectSlow = EstimateCompletionTime (i, mss);
        if (ectSlow < lowestSlowEct)
          {
            lowestSlowEct = ectSlow;
            bestSlowId = i;
          }
      }

    if (bestSlowId < 0)
      {
        return -1; // No other subflows have free window
      }

    // ECF decision rule with 5% hysteresis margin
    double hysteresisMargin = m_hysteresis ? 1.0 : 1.05;
    if (lowestSlowEct < ectFastWait * hysteresisMargin)
      {
        m_hysteresis = true;
        return bestSlowId;
      }
    else
      {
        m_hysteresis = false;
        return -1; // Wait for fast subflow
      }
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
  bool m_hysteresis;
};

// Backwards compatibility alias
typedef MpTcpSchedulerEcf MpTcpSchedulerEcfCustom;

} // namespace ns3

#endif /* MPTCP_SCHEDULER_ECF_H */
