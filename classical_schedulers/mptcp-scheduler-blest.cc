#include "mptcp-scheduler-blest.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/mptcp-subflow.h"
#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("MpTcpSchedulerBlest");
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerBlest);

static inline SequenceNumber64 SEQ32TO64(SequenceNumber32 seq)
{
  return SequenceNumber64 (seq.GetValue ());
}

TypeId
MpTcpSchedulerBlest::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpTcpSchedulerBlest")
    .SetParent<MpTcpScheduler> ()
    .AddConstructor<MpTcpSchedulerBlest> ()
  ;
  return tid;
}

MpTcpSchedulerBlest::MpTcpSchedulerBlest ()
  : MpTcpScheduler (),
    m_metaSock (0)
{
  NS_LOG_FUNCTION (this);
}

MpTcpSchedulerBlest::~MpTcpSchedulerBlest ()
{
  NS_LOG_FUNCTION (this);
}

void
MpTcpSchedulerBlest::SetMeta (Ptr<MpTcpSocketBase> metaSock)
{
  NS_ASSERT (metaSock);
  m_metaSock = metaSock;
}

Ptr<MpTcpSubflow>
MpTcpSchedulerBlest::GetSubflowToUseForEmptyPacket ()
{
  NS_ASSERT (m_metaSock->GetNActiveSubflows () > 0);
  return m_metaSock->GetSubflow (0);
}

bool
MpTcpSchedulerBlest::IsSlowSubflowBlocked (int fastId, int slowId)
{
  Ptr<MpTcpSubflow> fastSf = m_metaSock->GetSubflow (fastId);
  Ptr<MpTcpSubflow> slowSf = m_metaSock->GetSubflow (slowId);

  // If slow subflow hasn't transmitted yet, never block it (allow initial RTT probe)
  if (slowSf->m_tcb->m_lastRtt.Get () == Seconds (0.0))
    {
      return false;
    }

  Time fastRtt = fastSf->m_rtt->GetEstimate ();
  Time slowRtt = slowSf->m_rtt->GetEstimate ();

  if (slowRtt <= fastRtt)
    {
      return false;
    }

  double fastRate = EstimateRate (fastId);
  double fastQueueDelay = (double)fastSf->BytesInFlight () / fastRate;
  double fastDeliveryTime = fastRtt.GetSeconds () + fastQueueDelay;
  double slowDeliveryTime = slowRtt.GetSeconds ();

  // BLEST paper: block slow subflow if it would arrive later than fast subflow pipeline
  if (slowDeliveryTime > fastDeliveryTime && fastSf->AvailableWindow () > 0)
    {
      return true;
    }
  return false;
}

double
MpTcpSchedulerBlest::EstimateRate (int subflowId)
{
  Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (subflowId);
  if (!sf) return 500000.0;

  Ptr<Node> node = sf->GetNode ();
  if (node && (uint32_t)subflowId < node->GetNDevices ())
    {
      Ptr<NetDevice> dev = node->GetDevice (subflowId);
      DataRateValue dv;
      if (dev && dev->GetAttributeFailSafe ("DataRate", dv))
        {
          double physicalRate = dv.Get ().GetBitRate () / 8.0;
          return std::max (10000.0, physicalRate);
        }
    }

  double rttSec = sf->m_rtt->GetEstimate ().GetSeconds ();
  if (rttSec <= 0.001) rttSec = 0.01;
  uint32_t cwnd = sf->m_tcb->m_cWnd.Get ();
  if (cwnd == 0) cwnd = sf->GetSegSize ();
  return std::max (10000.0, (double)cwnd / rttSec);
}

int
MpTcpSchedulerBlest::SelectSubflow ()
{
  int nSubflows = m_metaSock->GetNActiveSubflows ();
  if (nSubflows == 0) return -1;

  // 1. Probe uninitialized subflows first
  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      if (sf && sf->AvailableWindow () > 0 && sf->m_tcb->m_lastRtt.Get () == Seconds (0.0))
        {
          return i;
        }
    }

  // Find fastest subflow
  int fastestId = -1;
  Time lowestRtt = Time::Max ();
  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      if (sf && sf->AvailableWindow () > 0 && sf->m_rtt->GetEstimate () < lowestRtt)
        {
          lowestRtt = sf->m_rtt->GetEstimate ();
          fastestId = i;
        }
    }

  if (fastestId < 0) return -1;

  // BLEST heuristic:
  // Dispatches to the fastest subflow by default.
  // When fastest subflow has pipeline backlog, evaluates secondary subflows:
  Ptr<MpTcpSubflow> fastSf = m_metaSock->GetSubflow (fastestId);
  double fastRate = EstimateRate (fastestId);
  double fastPipelineTime = ((double)fastSf->BytesInFlight () + fastSf->GetSegSize ()) / fastRate;

  int bestSecondaryId = -1;
  double minSecondaryPipelineTime = 1e12;

  for (int i = 0; i < nSubflows; ++i)
    {
      if (i == fastestId) continue;
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      if (sf && sf->AvailableWindow () > 0 && !IsSlowSubflowBlocked (fastestId, i))
        {
          double slowRate = EstimateRate (i);
          double slowPipelineTime = ((double)sf->BytesInFlight () + sf->GetSegSize ()) / slowRate;
          if (slowPipelineTime < minSecondaryPipelineTime)
            {
              minSecondaryPipelineTime = slowPipelineTime;
              bestSecondaryId = i;
            }
        }
    }

  if (bestSecondaryId >= 0 && minSecondaryPipelineTime <= fastPipelineTime)
    {
      return bestSecondaryId; // Non-blocking aggregation on secondary path
    }

  if (fastSf->AvailableWindow () > 0)
    {
      return fastestId;
    }

  if (bestSecondaryId >= 0)
    {
      return bestSecondaryId;
    }

  return -1; // Wait for faster subflow
}

bool
MpTcpSchedulerBlest::GenerateMapping (int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_metaSock);

  SequenceNumber32 metaNextTxSeq = m_metaSock->Getvalue_nextTxSeq ();
  uint32_t amountOfDataToSend = m_metaSock->m_txBuffer->SizeFromSequence (metaNextTxSeq);
  uint32_t metaWindow = m_metaSock->AvailableWindow ();

  if (amountOfDataToSend <= 0 || metaWindow <= 0)
    {
      return false;
    }

  int id = SelectSubflow ();
  if (id < 0)
    {
      return false;
    }

  Ptr<MpTcpSubflow> subflow = m_metaSock->GetSubflow (id);
  uint32_t subflowWindow = subflow->AvailableWindow ();
  uint32_t canSend = std::min (subflowWindow, metaWindow);

  if (canSend > 0)
    {
      activeSubflowArrayId = id;
      dsn = SEQ32TO64 (metaNextTxSeq);
      canSend = std::min (canSend, amountOfDataToSend);
      length = std::min (canSend, subflow->GetSegSize ());
      return true;
    }

  return false;
}

} // namespace ns3
