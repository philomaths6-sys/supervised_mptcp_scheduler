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

#include "ns3/mptcp-scheduler-round-robin.h"
#include "ns3/mptcp-subflow.h"
#include "ns3/mptcp-socket-base.h"
#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("MpTcpSchedulerRoundRobin");

namespace ns3
{

static inline
SequenceNumber64 SEQ32TO64(SequenceNumber32 seq)
{
  return SequenceNumber64( seq.GetValue());
}

TypeId
MpTcpSchedulerRoundRobin::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpTcpSchedulerRoundRobin")
    .SetParent<MpTcpScheduler> ()
    .AddConstructor<MpTcpSchedulerRoundRobin> ()
  ;
  return tid;
}

MpTcpSchedulerRoundRobin::MpTcpSchedulerRoundRobin() :
  MpTcpScheduler(),
  m_lastUsedFlowId(0),
  m_metaSock(0)
{
  NS_LOG_FUNCTION(this);
}

MpTcpSchedulerRoundRobin::~MpTcpSchedulerRoundRobin (void)
{
  NS_LOG_FUNCTION(this);
}

void
MpTcpSchedulerRoundRobin::SetMeta(Ptr<MpTcpSocketBase> metaSock)
{
  NS_ASSERT(metaSock);
  NS_ASSERT_MSG(m_metaSock == 0, "SetMeta already called");
  m_metaSock = metaSock;
}

Ptr<MpTcpSubflow>
MpTcpSchedulerRoundRobin::GetSubflowToUseForEmptyPacket()
{
  if (!m_metaSock || m_metaSock->GetNActiveSubflows() == 0)
    {
      return 0;
    }
  return m_metaSock->GetSubflow(0);
}

/* Capacity-Proportional Deficit Round-Robin (CP-DRR) Scheduler */
bool
MpTcpSchedulerRoundRobin::GenerateMapping(int& activeSubflowArrayId, SequenceNumber64& dsn, uint16_t& length)
{
  NS_LOG_FUNCTION(this);
  if (!m_metaSock) return false;

  int nbOfSubflows = m_metaSock->GetNActiveSubflows();
  if (nbOfSubflows <= 0) return false;

  SequenceNumber32 metaNextTxSeq = m_metaSock->Getvalue_nextTxSeq(); 
  uint32_t amountOfDataToSend = m_metaSock->m_txBuffer->SizeFromSequence( metaNextTxSeq );
  uint32_t metaWindow = m_metaSock->AvailableWindow();

  if (amountOfDataToSend <= 0 || metaWindow <= 0)
    {
      return false;
    }

  // 1. Probe uninitialized subflows first so they get an RTT sample immediately
  for (int i = 0; i < nbOfSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow(i);
      if (sf && sf->AvailableWindow() > 0 && sf->m_tcb->m_lastRtt.Get() == Seconds(0.0))
        {
          activeSubflowArrayId = i;
          dsn = SEQ32TO64(metaNextTxSeq);
          uint32_t canSend = std::min((uint32_t)sf->AvailableWindow(), metaWindow);
          canSend = std::min(canSend, amountOfDataToSend);
          length = std::min(canSend, (uint32_t)sf->GetSegSize());
          return true;
        }
    }

  // 2. Compute dynamic subflow rates and weights
  std::vector<double> rates(nbOfSubflows, 1.0);
  double minRate = 1e12;
  for (int i = 0; i < nbOfSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow(i);
      if (sf)
        {
          double rate = 500000.0;
          Ptr<Node> node = sf->GetNode ();
          if (node && (uint32_t)i < node->GetNDevices ())
            {
              Ptr<NetDevice> dev = node->GetDevice (i);
              DataRateValue dv;
              if (dev && dev->GetAttributeFailSafe ("DataRate", dv))
                {
                  double physicalRate = dv.Get ().GetBitRate () / 8.0;
                  rate = std::max (10000.0, physicalRate);
                }
            }
          rates[i] = rate;
          if (rates[i] < minRate) minRate = rates[i];
        }
    }
  if (minRate <= 0.0) minRate = 1.0;

  // Replenish deficits if all active subflow deficits are <= 0
  bool anyDeficitPositive = false;
  for (int i = 0; i < nbOfSubflows; ++i)
    {
      Ptr<MpTcpSubflow> sf = m_metaSock->GetSubflow(i);
      if (sf && sf->AvailableWindow() > 0 && m_deficits[i] > 0)
        {
          anyDeficitPositive = true;
          break;
        }
    }

  if (!anyDeficitPositive)
    {
      for (int i = 0; i < nbOfSubflows; ++i)
        {
          int weight = std::max(1, (int)std::round(rates[i] / minRate));
          if (weight > 20) weight = 20;
          m_deficits[i] = weight;
        }
    }

  // 3. Find subflow with deficit > 0 and available window
  int selectedId = -1;
  for (int attempt = 0; attempt < nbOfSubflows; ++attempt)
    {
      int fid = (m_lastUsedFlowId + attempt) % nbOfSubflows;
      Ptr<MpTcpSubflow> subflow = m_metaSock->GetSubflow(fid);
      if (subflow && subflow->AvailableWindow() > 0 && m_deficits[fid] > 0)
        {
          selectedId = fid;
          break;
        }
    }

  if (selectedId < 0)
    {
      return false; // Wait for subflow window to open to preserve bandwidth-proportional pacing
    }

  if (selectedId >= 0)
    {
      Ptr<MpTcpSubflow> subflow = m_metaSock->GetSubflow(selectedId);
      uint32_t subflowWindow = subflow->AvailableWindow();
      uint32_t canSend = std::min(subflowWindow, metaWindow);
      if (canSend > 0)
        {
          activeSubflowArrayId = selectedId;
          dsn = SEQ32TO64(metaNextTxSeq);
          canSend = std::min(canSend, amountOfDataToSend);
          length = std::min(canSend, (uint32_t)subflow->GetSegSize());
          m_deficits[selectedId]--;
          if (m_deficits[selectedId] <= 0)
            {
              m_lastUsedFlowId = (selectedId + 1) % nbOfSubflows;
            }
          return true;
        }
    }

  return false;
}

} // end of 'ns3'
