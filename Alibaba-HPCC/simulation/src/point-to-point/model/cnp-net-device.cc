/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) 2006 Georgia Tech Research Corporation, INRIA
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
* Author: Yuliang Li <yuliangli@g.harvard.com>
*/

#define __STDC_LIMIT_MACROS 1
#include <stdint.h>
#include <stdio.h>
#include "ns3/cnp-net-device.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/object-vector.h"
#include "ns3/pause-header.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/assert.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-header.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/qbb-channel.h"
#include "ns3/random-variable.h"
#include "ns3/flow-id-tag.h"
#include "ns3/qbb-header.h"
#include "ns3/error-model.h"
#include "cn-header.h"
#include "ns3/ppp-header.h"
#include "ns3/udp-header.h"
#include "ns3/seq-ts-header.h"
#include "ns3/pointer.h"
#include "ns3/custom-header.h"

#include <iostream>

NS_LOG_COMPONENT_DEFINE("cnpNetDevice");
// cnpNetDevice既表示网卡，也表示交换机

namespace ns3 {
	


	/******************
	 * cnpNetDevice
	 *****************/
	NS_OBJECT_ENSURE_REGISTERED(cnpNetDevice);

	TypeId
		cnpNetDevice::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::cnpNetDevice")
			.SetParent<PointToPointNetDevice>()
			.AddConstructor<cnpNetDevice>()
			.AddAttribute("cnpEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(true),
				MakeBooleanAccessor(&cnpNetDevice::m_cnpEnabled),
				MakeBooleanChecker())
			.AddAttribute("QcnEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(false),
				MakeBooleanAccessor(&cnpNetDevice::m_qcnEnabled),
				MakeBooleanChecker())
			.AddAttribute("DynamicThreshold",
				"Enable dynamic threshold.",
				BooleanValue(false),
				MakeBooleanAccessor(&cnpNetDevice::m_dynamicth),
				MakeBooleanChecker())
			.AddAttribute("PauseTime",
				"Number of microseconds to pause upon congestion",
				UintegerValue(5),
				MakeUintegerAccessor(&cnpNetDevice::m_pausetime),
				MakeUintegerChecker<uint32_t>())
			.AddAttribute ("TxBeQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&cnpNetDevice::m_queue),
					MakePointerChecker<Queue> ())
			.AddAttribute ("RdmaEgressQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&cnpNetDevice::m_rdmaEQ),
					MakePointerChecker<Object> ())
			.AddTraceSource ("cnpEnqueue", "Enqueue a packet in the cnpNetDevice.",
					MakeTraceSourceAccessor (&cnpNetDevice::m_traceEnqueue))
			.AddTraceSource ("cnpDequeue", "Dequeue a packet in the cnpNetDevice.",
					MakeTraceSourceAccessor (&cnpNetDevice::m_traceDequeue))
			.AddTraceSource ("cnpDrop", "Drop a packet in the cnpNetDevice.",
					MakeTraceSourceAccessor (&cnpNetDevice::m_traceDrop))
			.AddTraceSource ("RdmaQpDequeue", "A qp dequeue a packet.",
					MakeTraceSourceAccessor (&cnpNetDevice::m_traceQpDequeue))
			.AddTraceSource ("cnpPfc", "get a PFC packet. 0: resume, 1: pause",
					MakeTraceSourceAccessor (&cnpNetDevice::m_tracePfc))
			;

