/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef FEATURE_EXTRACTOR_H
#define FEATURE_EXTRACTOR_H

#include "../classical_schedulers/telemetry_tracker.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace ns3 {

/**
 * \brief FeatureExtractor implements the single, shared feature calculation
 * used identically by both the training data logger and the online inference scheduler.
 * Prevents train/serve skew by design.
 */
class FeatureExtractor
{
public:
  static const int kNumFeatures = 12;

  /**
   * \brief Extract 12-dimensional feature vector for a candidate subflow
   * \param sfId Target subflow index to score
   * \param nActiveSubflows Number of currently active subflows
   * \param outFeatures Pointer to float array of size at least 12
   */
  static void Build (int sfId, int nActiveSubflows, float* outFeatures)
  {
    if (!outFeatures) return;

    TelemetryTracker& tracker = TelemetryTracker::Get ();
    SubflowTelemetry target = tracker.GetTelemetry (sfId);

    // Compute aggregate context across all active subflows
    double minRttMsAll = 1e9;
    double totalBwBps = 0.0;
    for (int i = 0; i < nActiveSubflows; ++i)
      {
        SubflowTelemetry tel = tracker.GetTelemetry (i);
        double rttMs = tel.rtt.GetMilliSeconds ();
        if (rttMs > 0.1 && rttMs < minRttMsAll)
          {
            minRttMsAll = rttMs;
          }
        totalBwBps += std::max (1000.0, tel.bandwidthBps);
      }
    if (minRttMsAll >= 1e8) minRttMsAll = 20.0;
    if (totalBwBps <= 0.0) totalBwBps = 1000.0;

    double rttMs = target.rtt.GetMilliSeconds ();
    if (rttMs <= 0.0) rttMs = 20.0;

    double minRttMs = target.minRttSampled ? target.minRtt.GetMilliSeconds () : rttMs;
    if (minRttMs <= 0.0) minRttMs = rttMs;

    double bwMbps = target.bandwidthBps / 1e6;
    double cwndB = (double)target.cwnd;
    double inflightB = (double)target.bytesInFlight;
    double availB = (double)target.availableWindow;
    double lossRate = std::max (0.0, std::min (1.0, target.lossRate));

    // 7 Core features from proposal
    outFeatures[0] = static_cast<float> (rttMs);
    outFeatures[1] = static_cast<float> (minRttMs);
    outFeatures[2] = static_cast<float> (bwMbps);
    outFeatures[3] = static_cast<float> (cwndB);
    outFeatures[4] = static_cast<float> (inflightB);
    outFeatures[5] = static_cast<float> (availB);
    outFeatures[6] = static_cast<float> (lossRate);

    // 5 Scale-free relative features
    outFeatures[7] = static_cast<float> (rttMs / std::max (0.1, minRttMsAll));
    outFeatures[8] = static_cast<float> (target.bandwidthBps / totalBwBps);
    outFeatures[9] = static_cast<float> (availB / std::max (1.0, cwndB));
    outFeatures[10] = static_cast<float> (rttMs / std::max (0.1, minRttMs));
    outFeatures[11] = static_cast<float> (nActiveSubflows);
  }

  static bool AllFinite (const float* features, int numFeatures = kNumFeatures)
  {
    if (!features) return false;
    for (int i = 0; i < numFeatures; ++i)
      {
        if (std::isnan (features[i]) || std::isinf (features[i]))
          {
            return false;
          }
      }
    return true;
  }
};

} // namespace ns3

#endif /* FEATURE_EXTRACTOR_H */
