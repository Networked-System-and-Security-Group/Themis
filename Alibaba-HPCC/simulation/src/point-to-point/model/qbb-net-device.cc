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
#include <fstream>
#include "ns3/qbb-net-device.h"
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
#include "ns3/cn-header.h"
#include "ns3/ppp-header.h"
#include "ns3/udp-header.h"
#include "ns3/seq-ts-header.h"
#include "ns3/pointer.h"
#include "ns3/custom-header.h"

#include <iostream>

NS_LOG_COMPONENT_DEFINE("QbbNetDevice");
// QbbNetDevice既表示网卡，也表示交换机

namespace ns3 {
	
	uint32_t RdmaEgressQueue::ack_q_idx = 3;
	// RdmaEgressQueue
	TypeId RdmaEgressQueue::GetTypeId (void)
	{
		static TypeId tid = TypeId ("ns3::RdmaEgressQueue")
			.SetParent<Object> ()
			.AddTraceSource ("RdmaEnqueue", "Enqueue a packet in the RdmaEgressQueue.",
					MakeTraceSourceAccessor (&RdmaEgressQueue::m_traceRdmaEnqueue))
			.AddTraceSource ("RdmaDequeue", "Dequeue a packet in the RdmaEgressQueue.",
					MakeTraceSourceAccessor (&RdmaEgressQueue::m_traceRdmaDequeue))
			;
		return tid;
	}

	RdmaEgressQueue::RdmaEgressQueue(){
		m_rrlast = 0;
		m_qlast = 0;
		m_ackQ = CreateObject<DropTailQueue>();
		m_ackQ->SetAttribute("MaxBytes", UintegerValue(0xffffffff)); // queue limit is on a higher level, not here
	}

	Ptr<Packet> RdmaEgressQueue::DequeueQindex(int qIndex){
		if (qIndex == -1){ // high prio
			Ptr<Packet> p = m_ackQ->Dequeue();
			m_qlast = -1;
			m_traceRdmaDequeue(p, 0);
			return p;
		}
		if (qIndex >= 0){ // qp
			Ptr<Packet> p = m_rdmaGetNxtPkt(m_qpGrp->Get(qIndex));
			m_rrlast = qIndex;
			m_qlast = qIndex;
			m_traceRdmaDequeue(p, m_qpGrp->Get(qIndex)->m_pg);
			return p;
		}
		return 0;
	}
	int RdmaEgressQueue::GetNextQindex(bool paused[]){
		bool found = false;
		uint32_t qIndex;
		if (!paused[ack_q_idx] && m_ackQ->GetNPackets() > 0)
			return -1;

		// no pkt in highest priority queue, do rr for each qp
		int res = -1024;
		uint32_t fcount = m_qpGrp->GetN();
		uint32_t min_finish_id = 0xffffffff;
		for (qIndex = 1; qIndex <= fcount; qIndex++){
			uint32_t idx = (qIndex + m_rrlast) % fcount;
			Ptr<RdmaQueuePair> qp = m_qpGrp->Get(idx);
			if (!paused[qp->m_pg] && qp->GetBytesLeft() > 0 && !qp->IsWinBound()){
				if (m_qpGrp->Get(idx)->m_nextAvail.GetTimeStep() > Simulator::Now().GetTimeStep()) //not available now
					continue;
				res = idx;
				break;
			}else if (qp->IsFinished()){
				min_finish_id = idx < min_finish_id ? idx : min_finish_id;
			}
		}

		// clear the finished qp
		if (min_finish_id < 0xffffffff){
			int nxt = min_finish_id;
			auto &qps = m_qpGrp->m_qps;
			for (int i = min_finish_id + 1; i < fcount; i++) if (!qps[i]->IsFinished()){
				if (i == res) // update res to the idx after removing finished qp
					res = nxt;
				qps[nxt] = qps[i];
				nxt++;
			}
			qps.resize(nxt);
		}
		return res;
	}

