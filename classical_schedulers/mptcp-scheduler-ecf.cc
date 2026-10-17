#include "mptcp-scheduler-ecf.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/mptcp-subflow.h"
#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("MpTcpSchedulerEcf");
NS_OBJECT_ENSURE_REGISTERED (MpTcpSchedulerEcf);

static inline SequenceNumber64 SEQ32TO64(SequenceNumber32 seq)
{
  return SequenceNumber64 (seq.GetValue ());
}

TypeId
MpTcpSchedulerEcf::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpTcpSchedulerEcf")
    .SetParent<MpTcpScheduler> ()
    .AddConstructor<MpTcpSchedulerEcf> ()
  ;
  return tid;
}

MpTcpSchedulerEcf::MpTcpSchedulerEcf ()
  : MpTcpScheduler (),
    m_metaSock (0)
{
  NS_LOG_FUNCTION (this);
}

MpTcpSchedulerEcf::~MpTcpSchedulerEcf ()
{
  NS_LOG_FUNCTION (this);
}

void
MpTcpSchedulerEcf::SetMeta (Ptr<MpTcpSocketBase> metaSock)
{
  NS_ASSERT (metaSock);
  m_metaSock = metaSock;
}

Ptr<MpTcpSubflow>
MpTcpSchedulerEcf::GetSubflowToUseForEmptyPacket ()
{
  NS_ASSERT (m_metaSock->GetNActiveSubflows () > 0);
  return m_metaSock->GetSubflow (0);
}

double
MpTcpSchedulerEcf::EstimateRate (int subflowId)
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

double
MpTcpSchedulerEcf::EstimateCompletionTime (int subflowId)
{
  Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (subflowId);
  if (!sf) return 1e9;
  if (sf->m_tcb->m_lastRtt.Get () == Seconds (0.0))
    {
      return 0.0001; // Probe uninitialized subflow first
    }

  double estRate = EstimateRate (subflowId);
  uint32_t segSize = sf->GetSegSize ();
  if (segSize == 0) segSize = 536;

  double rttSec = sf->m_rtt->GetEstimate ().GetSeconds ();
  if (rttSec <= 0.001) rttSec = 0.02;
  double queueTime = (double)sf->BytesInFlight () / estRate;
  double transTime = (double)segSize / estRate;

  return rttSec + queueTime + transTime;
}

int
MpTcpSchedulerEcf::SelectSubflow ()
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

  int bestId = -1;
  double minCompletionTime = 1e9;

  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      if (sf && sf->AvailableWindow () > 0)
        {
          double ect = EstimateCompletionTime (i);
          if (ect < minCompletionTime)
            {
              minCompletionTime = ect;
              bestId = i;
            }
        }
    }

  return bestId;
}

bool
MpTcpSchedulerEcf::GenerateMapping (int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
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
