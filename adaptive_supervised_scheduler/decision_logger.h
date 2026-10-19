/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef DECISION_LOGGER_H
#define DECISION_LOGGER_H

#include "ns3/core-module.h"
#include "ns3/sequence-number.h"
#include "../classical_schedulers/telemetry_tracker.h"
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>

namespace ns3 {

/**
 * \brief DecisionLogger logs per-segment scheduling decisions and telemetry state.
 * Implements schema defined in MPTCP Implementation Roadmap Appendix A.3.
 */
class DecisionLogger
{
public:
  static DecisionLogger& Get ()
  {
    static DecisionLogger instance;
    return instance;
  }

  void SetEnabled (bool enabled, const std::string& filename = "",
                   const std::string& scenarioId = "s0000",
                   const std::string& policyName = "Heuristic",
                   uint32_t runId = 1)
  {
    m_enabled = enabled;
    m_scenarioId = scenarioId;
    m_policy = policyName;
    m_runId = runId;

    if (m_file.is_open ())
      {
        m_file.close ();
      }

    if (m_enabled && !filename.empty ())
      {
        m_file.open (filename);
        if (m_file.is_open ())
          {
            WriteHeader ();
          }
      }
  }

  bool IsEnabled () const
  {
    return m_enabled && m_file.is_open ();
  }

  void LogDecision (Time now, SequenceNumber64 dsn, uint16_t length,
                   int chosenSubflow, int nActiveSubflows,
                   int oracleBest = -1, const std::vector<double>& oracleEct = {})
  {
    if (!IsEnabled ()) return;

    TelemetryTracker& tracker = TelemetryTracker::Get ();

    m_file << m_runId << ","
           << m_scenarioId << ","
           << m_policy << ","
           << std::fixed << std::setprecision (6) << now.GetSeconds () << ","
           << dsn.GetValue () << ","
           << length << ","
           << chosenSubflow << ","
           << nActiveSubflows;

    // Slots k = 0 to 3
    for (int k = 0; k < 4; ++k)
      {
        if (k < nActiveSubflows)
          {
            SubflowTelemetry tel = tracker.GetTelemetry (k);
            double rttMs = tel.rtt.GetMilliSeconds ();
            double minRttMs = tel.minRttSampled ? tel.minRtt.GetMilliSeconds () : rttMs;
            double bwMbps = tel.bandwidthBps / 1e6;
            m_file << ",1,"
                   << std::setprecision (2) << rttMs << ","
                   << minRttMs << ","
                   << std::setprecision (3) << bwMbps << ","
                   << tel.cwnd << ","
                   << tel.bytesInFlight << ","
                   << tel.availableWindow << ","
                   << std::setprecision (4) << tel.lossRate;
          }
        else
          {
            // Inactive padded slot
            m_file << ",0,0.0,0.0,0.0,0,0,0,0.0";
          }
      }

    // Secondary oracle labels
    m_file << "," << oracleBest;
    for (int k = 0; k < 4; ++k)
      {
        if (k < (int)oracleEct.size ())
          {
            m_file << "," << std::setprecision (2) << oracleEct[k];
          }
        else
          {
            m_file << ",0.0";
          }
      }

    m_file << "\n";
  }

  void Flush ()
  {
    if (m_file.is_open ())
      {
        m_file.flush ();
      }
  }

  void Close ()
  {
    if (m_file.is_open ())
      {
        m_file.close ();
      }
    m_enabled = false;
  }

private:
  DecisionLogger ()
    : m_enabled (false),
      m_scenarioId ("s0000"),
      m_policy ("default"),
      m_runId (1)
  {}

  void WriteHeader ()
  {
    m_file << "run_id,scenario_id,policy,t_s,dsn,len,chosen,n_sf";
    for (int k = 0; k < 4; ++k)
      {
        m_file << ",valid_" << k
               << ",rtt_ms_" << k
               << ",minrtt_ms_" << k
               << ",bw_mbps_" << k
               << ",cwnd_B_" << k
               << ",inflight_B_" << k
               << ",avail_B_" << k
               << ",loss_" << k;
      }
    m_file << ",oracle_best,oracle_ect_ms_0,oracle_ect_ms_1,oracle_ect_ms_2,oracle_ect_ms_3\n";
  }

  bool m_enabled;
  std::ofstream m_file;
  std::string m_scenarioId;
  std::string m_policy;
  uint32_t m_runId;
};

} // namespace ns3

#endif /* DECISION_LOGGER_H */