	int RdmaEgressQueue::GetLastQueue(){
		return m_qlast;
	}

	uint32_t RdmaEgressQueue::GetNBytes(uint32_t qIndex){
		NS_ASSERT_MSG(qIndex < m_qpGrp->GetN(), "RdmaEgressQueue::GetNBytes: qIndex >= m_qpGrp->GetN()");
		return m_qpGrp->Get(qIndex)->GetBytesLeft();
	}

	uint32_t RdmaEgressQueue::GetFlowCount(void){
		return m_qpGrp->GetN();
	}

	Ptr<RdmaQueuePair> RdmaEgressQueue::GetQp(uint32_t i){
		return m_qpGrp->Get(i);
	}
 
	void RdmaEgressQueue::RecoverQueue(uint32_t i){
		NS_ASSERT_MSG(i < m_qpGrp->GetN(), "RdmaEgressQueue::RecoverQueue: qIndex >= m_qpGrp->GetN()");
		m_qpGrp->Get(i)->snd_nxt = m_qpGrp->Get(i)->snd_una;
	}

	void RdmaEgressQueue::EnqueueHighPrioQ(Ptr<Packet> p){
		m_traceRdmaEnqueue(p, 0);
		m_ackQ->Enqueue(p);
	}

	void RdmaEgressQueue::CleanHighPrio(TracedCallback<Ptr<const Packet>, uint32_t> dropCb){
		while (m_ackQ->GetNPackets() > 0){
			Ptr<Packet> p = m_ackQ->Dequeue();
			dropCb(p, 0);
		}
	}

	/******************
	 * QbbNetDevice
	 *****************/
	NS_OBJECT_ENSURE_REGISTERED(QbbNetDevice);

