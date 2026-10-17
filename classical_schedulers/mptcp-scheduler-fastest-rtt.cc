/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2015 University of Sussex
 * Copyright (c) 2015 Université Pierre et Marie Curie (UPMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:  Kashif Nadeem <kshfnadeem@gmail.com>
 *          Matthieu Coudron <matthieu.coudron@lip6.fr>
 *          Morteza Kheirkhah <m.kheirkhah@sussex.ac.uk>
 */
#include "ns3/mptcp-scheduler-fastest-rtt.h"
#include "ns3/mptcp-subflow.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("MpTcpSchedulerFastestRTT");

namespace ns3
{

static inline
SequenceNumber64 SEQ32TO64(SequenceNumber32 seq)
{
  return SequenceNumber64( seq.GetValue());
}

TypeId
MpTcpSchedulerFastestRTT::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpTcpSchedulerFastestRTT")
    .SetParent<MpTcpScheduler> ()
    .AddConstructor<MpTcpSchedulerFastestRTT> ()
  ;

  return tid;
}

MpTcpSchedulerFastestRTT::MpTcpSchedulerFastestRTT()
  : MpTcpScheduler(),
    m_metaSock(0)
{
  NS_LOG_FUNCTION(this);
}

MpTcpSchedulerFastestRTT::~MpTcpSchedulerFastestRTT (void)
{
  NS_LOG_FUNCTION(this);
}

void
MpTcpSchedulerFastestRTT::SetMeta(Ptr<MpTcpSocketBase> metaSock)
{
  NS_ASSERT(metaSock);
  NS_ASSERT_MSG(m_metaSock == 0, "SetMeta already called");
  m_metaSock = metaSock;
}

double
MpTcpSchedulerFastestRTT::EstimateRate (int subflowId)
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
MpTcpSchedulerFastestRTT::FindFastestSubflowWithFreeWindow()
{
  NS_LOG_FUNCTION_NOARGS();
  int nSubflows = m_metaSock->GetNActiveSubflows();
  if (nSubflows <= 0) return -1;

  // 1. Probe uninitialized subflows first so they get an initial RTT estimate
  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow(i);
      if (sf && sf->AvailableWindow() > 0 && sf->m_tcb->m_lastRtt.Get() == Seconds(0.0))
        {
          return i;
        }
    }

  // 2. Classical minRTT: Select subflow with lowest smoothed RTT among those with free window
  int bestId = -1;
  Time minRtt = Time::Max ();

  for (int i = 0; i < nSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow(i);
      if (!sf || sf->AvailableWindow() == 0) continue;

      Time rtt = sf->m_rtt->GetEstimate ();
      if (rtt < minRtt)
        {
          minRtt = rtt;
          bestId = i;
        }
    }

  return bestId;
}

Ptr<MpTcpSubflow>
MpTcpSchedulerFastestRTT::GetSubflowToUseForEmptyPacket()
{
  NS_ASSERT(m_metaSock->GetNActiveSubflows() > 0 );
  return  m_metaSock->GetSubflow(0);
}

bool
MpTcpSchedulerFastestRTT::GenerateMapping(int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
{
  NS_LOG_FUNCTION(this);
  NS_ASSERT(m_metaSock);

  uint32_t amountOfDataToSend = 0;
  //! Tx data not sent to subflows yet
  SequenceNumber32 metaNextTxSeq = m_metaSock->Getvalue_nextTxSeq();
  amountOfDataToSend = m_metaSock->m_txBuffer->SizeFromSequence( metaNextTxSeq );
  uint32_t metaWindow = m_metaSock->AvailableWindow();
  if (amountOfDataToSend <= 0 || metaWindow <= 0)
    {
      return false;
    }

  int id = FindFastestSubflowWithFreeWindow();
  if(id < 0)
    {
      NS_LOG_DEBUG("No valid subflow");
      return false;
    }
  Ptr<MpTcpSubflow> subflow = m_metaSock->GetSubflow(id);
  uint32_t subflowWindow = subflow->AvailableWindow();
  uint32_t canSend = std::min( subflowWindow, metaWindow);

  if (canSend > 0)
    {
      activeSubflowArrayId = id;
      dsn = SEQ32TO64(metaNextTxSeq);
      canSend = std::min(canSend, amountOfDataToSend);
      length = std::min(canSend, (uint32_t)subflow->GetSegSize());
      return true;
    }
  return false;
}

} // namespace ns3