		return tid;
	}

	cnpNetDevice::cnpNetDevice()
	{
		NS_LOG_FUNCTION(this);
		m_ecn_source = new std::vector<ECNAccount>;
		for (uint32_t i = 0; i < qCnt; i++){
			m_paused[i] = false;
		}

		m_rdmaEQ = CreateObject<RdmaEgressQueue>();
	}

	cnpNetDevice::~cnpNetDevice()
	{
		NS_LOG_FUNCTION(this);
	}

	void
		cnpNetDevice::DoDispose()
	{
		NS_LOG_FUNCTION(this);

		PointToPointNetDevice::DoDispose();
	}

	void
		cnpNetDevice::TransmitComplete(void)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
		m_txMachineState = READY;
		NS_ASSERT_MSG(m_currentPkt != 0, "cnpNetDevice::TransmitComplete(): m_currentPkt zero");
		m_phyTxEndTrace(m_currentPkt);
		m_currentPkt = 0;
		DequeueAndTransmit();
	}

	/**
 * @brief 从队列中取出并发送数据包
 *
 * 如果链路未连接，则返回。如果发送通道忙，则返回。
 * 根据节点类型，从队列中取出数据包并发送。
 * 如果节点类型为0，则从RDMA事件队列中获取数据包并发送；
 * 如果节点类型为交换机，则从普通队列中取出数据包并发送。
 *
 * @return 无返回值
 */
	void
		cnpNetDevice::DequeueAndTransmit(void)
	{
		NS_LOG_FUNCTION(this);
		if (!m_linkUp) return; // if link is down, return
		if (m_txMachineState == BUSY) return;	// Quit if channel busy
		Ptr<Packet> p;
		if (m_node->GetNodeType() == 0){
			int qIndex = m_rdmaEQ->GetNextQindex(m_paused);
			if (qIndex != -1024){
				if (qIndex == -1){ // high prio
					p = m_rdmaEQ->DequeueQindex(qIndex);
					m_traceDequeue(p, 0);
					TransmitStart(p);
					return;
				}
				// a qp dequeue a packet
				Ptr<RdmaQueuePair> lastQp = m_rdmaEQ->GetQp(qIndex);
				p = m_rdmaEQ->DequeueQindex(qIndex);

				// transmit
				m_traceQpDequeue(p, lastQp);
				TransmitStart(p);
				// update for the next avail time
				m_rdmaPktSent(lastQp, p, m_tInterframeGap);
			}else { // no packet to send
				NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
				Time t = Simulator::GetMaximumSimulationTime();
				for (uint32_t i = 0; i < m_rdmaEQ->GetFlowCount(); i++){
					Ptr<RdmaQueuePair> qp = m_rdmaEQ->GetQp(i);
					if (qp->GetBytesLeft() == 0)
						continue;
					t = Min(qp->m_nextAvail, t);
				}
				if (m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() && t > Simulator::Now()){
					m_nextSend = Simulator::Schedule(t - Simulator::Now(), &cnpNetDevice::DequeueAndTransmit, this);
				}
			}
			return;
		}else{   //switch, doesn't care about qcn, just send
			// cnp-net-device便是Themis中的External Switch，在收到CNP后会对target flow做一个减速
			p = m_queue->DequeueRR(m_paused);		//this is round-robin
			if (p != 0){
				//检查p是否符合m_cnp_handler中的条件，如果符合则更新seq，并放到队尾
				CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
				ch.getInt = 1;
				p->PeekHeader(ch);
				for(auto &cnp : m_cnp_handler){
					// CNP_Handler这个类的port指的是
					if (cnp.port == ch.udp.sport && cnp.sip == ch.dip && cnp.qIndex == ch.udp.pg){
                        if(!cnp.finished){
							//第一个包第一次
							if(cnp.first == 0){
								cnp.first = ch.udp.seq;
								uint64_t bytesInQueue=m_queue->GetNBytes(cnp.qIndex);
								cnp.delay = ns3::NanoSeconds(bytesInQueue * 80 *cnp.n);
								cnp.n--;
								//p重新入队
								m_queue->Enqueue(p,ch.udp.pg);
								return;
							}
							//第一个包其他次
							else if(cnp.first == ch.udp.seq){
								if(cnp.n==0){
									cnp.finished = true;
									//现在时间+delay-55微秒
									int64_t nowMicroSeconds = ns3::Simulator::Now().GetMicroSeconds();
									int64_t recTimeMicroSeconds = cnp.rec_time.GetMicroSeconds();
									int64_t newRecTimeMicroSeconds = nowMicroSeconds + recTimeMicroSeconds - 55;
									cnp.rec_time = ns3::MicroSeconds(newRecTimeMicroSeconds);
								}
								else{
									cnp.n--;
									m_queue->Enqueue(p,ch.udp.pg);
									return;
								}
							}
							//其他包
							else{
								m_queue->Enqueue(p,ch.udp.pg);
								return;
							}
						}
					}
				}
				m_snifferTrace(p);
				m_promiscSnifferTrace(p);
				Ipv4Header h;
				Ptr<Packet> packet = p->Copy();
				uint16_t protocol = 0;
				ProcessHeader(packet, protocol);
				packet->RemoveHeader(h);
				FlowIdTag t;
				uint32_t qIndex = m_queue->GetLastQueue();
				CustomHeader ch2(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
				packet->PeekHeader(ch2);
				if(ch2.l3Prot == 0xFF){
					ReceiveCnp(packet,ch2);
				}
				if (qIndex == 0){//this is a pause or cnp, send it immediately!
					m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
					p->RemovePacketTag(t);
				}else{
					m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
					p->RemovePacketTag(t);
				}
				//m_node转为switchnode
				m_traceDequeue(p, qIndex);


				TransmitStart(p);
				return;
			}else{ //No queue can deliver any packet
				NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
				if (m_node->GetNodeType() == 0 && m_qcnEnabled){ //nothing to send, possibly due to qcn flow control, if so reschedule sending
					Time t = Simulator::GetMaximumSimulationTime();
					for (uint32_t i = 0; i < m_rdmaEQ->GetFlowCount(); i++){
						Ptr<RdmaQueuePair> qp = m_rdmaEQ->GetQp(i);
						if (qp->GetBytesLeft() == 0)
							continue;
						t = Min(qp->m_nextAvail, t);
					}
					if (m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() && t > Simulator::Now()){
						m_nextSend = Simulator::Schedule(t - Simulator::Now(), &cnpNetDevice::DequeueAndTransmit, this);
					}
				}
			}
		}
		return;
	}

int cnt = 0;
	void cnpNetDevice::ReceiveCnp(Ptr<Packet> p, CustomHeader &ch) {
		uint16_t qIndex = ch.ack.pg;
		uint16_t port = ch.ack.dport; // ns3中是使用ack打标记来模拟CNP包的，所以ack.dport就是cnp想要作用的发送端端口号
		uint16_t sip = ch.sip;
		//在m_cnp_handler中查
		for (auto &cnp : m_cnp_handler){
			if (cnp.port == port && cnp.sip == sip && cnp.qIndex == qIndex){
				//满速结束
				if(cnp.finished==true&&cnp.rec_time<=Simulator::Now()){
					cnp.first=0;
					cnp.finished=false;
				}
			}
		}
		CNP_Handler cnp;
		cnp.qIndex = qIndex;
		cnp.port = port;
		cnp.sip = sip;
		cnt++; std::cout << "cnt = " << cnt << "\n";
		m_cnp_handler.push_back(cnp);

		return;
	}

	void
		cnpNetDevice::Resume(unsigned qIndex)
	{
		NS_LOG_FUNCTION(this << qIndex);
		NS_ASSERT_MSG(m_paused[qIndex], "Must be PAUSEd");
		m_paused[qIndex] = false;
		NS_LOG_INFO("Node " << m_node->GetId() << " dev " << m_ifIndex << " queue " << qIndex <<
			" resumed at " << Simulator::Now().GetSeconds());
		DequeueAndTransmit();
	}

	void
		cnpNetDevice::Receive(Ptr<Packet> packet)
	{
		NS_LOG_FUNCTION(this << packet);
		if (!m_linkUp){
			m_traceDrop(packet, 0);
			return;
		}

		if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
		{
			// 
			// If we have an error model and it indicates that it is time to lose a
			// corrupted packet, don't forward this packet up, let it go.
			//
			m_phyRxDropTrace(packet);
			return;
		}

		m_macRxTrace(packet);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		ch.getInt = 1; // parse INT header
		packet->PeekHeader(ch);
		if (ch.l3Prot == 0xFE){ // PFC
			if (!m_cnpEnabled) return;
			unsigned qIndex = ch.pfc.qIndex;
			if (ch.pfc.time > 0){
				m_tracePfc(1); // 暂停
				m_paused[qIndex] = true;
			}else{
				m_tracePfc(0); // 继续
				Resume(qIndex);
			}
		}else { // non-PFC packets (data, ACK, NACK, CNP...)
			if (m_node->GetNodeType() > 0){ // switch
				packet->AddPacketTag(FlowIdTag(m_ifIndex));
				m_node->SwitchReceiveFromDevice(this, packet, ch);
			}else { // NIC
				// send to RdmaHw
				int ret = m_rdmaReceiveCb(packet, ch);
				// TODO we may based on the ret do something
			}
		}
		return;
	}

	bool cnpNetDevice::Send(Ptr<Packet> packet, const Address &dest, uint16_t protocolNumber)
	{
		NS_ASSERT_MSG(false, "cnpNetDevice::Send not implemented yet\n");
		return false;
	}

	bool cnpNetDevice::SwitchSend (uint32_t qIndex, Ptr<Packet> packet, CustomHeader &ch){
		m_macTxTrace(packet);
		m_traceEnqueue(packet, qIndex);
		m_queue->Enqueue(packet, qIndex);
		std::cout << "cnpNetDevice::SwitchSend\n";
		DequeueAndTransmit();
		return true;
	}
	//发送PFC
	void cnpNetDevice::SendPfc(uint32_t qIndex, uint32_t type){
		Ptr<Packet> p = Create<Packet>(0);
		PauseHeader pauseh((type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
		p->AddHeader(pauseh);
		Ipv4Header ipv4h;  // Prepare IPv4 header
		ipv4h.SetProtocol(0xFE);
		ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
		ipv4h.SetPayloadSize(p->GetSize());
		ipv4h.SetTtl(1);
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		p->AddHeader(ipv4h);
		AddHeader(p, 0x800);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		SwitchSend(0, p, ch);
	}
	void cnpNetDevice::SendCnp(Ptr<Packet> p, CustomHeader &ch){
		//发送CNP
		//新建包，设置l3Prot为0xFF，设置sip,dport,qIndex
		CnHeader seqh;
		if(ch.udp.sport==100)return;
		seqh.SetPG(ch.udp.pg);
		seqh.SetSport(ch.udp.dport);
		seqh.SetDport(ch.udp.sport);
		//std::cout << "CNP sent from " << m_node->GetId() << " to " << ch.sip << " port " << seqh.GetDport() << " pg "<< seqh.GetPG()<< std::endl;
		//Ipv4Address(ch.sip).Print(std::cout);
		Ptr<Packet> newp = Create<Packet>(std::max(60-14-20-(int)seqh.GetSerializedSize(), 0));
		newp->AddHeader(seqh);
		Ipv4Header ipv4h;	// Prepare IPv4 header
		ipv4h.SetDestination(Ipv4Address(ch.sip));
		//Source为当前设备
		//ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		ipv4h.SetSource(Ipv4Address(ch.dip));
		ipv4h.SetProtocol(0xFF); //ack=0xFC nack=0xFD cdn=0xFF
		ipv4h.SetTtl(64);
		ipv4h.SetPayloadSize(newp->GetSize());
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		//控制台输出ipv4h的信息
		newp->AddHeader(ipv4h);
		AddHeader(newp, 0x800);	// Attach PPP header
		// send
		CustomHeader ch2(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		newp->PeekHeader(ch2);
		//std::cout << "ch2 " << ch2.cnp.dport << std::endl;
	
		SwitchSend(0, newp, ch2);
		//终端打印CNP
	}
	bool
		cnpNetDevice::Attach(Ptr<QbbChannel> ch)
	{
		NS_LOG_FUNCTION(this << &ch);
		m_channel = ch;
		m_channel->Attach(this);
		NotifyLinkUp();
		return true;
	}

	bool
		cnpNetDevice::TransmitStart(Ptr<Packet> p)
	{
		NS_LOG_FUNCTION(this << p);
		NS_LOG_LOGIC("UID is " << p->GetUid() << ")");
		//
		// This function is called to start the process of transmitting a packet.
		// We need to tell the channel that we've started wiggling the wire and
		// schedule an event that will be executed when the transmission is complete.
		//
		NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");
		m_txMachineState = BUSY;
		m_currentPkt = p;
		m_phyTxBeginTrace(m_currentPkt);
		Time txTime = Seconds(m_bps.CalculateTxTime(p->GetSize()));
		Time txCompleteTime = txTime + m_tInterframeGap;
		NS_LOG_LOGIC("Schedule TransmitCompleteEvent in " << txCompleteTime.GetSeconds() << "sec");
		Simulator::Schedule(txCompleteTime, &cnpNetDevice::TransmitComplete, this);

		bool result = m_channel->TransmitStart(p, this, txTime);
		if (result == false)
		{
			m_phyTxDropTrace(p);
		}
		return result;
	}

	Ptr<Channel>
		cnpNetDevice::GetChannel(void) const
	{
		return m_channel;
	}

   bool cnpNetDevice::Iscnp(void) const{
	   return true;
   }

   void cnpNetDevice::NewQp(Ptr<RdmaQueuePair> qp){
	   qp->m_nextAvail = Simulator::Now();
	   DequeueAndTransmit();
   }
   void cnpNetDevice::ReassignedQp(Ptr<RdmaQueuePair> qp){
	   DequeueAndTransmit();
   }
   void cnpNetDevice::TriggerTransmit(void){
	   DequeueAndTransmit();
   }

	void cnpNetDevice::SetQueue(Ptr<BEgressQueue> q){
		NS_LOG_FUNCTION(this << q);
		m_queue = q;
	}

	Ptr<BEgressQueue> cnpNetDevice::GetQueue(){
		return m_queue;
	}


	void cnpNetDevice::RdmaEnqueueHighPrioQ(Ptr<Packet> p){
		m_traceEnqueue(p, 0);
		m_rdmaEQ->EnqueueHighPrioQ(p);
	}

	void cnpNetDevice::TakeDown(){
		// TODO: delete packets in the queue, set link down
		if (m_node->GetNodeType() == 0){
			// clean the high prio queue
			m_rdmaEQ->CleanHighPrio(m_traceDrop);
			// notify driver/RdmaHw that this link is down
			m_rdmaLinkDownCb(this);
		}else { // switch
			// clean the queue
			for (uint32_t i = 0; i < qCnt; i++)
				m_paused[i] = false;
			while (1){
				Ptr<Packet> p = m_queue->DequeueRR(m_paused);
				if (p == 0)
					 break;
				m_traceDrop(p, m_queue->GetLastQueue());
			}
			// TODO: Notify switch that this link is down
		}
		m_linkUp = false;
	}

	void cnpNetDevice::UpdateNextAvail(Time t){
		if (!m_nextSend.IsExpired() && t < m_nextSend.GetTs()){
			Simulator::Cancel(m_nextSend);
			Time delta = t < Simulator::Now() ? Time(0) : t - Simulator::Now();
			m_nextSend = Simulator::Schedule(delta, &cnpNetDevice::DequeueAndTransmit, this);
		}
	}
} // namespace ns3