	TypeId
		QbbNetDevice::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::QbbNetDevice")
			.SetParent<PointToPointNetDevice>()
			.AddConstructor<QbbNetDevice>()
			.AddAttribute("QbbEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(true),
				MakeBooleanAccessor(&QbbNetDevice::m_qbbEnabled),
				MakeBooleanChecker())
			.AddAttribute("QcnEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(false),
				MakeBooleanAccessor(&QbbNetDevice::m_qcnEnabled),
				MakeBooleanChecker())
			.AddAttribute("DynamicThreshold",
				"Enable dynamic threshold.",
				BooleanValue(false),
				MakeBooleanAccessor(&QbbNetDevice::m_dynamicth),
				MakeBooleanChecker())
			.AddAttribute("PauseTime",
				"Number of microseconds to pause upon congestion",
				UintegerValue(5),
				MakeUintegerAccessor(&QbbNetDevice::m_pausetime),
				MakeUintegerChecker<uint32_t>())
			.AddAttribute ("TxBeQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&QbbNetDevice::m_queue),
					MakePointerChecker<Queue> ())
			.AddAttribute ("RdmaEgressQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&QbbNetDevice::m_rdmaEQ),
					MakePointerChecker<Object> ())
			.AddTraceSource ("QbbEnqueue", "Enqueue a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceEnqueue))
			.AddTraceSource ("QbbDequeue", "Dequeue a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceDequeue))
			.AddTraceSource ("QbbDrop", "Drop a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceDrop))
			.AddTraceSource ("RdmaQpDequeue", "A qp dequeue a packet.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceQpDequeue))
			.AddTraceSource ("QbbPfc", "get a PFC packet. 0: resume, 1: pause",
					MakeTraceSourceAccessor (&QbbNetDevice::m_tracePfc))
			;

		return tid;
	}

	QbbNetDevice::QbbNetDevice()
	{
		NS_LOG_FUNCTION(this);
		m_ecn_source = new std::vector<ECNAccount>;
		for (uint32_t i = 0; i < qCnt; i++){
			m_paused[i] = false;
		}

		m_rdmaEQ = CreateObject<RdmaEgressQueue>();
	}

	QbbNetDevice::~QbbNetDevice()
	{
		NS_LOG_FUNCTION(this);
	}

	void
		QbbNetDevice::DoDispose()
	{
		NS_LOG_FUNCTION(this);

		PointToPointNetDevice::DoDispose();
	}

	void
		QbbNetDevice::TransmitComplete(void)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
		m_txMachineState = READY;
		NS_ASSERT_MSG(m_currentPkt != 0, "QbbNetDevice::TransmitComplete(): m_currentPkt zero");
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
int first_num=0;
int finish_num=0;
void WriteToFile(std::string str){
	std::ofstream out("./output.txt",std::ios::app);
	out<<str;
	out.close();
}

	void
		QbbNetDevice::DequeueAndTransmit(void)
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
					m_nextSend = Simulator::Schedule(t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
				}
			}
			return;
		}else{   //switch, doesn't care about qcn, just send
			p = m_queue->DequeueRR(m_paused);		//this is round-robin
			if (p != 0){
				//检查p是否符合m_cnp_handler中的条件，如果符合则更新seq，并放到队尾
				CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
				//ch.getInt = 1;
				p->PeekHeader(ch);
				//nzh:第二个模块的处理逻辑，若是有cnp，就发到新端口,此端口的re_queue指向新端口的m_queue
				if(m_queue->GetLastQueue()!=0&&m_bps==1600000000000)
				{
					CnpKey key(ch.udp.dport,ch.udp.sport,ch.dip,ch.sip,ch.udp.pg);
					if(m_cnp_handler==NULL)
					{
						std::cout<<"cnp_handler is null"<<std::endl;
					}
					auto it = m_cnp_handler->find(key);
					if(it!=m_cnp_handler->end())
					{
						// if(re_queue)
						// 	re_queue->Enqueue(p,ch.udp.pg);
						// else
						// {
						// 	NS_LOG_ERROR("re_queue mot found");
						// }

					}

				}
				// if(enable_themis&&m_queue->GetLastQueue()!=0&&m_bps==1600000000000){
				// std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// WriteToFile(str);
				// //输出源ip和目的ip
				// // std::cout<<ch.sip<<"  "<<ch.dip<<std::endl;
				// }
				// if(enable_themis&&m_queue->GetLastQueue()!=0&&m_bps==400000000000){
				// // 					std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// // WriteToFile(str);
				// 	//输出此时队列内的长度	
				// 	if(ch.udp.pg!=m_queue->GetLastQueue())
				// 	{
				// 		std::cout<<ch.udp.pg<<"  "<<m_queue->GetLastQueue()<<std::endl;
				// 		std::cout<<ch.udp.sport<<"  "<<ch.dip<<"  "<<ch.udp.pg<<std::endl;
				// 	}
				// 	CnpKey key(ch.udp.dport,ch.udp.sport,ch.dip,ch.sip,ch.udp.pg);
				// 	// if(m_cnp_handler==NULL)
				// 	// {
				// 	// 	std::cout<<"cnp_handler is null"<<std::endl;
				// 	// }
				// 	auto it = m_cnp_handler->find(key);
				// 	if(it!=m_cnp_handler->end())
				// 	{
				// 		CNP_Handler &cnp = it->second;
				// 		if(!cnp.finished&&cnp.sended)
				// 		{
				// 			//第一个包第一次
				// 			if(cnp.first==0)
				// 			{
				// 				cnp.finish_time=cnp.rec_time;
				// 				//std::cout<<m_node->GetId()<<std::endl;
				// 				first_num++;
				// 				//printf("first_num:%d\n",first_num);
				// 				cnp.first=ch.udp.seq;
				// 				cnp.biggest=0;
				// 				cnp.delay = Seconds(m_bps.CalculateTxTime(m_queue->GetNBytes(ch.udp.pg)));
				// 				//std::cout<<"delay:              "<<cnp.delay<<std::endl;
				// 				cnp.n--;
				// 				m_queue->Enqueue(p,ch.udp.pg);
				// 				return;
				// 			}
				// 			//第一个包其他次
				// 			else if(cnp.first==ch.udp.seq)
				// 			{
				// 				//printf("%d n = %d",first_num,cnp.n);
				// 				if(cnp.n!=0){
				// 					cnp.n--;
				// 					cnp.delay+=Seconds(m_bps.CalculateTxTime(m_queue->GetNBytes(ch.udp.pg)));
				// 					m_queue->Enqueue(p,ch.udp.pg);
				// 					return;
				// 				}
				// 				else{
				// 					cnp.finished=true;
				// 					cnp.sended=false;
				// 					//important
				// 					//printf("into stage 2 %d\n",first_num);
				// 				}
				// 			}
				// 			//其他包
				// 			else {
				// 				//printf("2\n");
				// 				m_queue->Enqueue(p,ch.udp.pg);
				// 				return;
				// 			}
				// 		}
				// 		if(cnp.finished&&!cnp.sended)
				// 		{
				// 			//printf("3\n");
				// 			if(ns3::Simulator::Now()>=cnp.finish_time)
				// 			{
				// 				cnp.sended=true;
				// 				//printf("into stage 3 %d\n",first_num);
				// 			}
				// 			if(ch.udp.seq>cnp.biggest+1000)
				// 			{
				// 				m_queue->Enqueue(p,ch.udp.pg);
				// 				return;
				// 			}
				// 		}
				// 		if(cnp.finished&&cnp.sended)
				// 		{
				// 			if(ch.udp.seq>cnp.biggest+1000)
				// 			{
				// 				m_queue->Enqueue(p,ch.udp.pg);
				// 				return;
				// 			}
				// 			if(ns3::Simulator::Now()-cnp.rec_time<=ns3::MicroSeconds(55))
				// 			{
				// 				cnp.finished=false;
				// 				cnp.first=0;
				// 				cnp.n=num;
				// 				//printf("receive cnp again\n");
				// 				cnp.biggest=0;
				// 			}
				// 		}
				// 		if(ch.udp.seq>cnp.biggest)
				// 		{
				// 			cnp.biggest=ch.udp.seq;
				// 		}
				// /*注释到此为止 */
				// 		// if(!cnp.finished&&cnp.sended)
				// 		// {
				// 		// 	//第一个包第一次
				// 		// 	if(cnp.first==0)
				// 		// 	{
				// 		// 		// std::cout<<"sip= "<<ch.sip<< " dip= "<<ch.dip<<std::endl;
				// 		// 		cnp.finish_time=cnp.rec_time;
				// 		// 		//std::cout<<m_node->GetId()<<std::endl;
				// 		// 		first_num++;
				// 		// 		//printf("first_num:%d\n",first_num);
				// 		// 		cnp.first=ch.udp.seq;
				// 		// 		cnp.biggest=ch.udp.seq;
				// 		// 		cnp.delay = Seconds(m_bps.CalculateTxTime(m_queue->GetNBytes(ch.udp.pg)));
				// 		// 		//std::cout<<"delay:              "<<cnp.delay<<std::endl;
				// 		// 		//cnp.n--;
				// 		// 		if(m_queue->GetNBytes(m_queue->GetLastQueue())>10)
				// 		// 		{
				// 		// 			cnp.n--;
				// 		// 		}
				// 		// 		else{
				// 		// 			cnp.first=0;
				// 		// 		}
				// 		// // 								std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// 		// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// 		// // WriteToFile(str);
				// 		// 		m_queue->Enqueue(p,ch.udp.pg);
				// 		// 		return;
				// 		// 	}
				// 		// 	//第一个包其他次
				// 		// 	else if(cnp.first==ch.udp.seq)
				// 		// 	{
				// 		// 		//printf("%d n = %d",first_num,cnp.n);
				// 		// 		if(cnp.n!=0){
				// 		// 			cnp.n--;
				// 		// // 									std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// 		// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// 		// // WriteToFile(str);
				// 		// 			cnp.delay+=Seconds(m_bps.CalculateTxTime(m_queue->GetNBytes(ch.udp.pg)));
				// 		// 			m_queue->Enqueue(p,ch.udp.pg);
				// 		// 			return;
				// 		// 		}
				// 		// 		else{
				// 		// 			cnp.finished=true;
				// 		// 			cnp.sended=false;
				// 		// 			//important
				// 		// 			//printf("into stage 2 %d\n",first_num);
				// 		// 		}
				// 		// 	}
				// 		// 	//其他包
				// 		// 	else {
				// 		// 		if(ch.udp.seq>cnp.biggest)
				// 		// 		{
				// 		// 			cnp.biggest=ch.udp.seq;
				// 		// 		}
				// 		// 		//printf("2\n");
				// 		// // 								std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// 		// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// 		// // WriteToFile(str);
				// 		// 		m_queue->Enqueue(p,ch.udp.pg);
				// 		// 		return;
				// 		// 	}
				// 		// }
				// 		// if(cnp.finished&&!cnp.sended)
				// 		// {
				// 		// 	//printf("3\n");
				// 		// 	if(ns3::Simulator::Now()>=cnp.finish_time)
				// 		// 	{
				// 		// 		cnp.sended=true;
				// 		// 		//printf("into stage 3 %d\n",first_num);
				// 		// 	}
				// 		// 	if(ch.udp.seq>cnp.biggest&&cnp.biggest)
				// 		// 	{
				// 		// 		cnp.biggest+=1000;
				// 		// // 								std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// 		// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// 		// // WriteToFile(str);
				// 		// 		m_queue->Enqueue(p,ch.udp.pg);
				// 		// 		return;
				// 		// 	}
				// 		// 	if(ch.udp.seq==cnp.biggest&&cnp.biggest)
				// 		// 	{
				// 		// 		finish_num++;
				// 		// 		//printf("finish_num:%d\n",finish_num);
				// 		// 		cnp.biggest=0;
				// 		// 	}
				// 		// }
				// 		// if(cnp.finished&&cnp.sended)
				// 		// {
				// 		// 	if(ch.udp.seq>cnp.biggest&&cnp.biggest)
				// 		// 	{

				// 		// 		//printf("stage 3 resubmit%d %u %u\n",first_num,cnp.biggest,ch.udp.seq);
				// 		// 		//std::cout<<"stage 3 resubmit "<<first_num<<" "<<cnp.biggest<<" "<<ch.udp.seq<<std::endl;
				// 		// 		cnp.biggest+=1000;
				// 		// // 								std::string str = std::to_string(m_queue->GetNBytes(m_queue->GetLastQueue()));
				// 		// // str=str+' '+std::to_string(ch.udp.seq)+'\n';
				// 		// // WriteToFile(str);
				// 		// 		//printf("biggest to %d\n",cnp.biggest);
				// 		// 		m_queue->Enqueue(p,ch.udp.pg);
				// 		// 		return;
				// 		// 	}
				// 		// 	if(ch.udp.seq==cnp.biggest&&cnp.biggest)
				// 		// 	{
				// 		// 		finish_num++;
				// 		// 		//printf("finish_num:%d\n",finish_num);
				// 		// 		cnp.biggest=0;
				// 		// 	}
				// 		// 	if(ns3::Simulator::Now()-cnp.rec_time<=ns3::MicroSeconds(55)&&cnp.biggest==0)
				// 		// 	{
				// 		// 		cnp.finished=false;
				// 		// 		cnp.first=0;
				// 		// 		cnp.n=num;
				// 		// 		//printf("receive cnp again\n");
				// 		// 	}
				// 		// }
				// 		/*注释到此为止 */
				// 	}
				//  }
				m_snifferTrace(p);
				m_promiscSnifferTrace(p);
				Ptr<Packet> packet = p->Copy();
				uint16_t protocol = 0;
				Ipv4Header h;
				ProcessHeader(packet, protocol);
				packet->RemoveHeader(h);
				//printf("1\n");
				FlowIdTag t;
				//printf("2\n");
				uint32_t qIndex = m_queue->GetLastQueue();
				// CustomHeader ch2(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
				// packet->PeekHeader(ch2);
				//printf("3\n");
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
						m_nextSend = Simulator::Schedule(t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
					}
				}
			}
		}
		return;
	}
