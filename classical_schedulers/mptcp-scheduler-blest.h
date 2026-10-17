#ifndef MPTCP_SCHEDULER_BLEST_H
#define MPTCP_SCHEDULER_BLEST_H

#include "ns3/mptcp-scheduler.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include <vector>
#include <map>

namespace ns3
{
class MpTcpSocketBase;
class MpTcpSubflow;

/**
 * \brief BLEST: Blocking Estimation-based MPTCP Scheduler (Ferlin et al., IFIP 2016)
 *
 * BLEST reduces Head-of-Line (HoL) blocking by estimating whether sending a packet
 * on a slower subflow will cause receiver-side buffer blocking compared to waiting
 * for the faster subflow to free up its congestion window.
 */
class MpTcpSchedulerBlest : public MpTcpScheduler
{
public:
  static TypeId GetTypeId (void);

  MpTcpSchedulerBlest();
  virtual ~MpTcpSchedulerBlest();

  void SetMeta(Ptr<MpTcpSocketBase> metaSock);
  virtual bool GenerateMapping(int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length);
  virtual Ptr<MpTcpSubflow> GetSubflowToUseForEmptyPacket();

protected:
  int SelectSubflow();
  bool IsSlowSubflowBlocked(int fastId, int slowId);
  double EstimateRate(int subflowId);

  Ptr<MpTcpSocketBase> m_metaSock;
  std::map<int, double> m_virtualQueuedBytes;
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_BLEST_H */
