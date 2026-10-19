/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MPTCP_SCHEDULER_SUPERVISED_H
#define MPTCP_SCHEDULER_SUPERVISED_H

#include "../classical_schedulers/telemetry_scheduler.h"
#include "feature_extractor.h"
#include "model_generated.h"
#include "decision_logger.h"
#include <limits>
#include <vector>

namespace ns3 {

/**
 * \brief MpTcpSchedulerSupervised
 * A lightweight supervised-learning MPTCP scheduler evaluated on embedded C++ decision trees.
 * Implements the decision procedure specified in MPTCP Implementation Roadmap Appendix A.5.
 */
class MpTcpSchedulerSupervised : public TelemetryScheduler
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MpTcpSchedulerSupervised")
      .SetParent<MpTcpScheduler> ()
      .AddConstructor<MpTcpSchedulerSupervised> ();
    return tid;
  }

  MpTcpSchedulerSupervised ()
    : TelemetryScheduler (),
      m_totalDecisions (0),
      m_fallbackDecisions (0),
      m_useWaitRule (true)
  {}

  virtual ~MpTcpSchedulerSupervised () {}

  void SetUseWaitRule (bool useWait)
  {
    m_useWaitRule = useWait;
  }

  uint32_t GetTotalDecisions () const { return m_totalDecisions; }
  uint32_t GetFallbackDecisions () const { return m_fallbackDecisions; }
  double GetFallbackRate () const
  {
    return m_totalDecisions > 0 ? (double)m_fallbackDecisions / (double)m_totalDecisions : 0.0;
  }

  virtual bool GenerateMapping (int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
  {
    if (!m_meta || m_meta->GetNActiveSubflows () == 0)
      {
        return false;
      }

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

    RefreshAll ();

    int nSubflows = m_meta->GetNActiveSubflows ();
    if (nSubflows == 0)
      {
        return false;
      }

    m_totalDecisions++;

    // 1. Identify all eligible subflows with available window
    std::vector<int> eligibleSubflows;
    for (int i = 0; i < nSubflows; ++i)
      {
        Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (i);
        uint16_t mss = sf->GetSegSize ();
        if (mss == 0) mss = 1440;
        if (sf->AvailableWindow () >= mss)
          {
            eligibleSubflows.push_back (i);
          }
      }

    if (eligibleSubflows.empty ())
      {
        return false;
      }

    // 2. Evaluate ML-predicted completion delay across ALL active subflows
    float features[FeatureExtractor::kNumFeatures];
    std::vector<float> predDelays (nSubflows, 1e9f);
    int bestAllId = eligibleSubflows[0];
    float bestAllDelay = 1e9f;

    for (int i = 0; i < nSubflows; ++i)
      {
        FeatureExtractor::Build (i, nSubflows, features);
        if (!FeatureExtractor::AllFinite (features))
          {
            m_fallbackDecisions++;
            return FallbackMinRtt (eligibleSubflows, activeSubflowArrayId, dsn, length, metaWin, pending);
          }

        float pred = supervised_model::ScaleAndPredict (features);
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);

        // Penalize impaired subflow based on observed packet loss and queue drainage delay
        float bwMbps = static_cast<float> (std::max (1.0, tel.bandwidthBps / 1e6));
        float lossPenalty = 1.0f + static_cast<float> (tel.lossRate) * 10.0f;
        float bwFactor = 1.0f + (15.0f / bwMbps);
        float effectiveDelay = pred * bwFactor * lossPenalty;
        predDelays[i] = effectiveDelay;

        if (effectiveDelay < bestAllDelay)
          {
            bestAllDelay = effectiveDelay;
            bestAllId = i;
          }
      }

    // 3. Find the best eligible subflow by ML completion score
    int bestEligibleId = -1;
    float bestEligibleScore = 1e9f;

    for (int i : eligibleSubflows)
      {
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);
        double availRatio = (tel.cwnd > 0) ? ((double)tel.availableWindow / (double)tel.cwnd) : 1.0;
        float score = predDelays[i] * (1.0f + static_cast<float> (1.0 - availRatio) * 0.25f);

        if (score < bestEligibleScore)
          {
            bestEligibleScore = score;
            bestEligibleId = i;
          }
      }

    if (bestEligibleId < 0)
      {
        return false;
      }

    // 4. Decision: If the globally optimal subflow has available window, schedule immediately
    int chosenSubflow = -1;
    Ptr<MpTcpSubflow> bestAllSf = m_meta->GetSubflow (bestAllId);
    uint16_t bestMss = bestAllSf->GetSegSize ();
    if (bestMss == 0) bestMss = 1440;
    if (bestAllSf->AvailableWindow () >= bestMss)
      {
        chosenSubflow = bestAllId;
      }
    else
      {
        // Deferral check: If the best eligible secondary subflow's completion delay exceeds
        // waiting for the primary path to complete its queue drain, defer to avoid HoL buffer starvation
        float primaryDrainTime = predDelays[bestAllId] * 1.35f;
        if (predDelays[bestEligibleId] > primaryDrainTime)
          {
            return false;
          }
        chosenSubflow = bestEligibleId;
      }

    int bestSubflow = chosenSubflow;
    if (bestSubflow < 0)
      {
        return false;
      }

    Ptr<MpTcpSubflow> chosenSf = m_meta->GetSubflow (bestSubflow);
    activeSubflowArrayId = bestSubflow;
    dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());

    uint16_t mss = chosenSf->GetSegSize ();
    if (mss == 0) mss = 1440;
    uint32_t sfWin = chosenSf->AvailableWindow ();
    uint32_t chosenLen = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));

    if (chosenLen == 0)
      {
        return false;
      }

    length = static_cast<uint16_t> (chosenLen);

    if (DecisionLogger::Get ().IsEnabled ())
      {
        DecisionLogger::Get ().LogDecision (Simulator::Now (), dsn, length, bestSubflow, nSubflows);
      }

    return true;
  }

private:
  bool FallbackMinRtt (const std::vector<int>& eligibleSubflows,
                       int& activeSubflowArrayId, SequenceNumber64& dsn,
                       uint16_t& length, uint32_t metaWin, uint32_t pending)
  {
    int best = eligibleSubflows[0];
    Time lowestRtt = Time::Max ();

    for (int i : eligibleSubflows)
      {
        SubflowTelemetry tel = TelemetryTracker::Get ().GetTelemetry (i);
        if (tel.rtt < lowestRtt)
          {
            lowestRtt = tel.rtt;
            best = i;
          }
      }

    Ptr<MpTcpSubflow> sf = m_meta->GetSubflow (best);
    activeSubflowArrayId = best;
    dsn = SequenceNumber64 (m_meta->Getvalue_nextTxSeq ().GetValue ());

    uint16_t mss = sf->GetSegSize ();
    if (mss == 0) mss = 1440;
    uint32_t sfWin = sf->AvailableWindow ();
    uint32_t chosenLen = std::min ((uint32_t)mss, std::min (sfWin, std::min (metaWin, pending)));

    length = static_cast<uint16_t> (chosenLen);
    return length > 0;
  }

  uint32_t m_totalDecisions;
  uint32_t m_fallbackDecisions;
  bool m_useWaitRule;
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_SUPERVISED_H */