//nzh：收到了交换机发的cnp，创建cnp_handler以存储cnp信息，更新cnp接收时间，
	void QbbNetDevice::ReceiveCnp(Ptr<Packet> p, CustomHeader &ch) {
		uint8_t c = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
		if(!c){
			return;
		}
		uint16_t qIndex = ch.ack.pg;
		uint16_t port = ch.ack.dport;
		uint32_t sip = ch.sip;
		//在m_cnp_handler中查
		CnpKey key(ch.ack.sport,ch.ack.dport,ch.sip,ch.dip,ch.ack.pg);
		if (m_cnp_handler == nullptr) {
        	std::cerr << "m_cnp_handler is null" << std::endl;
        	return;
    	}
		auto it = m_cnp_handler->find(key);
		if(it != m_cnp_handler->end()){
			CNP_Handler &cnp = it->second;
			//更新接收时间
			std::string str = std::to_string((Simulator::Now().GetNanoSeconds()-cnp.rec_time.GetNanoSeconds()))+"\n";
			//WriteToFile(str);
			//std::cout<<str<<std::endl;
			cnp.rec_time = Simulator::Now();
			
			return;
		}
		CNP_Handler cnp;
		cnp.n = num;
		//输出cnp.n
		//std::cout<< cnp.n <<std::endl;
		cnp.rec_time = Simulator::Now();
		//std::cout<<"sip= "<<ch.dip<<"dip "<<sip<<" port= "<<port<<" qIndex= "<<qIndex<<std::endl;
		(*m_cnp_handler)[key] = cnp;
		//std::cout<<" finish "<<std::endl;
		//(*m_cnp_handler)[key] = cnp;
		//m_cnp_handler->insert(std::pair<CnpKey, CNP_Handler>(key, cnp));
		//输出cnp_handler中的所有key
		//  if(m_node->GetId()==80){
		// 	std::cout<<"nd2 "<<m_node->GetId()<<" cnp received sip= "<<sip<<" port= "<<port<<" qIndex= "<<qIndex<<std::endl;
		// 	std::cout << "m_cnp_handler size: " << m_cnp_handler->size() << std::endl;
		//  }
		return;
	}

	void
		QbbNetDevice::Resume(unsigned qIndex)
	{
		NS_LOG_FUNCTION(this << qIndex);
		NS_ASSERT_MSG(m_paused[qIndex], "Must be PAUSEd");
		m_paused[qIndex] = false;
		NS_LOG_INFO("Node " << m_node->GetId() << " dev " << m_ifIndex << " queue " << qIndex <<
			" resumed at " << Simulator::Now().GetSeconds());
		DequeueAndTransmit();
	}

	void
		QbbNetDevice::Receive(Ptr<Packet> packet)
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
			if (!m_qbbEnabled) return;
			unsigned qIndex = ch.pfc.qIndex;
			if (ch.pfc.time > 0){
				m_tracePfc(1); // 暂停
				m_paused[qIndex] = true;
				//std::cout << "PFC received " << m_node->GetId()<< std::endl;
			}else{
				m_tracePfc(0); // 继续
				Resume(qIndex);
			}
		}
		else { // non-PFC packets (data, ACK, NACK, CNP...)
			if (m_node->GetNodeType() > 0){ // switch
			//nzh：非常重要的写死的参数，判断要不要处理cnp
			if ((ch.l3Prot == 0xFC||ch.l3Prot==0xFD) && enable_themis) {
				//std::cout<<m_bps.GetBitRate()<<std::endl;
				ReceiveCnp(packet, ch);
				//printf("finish receive cnp\n");
			}
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

	bool QbbNetDevice::Send(Ptr<Packet> packet, const Address &dest, uint16_t protocolNumber)
	{
		NS_ASSERT_MSG(false, "QbbNetDevice::Send not implemented yet\n");
		return false;
	}

	bool QbbNetDevice::SwitchSend (uint32_t qIndex, Ptr<Packet> packet, CustomHeader &ch){
		m_macTxTrace(packet);
		m_traceEnqueue(packet, qIndex);
		m_queue->Enqueue(packet, qIndex);
		DequeueAndTransmit();
		return true;
	}
	//发送PFC
	void QbbNetDevice::SendPfc(uint32_t qIndex, uint32_t type){
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
		//std::cout << "pfc sent from " << m_node->GetId() << " to " << ch.sip << " port " << ch.udp.dport << " pg "<< ch.udp.pg<< std::endl;
		SwitchSend(0, p, ch);
	}
	void QbbNetDevice::SendCnp(Ptr<Packet> p, CustomHeader &ch){
		//发送CNP
		//新建包，设置l3Prot为0xFF，设置sip,dport,qIndex
		//输出当前交换机的编号
		//std::cout << "CNP sent from " << m_node->GetId() << " to " << ch.sip << " port " << ch.udp.dport << " pg "<< ch.udp.pg<< std::endl;
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
		ipv4h.SetProtocol(0xFF); //ack=0xFC nack=0xFD cnp=0xFF
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
		//std::cout << ch2.l3Prot << std::endl;
		SwitchSend(0, newp, ch2);
		//终端打印CNP
	}
	bool
		QbbNetDevice::Attach(Ptr<QbbChannel> ch)
	{
		NS_LOG_FUNCTION(this << &ch);
		m_channel = ch;
		m_channel->Attach(this);
		NotifyLinkUp();
		return true;
	}

	bool
		QbbNetDevice::TransmitStart(Ptr<Packet> p)
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
		Simulator::Schedule(txCompleteTime, &QbbNetDevice::TransmitComplete, this);

		bool result = m_channel->TransmitStart(p, this, txTime);
		if (result == false)
		{
			m_phyTxDropTrace(p);
		}
		return result;
	}

	Ptr<Channel>
		QbbNetDevice::GetChannel(void) const
	{
		return m_channel;
	}

   bool QbbNetDevice::IsQbb(void) const{
	   return true;
   }

   void QbbNetDevice::NewQp(Ptr<RdmaQueuePair> qp){
	   qp->m_nextAvail = Simulator::Now();
	   DequeueAndTransmit();
   }
   void QbbNetDevice::ReassignedQp(Ptr<RdmaQueuePair> qp){
	   DequeueAndTransmit();
   }
   void QbbNetDevice::TriggerTransmit(void){
	   DequeueAndTransmit();
   }

	void QbbNetDevice::SetQueue(Ptr<BEgressQueue> q){
		NS_LOG_FUNCTION(this << q);
		m_queue = q;
	}

	Ptr<BEgressQueue> QbbNetDevice::GetQueue(){
		return m_queue;
	}

	Ptr<RdmaEgressQueue> QbbNetDevice::GetRdmaQueue(){
		return m_rdmaEQ;
	}

	void QbbNetDevice::RdmaEnqueueHighPrioQ(Ptr<Packet> p){
		m_traceEnqueue(p, 0);
		m_rdmaEQ->EnqueueHighPrioQ(p);
	}

	void QbbNetDevice::TakeDown(){
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

	void QbbNetDevice::UpdateNextAvail(Time t){
		if (!m_nextSend.IsExpired() && t < m_nextSend.GetTs()){
			Simulator::Cancel(m_nextSend);
			Time delta = t < Simulator::Now() ? Time(0) : t - Simulator::Now();
			m_nextSend = Simulator::Schedule(delta, &QbbNetDevice::DequeueAndTransmit, this);
		}
	}
} // namespace ns3
