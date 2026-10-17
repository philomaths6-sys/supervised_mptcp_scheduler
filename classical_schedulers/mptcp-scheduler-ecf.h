#ifndef MPTCP_SCHEDULER_ECF_H
#define MPTCP_SCHEDULER_ECF_H

#include "ns3/mptcp-scheduler.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include <vector>

namespace ns3
{
class MpTcpSocketBase;
class MpTcpSubflow;

/**
 * \brief ECF: Earliest Completion First MPTCP Scheduler (Lim et al., CoNEXT 2017)
 *
 * ECF estimates packet arrival times across all paths. It only schedules packets
 * on subflows if their estimated arrival time is earlier than sending on an alternate
 * faster path once that path's congestion window re-opens.
 */
class MpTcpSchedulerEcf : public MpTcpScheduler
{
public:
  static TypeId GetTypeId (void);

  MpTcpSchedulerEcf();
  virtual ~MpTcpSchedulerEcf();

  void SetMeta(Ptr<MpTcpSocketBase> metaSock);
  virtual bool GenerateMapping(int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length);
  virtual Ptr<MpTcpSubflow> GetSubflowToUseForEmptyPacket();

protected:
  int SelectSubflow();
  double EstimateCompletionTime(int subflowId);

  Ptr<MpTcpSocketBase> m_metaSock;
};

} // namespace ns3

#endif /* MPTCP_SCHEDULER_ECF_H */
