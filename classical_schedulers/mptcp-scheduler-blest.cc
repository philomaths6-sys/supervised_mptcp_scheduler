#include "mptcp-scheduler-blest.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/mptcp-subflow.h"
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

  Time fastRtt = fastSf->m_rtt->GetEstimate ();
  Time slowRtt = slowSf->m_rtt->GetEstimate ();

  // If slow RTT is significantly larger than fast RTT, sending on slow subflow
  // might arrive later than waiting for fast subflow CWND to open up.
  // BLEST Condition: RTT_slow > 2 * RTT_fast => penalty threshold
  if (slowRtt > 2 * fastRtt)
    {
      // Check in-flight data on fast subflow
      if (fastSf->AvailableWindow () == 0)
        {
          // Fast path CWND is currently full, but its packets will return an ACK
          // in less than (RTT_slow - RTT_fast) time.
          return true; // Predicts HoL blocking: do NOT schedule on slow path
        }
    }
  return false;
}

int
MpTcpSchedulerBlest::SelectSubflow ()
{
  int nSubflows = m_metaSock->GetNActiveSubflows ();
  if (nSubflows == 0) return -1;

  // Find fastest subflow
  int fastestId = -1;
  Time lowestRtt = Time::Max ();
  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      Time rtt = sf->m_rtt->GetEstimate ();
      if (rtt < lowestRtt)
        {
          lowestRtt = rtt;
          fastestId = i;
        }
    }

  if (fastestId >= 0 && m_metaSock->GetSubflow (fastestId)->AvailableWindow () > 0)
    {
      return fastestId;
    }

  // If fastest subflow window is full, check remaining subflows using BLEST heuristic
  for (int i = 0; i < nSubflows; ++i)
    {
      if (i == fastestId) continue;
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow (i);
      if (sf->AvailableWindow () > 0)
        {
          if (!IsSlowSubflowBlocked (fastestId, i))
            {
              return i; // Safe to use without causing HoL blocking
            }
          else
            {
              NS_LOG_DEBUG ("BLEST: Slow subflow " << i << " blocked to prevent HoL!");
            }
        }
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
