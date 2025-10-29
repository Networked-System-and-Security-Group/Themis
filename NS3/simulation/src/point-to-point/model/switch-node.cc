#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/udp-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "switch-node.h"
#include "qbb-net-device.h"
#include "qbb-header.h"
#include "ppp-header.h"
#include "ns3/int-header.h"
#include "rdma-hw.h"
#include "ns3/random-variable.h"
#include <cmath>
#include<map>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <ns3/global-settings.h>
#include <set> // 添加头文件
#include <tuple> // 用于存储流信息的元组
#include <algorithm>

// 定义一个全局变量，用于存储流信息
std::set<std::tuple<uint32_t, uint32_t, uint16_t, uint16_t>> uniqueFlows;

#define register_only 0
#define mat_only 0

namespace ns3 {

TypeId SwitchNode::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SwitchNode")
    .SetParent<Node> ()
    .AddConstructor<SwitchNode> ()
	.AddAttribute("EcnEnabled",
			"Enable ECN marking.",
			BooleanValue(false),
			MakeBooleanAccessor(&SwitchNode::m_ecnEnabled),
			MakeBooleanChecker())
	.AddAttribute("CcMode",
			"CC mode.",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ccMode),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("AckHighPrio",
			"Set high priority for ACK/NACK or not",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("MaxRtt",
			"Max Rtt of the network",
			UintegerValue(9000),
			MakeUintegerAccessor(&SwitchNode::m_maxRtt),
			MakeUintegerChecker<uint32_t>())
  ;
  return tid;
}
uint32_t ip_to_node_id(Ipv4Address ip){
	return (ip.Get() >> 8) & 0xffff;
}
SwitchNode::SwitchNode(){
	m_ecmpSeed = m_id;
	m_node_type = 1;
	m_mmu = CreateObject<SwitchMmu>();
	m_cnp_handler = std::map<CnpKey, CNP_Handler>();
	//m_cnp_handler = std::unordered_map<CnpKey, CNP_Handler>();
	ExternalSwitch = 0;
	loop_qbb_index = 7;
	m_preloadEnabled = false;
	m_preloadPacketCount = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		for (uint32_t j = 0; j < pCnt; j++)
			for (uint32_t k = 0; k < qCnt; k++)
				m_bytes[i][j][k] = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		m_txBytes[i] = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		m_lastPktSize[i] = m_lastPktTs[i] = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		m_u[i] = 0;
	//m_cnp_handler.reserve(1024);
}

int SwitchNode::GetOutDev(Ptr<const Packet> p, CustomHeader &ch){
	// look up entries
	auto entry = m_rtTable.find(ch.dip);

	// no matching entry
	if (entry == m_rtTable.end())
		return -1;

	// entry found
	auto &nexthops = entry->second;

	// pick one next hop based on hash
	union {
		uint8_t u8[4+4+2+2];
		uint32_t u32[3];
	} buf;
	buf.u32[0] = ch.sip;
	buf.u32[1] = ch.dip;
	if (ch.l3Prot == 0x6)
		buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
	else if (ch.l3Prot == 0x11)
		buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
	else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)
		buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);

	uint32_t idx = EcmpHash(buf.u8, 12, m_ecmpSeed) % nexthops.size();
	return nexthops[idx];
}

void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldPause(inDev, qIndex)){
		device->SendPfc(qIndex, 0);
		m_mmu->SetPause(inDev, qIndex);
	}
}


void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldResume(inDev, qIndex)){
		device->SendPfc(qIndex, 1);
		m_mmu->SetResume(inDev, qIndex);
		//std::cout<<"resume "<<Simulator::Now()<<" node "<<m_id<<" qIndex "<<qIndex<<std::endl;
	}
}

uint32_t max_cnp_num = 0;
//zxc:此函数用于判断是否收到cnp以及在收到cnp时更新m_cnp_handler信息
/**
 * 如果收到的不是CNP，donothing
 * 如果收到的是第一次CNP，则初始化
 * 如果收到的不是第一次cnp，且距离上次CNP大于100ns，则更新loop_num和biggest
 */
int SwitchNode::ReceiveCnp(Ptr<Packet>p, CustomHeader &ch){
	uint8_t c = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1; 
	int sid = ip_to_node_id(Ipv4Address(ch.sip)); 
	int did = ip_to_node_id(Ipv4Address(ch.dip));
	if(!c) 
	{
		return 0;//zxc:不是cnp的话返回0
	}
	
	//printf("received a cnp\n");
	//printf("ch.ack.dport = %d, ch.ack.sport = %d,\n ch.sid = %d, ch.did = %d,\n ch.ack.pg = %d, ch.ack.dport = %d, ch.ack.sport = %d\n", ch.ack.dport, ch.ack.sport, sid, did, ch.ack.pg, ch.ack.dport,ch.ack.sport);
	CnpKey key(ch.dip, ch.sip, ch.udp.pg, ch.udp.dport, ch.udp.sport);
	auto iter = m_cnp_handler.find(key);
	if(iter!=m_cnp_handler.end()){
	//if(iter!=nullptr){
		max_cnp_num = std::max(max_cnp_num, iter->second.cnp_num);
		//printf("max	cnp_num = %d\n", max_cnp_num);
		CNP_Handler &cnp_handler = iter->second;
		cnp_handler.recovered=0;
		if(Simulator::Now()-cnp_handler.rec_time>=ns3::NanoSeconds(1)){
			//std::cout<<"time"<<Simulator::Now()-cnp_handler.rec_time<<std::endl;
			cnp_handler.rec_time = Simulator::Now();
			cnp_handler.cnp_num += 1;
			if(cnp_handler.cnp_num==1)
			{
				cnp_handler.loop_num++;
				cnp_handler.alpha++;
				// cnp_handler.alpha=32-((m_recyclePacketCount-(1024))>>(9));
				// if(cnp_handler.alpha<1)cnp_handler.alpha=1;
				// if(cnp_handler.alpha>32)cnp_handler.alpha=32;
				cnp_handler.biggest++;
			}
			//cnp_handler.cnp_num += 1;
			if(cnp_handler.cnp_num>=cnp_handler.alpha)
			{
				cnp_handler.cnp_num -= cnp_handler.alpha;
				cnp_handler.alpha++;
				// cnp_handler.alpha=32-((m_recyclePacketCount-(1024))>>(9));
				// if(cnp_handler.alpha<1)cnp_handler.alpha=1;
				// if(cnp_handler.alpha>32)cnp_handler.alpha=32;
				cnp_handler.biggest++;
				cnp_handler.loop_num++;
			}
		}
		int32_t val = ((256 - (m_recyclePacketCount >> 7)));
		//if(val<1)val=1;
		if(cnp_handler.loop_num>val)
		{
			std::cout<<"min:"<<cnp_handler.loop_num<<" "<<val<<" "<<m_recyclePacketCount<<std::endl;
			cnp_handler.loop_num=val;
		}
	}
	else{
		CNP_Handler cnp_handler;
		cnp_handler.cnp_num = 1;
		//cnp_handler.rec_time=Simulator::Now();
		cnp_handler.set_last_loop = Simulator::Now();
		cnp_handler.per_recycle_num=((m_recyclePacketCount>>10)>1)?(m_recyclePacketCount>>10):0;
		//cnp_handler.per_recycle_num=1;
		m_cnp_handler[key] = cnp_handler;
		//m_cnp_handler.insert(key, cnp_handler);
		//std::cout<<Simulator::Now()<<" "<<sid<<" "<<did<<" "<<ch.udp.pg<<" "<<ch.udp.dport<<" "<<ch.udp.sport<<std::endl;
	}
	//printf("size of m_cnp_handler: %d\n", m_cnp_handler.size());
	return 1;//更新m_cnp_handler信息后返回1
}



bool isDataPkt(CustomHeader &ch){
	return !(ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || ch.l3Prot == 0xFD || ch.l3Prot == 0xFC);
}

int pkt_num = 0;
int recir_num = 0;

// int init_log = 0;
//nzh:非常重要的函数！！！important
void SwitchNode::SendToDev(Ptr<Packet>p, CustomHeader &ch){
	int idx = GetOutDev(p, ch);
	int sid = ip_to_node_id(Ipv4Address(ch.sip)); int did = ip_to_node_id(Ipv4Address(ch.dip));

	// 检查是否是预加载包
	if (p->recycle_times_left==-1) {
		// 预加载包直接发送到循环端口
		idx = loop_qbb_index;
		//std::cout << "Switch " << idx << " handling preload packet " << ch.udp.seq << " directly to loop port" << std::endl;
	}

	if (idx >= 0){
		NS_ASSERT_MSG(m_devices[idx]->IsLinkUp(), "The routing table look up should return link that is up");

		// determine the qIndex
		uint32_t qIndex;
		if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || (m_ackHighPrio && (ch.l3Prot == 0xFD || ch.l3Prot == 0xFC))){  //QCN or PFC or NACK, go highest priority
			qIndex = 0;
		}else{
			qIndex = (ch.l3Prot == 0x06 ? 1 : ch.udp.pg); // if TCP, put to queue 1
		}

		// rixin: 下面只是更新了idx，也就是要走哪个端口出去，此操作必须在Label1之前，不然循环qbb的egress_bytes会不对，导致产生ECN
		//zxc:控制逻辑只在外部交换机上实现
		// rixin: 第二个模块
		if(1&&(m_id >=48)){
			if (p->recycle_times_left==-1) {
			// 预加载包直接发送到循环端口
			idx = loop_qbb_index;

			//std::cout << "Switch " << m_id << " handling preload packet " << ch.udp.seq << " directly to loop port" << std::endl;
			goto Pre;
		}
			int sid = ip_to_node_id(Ipv4Address(ch.sip)); int did = ip_to_node_id(Ipv4Address(ch.dip));

			//zxc：判断是否是cnp以及更新cnp_handler信息
			int is_cnp = ReceiveCnp(p,ch);
			//zxc:判断是否要循环减速，cnp直接发走，非cnp才需要执行此操作
			if(!is_cnp){
				CnpKey key(ch.sip,ch.dip,ch.udp.pg,ch.udp.sport,ch.udp.dport);
				auto iter = m_cnp_handler.find(key);
				//zxc: 如果没有被cnp命中则直接发走，被命中则进入下方控制逻辑
				if(iter!=m_cnp_handler.end()) {
				//if(iter!=nullptr) {
					//printf("1111111111111111111\n");
					if (isDataPkt(ch)&&idx!=5&&((!mat_only)||(Simulator::Now()-iter->second.rec_time>=ns3::MicroSeconds(2000)))) {
						//std::cout << "Pkt recycle" << std::endl;
						//zxc:recycle_times_left==0表明这个包已经被减速并完成减速
						//std::cout<<"sid = "<<sid<<", did = "<<did<<"mid = "<<m_id<<"idx = "<<idx<<std::endl;
						if(p->recycle_times_left!=0){
							auto flowInfo = std::make_tuple(ch.sip, ch.dip, ch.udp.sport, ch.udp.dport);

							// 如果流信息尚未存储，则存储并输出
							if (uniqueFlows.find(flowInfo) == uniqueFlows.end()) {
								uniqueFlows.insert(flowInfo);
								// std::cout << uniqueFlows.size()
								// 		<< std::endl;
							}
							if(p->recycle_times_left == -100){
								p->recycle_times_left = iter->second.loop_num;
								m_recyclePacketCount++; 
								//std::cout<<"cnt+: "<<m_recyclePacketCount<<std::endl;
								if(m_recyclePacketCount>r_max)
								{
									r_max=m_recyclePacketCount;
									//std::cout<<"r_max : "<<r_max<<std::endl;
								}
								idx = loop_qbb_index;
								//std::cout<<m_id<<" "<<Simulator::Now()<<" "<<ch.udp.seq<<" "<<p->recycle_times_left<<std::endl;
								// if(p->recycle_times_left){
								// 	idx = loop_qbb_index;
								// 	p->recycle_times_left -= iter->second.per_recycle_num;
								// 	if(p->recycle_times_left<0)
								// 	{
								// 		p->recycle_times_left=0;
								// 	}
								// 	iter->second.recover[p->recycle_times_left]++;

								// }
							}
							else{
								iter->second.recover[p->recycle_times_left]--;
								p->recycle_times_left -= iter->second.per_recycle_num;
								if(p->recycle_times_left<0)
								{
									p->recycle_times_left=0;
								}
								if(Simulator::Now()-iter->second.rec_time>=ns3::MicroSeconds(5000)){
									iter->second.alpha=1;
									for(int i = p->recycle_times_left; i >0;i--)
									{
										if(iter->second.recover[i])
										{
											goto Minus;
										}
									}
									//std::cout<<"recover"<<std::endl;
									//p->recycle_times_left/=2;
									p->recycle_times_left = 0;// 包通过270行归0，计数器-1
									goto Send;
								}
								Minus:
								Send:
								idx = loop_qbb_index;
								
								iter->second.recover[p->recycle_times_left]++;
								
								// 检查循环次数是否归0
								if(p->recycle_times_left == 0){
									m_recyclePacketCount--; // 循环次数归0，计数器-1
									//std::cout<<"cnt : "<<m_recyclePacketCount<<std::endl;
								}
							}
						}
						else{
						}
					}
					
				}
			}
		}
		Pre:
		// Label1：admission control
		FlowIdTag t;
		p->PeekPacketTag(t);
		uint32_t inDev = t.GetFlowId(); 
		//模块：naive_solution_sendPFC
		if(0&&(m_id >=48)){
			
			int sid = ip_to_node_id(Ipv4Address(ch.sip)); int did = ip_to_node_id(Ipv4Address(ch.dip));

			//zxc：判断是否是cnp以及更新cnp_handler信息
			int is_cnp = ReceiveCnp(p,ch);
			//zxc:判断是否要循环减速，cnp直接发走，非cnp才需要执行此操作
			if(!is_cnp){
				CnpKey key(ch.sip,ch.dip,ch.udp.pg,ch.udp.sport,ch.udp.dport);
				auto iter = m_cnp_handler.find(key);
				//zxc: 如果没有被cnp命中则直接发走，被命中则进入下方控制逻辑
				if(iter!=m_cnp_handler.end()) {
				//if(iter!=nullptr) {
					//solution:PFC
					if(isDataPkt(ch)&&idx!=5)
					{
						if(Simulator::Now()-iter->second.rec_time<ns3::MicroSeconds(100)&&iter->second.cnp_num>5)
						{
							Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
							device->SendPfc(qIndex, 1);
							m_mmu->SetResume(inDev, qIndex);
							//std::cout<<"paused "<<Simulator::Now()<<" node "<<m_id<<" qIndex "<<qIndex<<std::endl;
						}
						else{
							CheckAndSendResume(inDev, qIndex);
						}
					}
				}
				else
				{
					CheckAndSendResume(inDev, qIndex);
				}
			}
		}
		if (qIndex != 0){ //not highest priority
			//交换机判断丢包逻辑
			if (m_mmu->CheckIngressAdmission(inDev, qIndex, p->GetSize()) && m_mmu->CheckEgressAdmission(idx, qIndex, p->GetSize())){			// Admission control
				m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
				m_mmu->UpdateEgressAdmission(idx, qIndex, p->GetSize());
			} else {
				printf("Packet Drop: switch id:%u, flow id: %u\n", m_id, GlobalSettings::getFlowId(ch));
				return; // Drop
			}
			CheckAndSendPfc(inDev, qIndex);
		}

		
		//zxc:inDev是输入网卡，idx是目的网卡，qIndex是目的网卡接收此pkt的队列
		m_bytes[inDev][idx][qIndex] += p->GetSize();
		m_devices[idx]->SwitchSend(qIndex, p, ch);
		GlobalSettings::record_flow_distribution(ch, this, idx, p->GetSize());
	}
	else {
		printf("Packet Drop: switch id:%u, flow id: %u\n", m_id, GlobalSettings::getFlowId(ch));
		return; // Drop
	}
}
uint32_t SwitchNode::EcmpHash(const uint8_t* key, size_t len, uint32_t seed) {
  uint32_t h = seed;
  if (len > 3) {
    const uint32_t* key_x4 = (const uint32_t*) key;
    size_t i = len >> 2;
    do {
      uint32_t k = *key_x4++;
      k *= 0xcc9e2d51;
      k = (k << 15) | (k >> 17);
      k *= 0x1b873593;
      h ^= k;
      h = (h << 13) | (h >> 19);
      h += (h << 2) + 0xe6546b64;
    } while (--i);
    key = (const uint8_t*) key_x4;
  }
  if (len & 3) {
    size_t i = len & 3;
    uint32_t k = 0;
    key = &key[i - 1];
    do {
      k <<= 8;
      k |= *key--;
    } while (--i);
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    h ^= k;
  }
  h ^= len;
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

void SwitchNode::SetEcmpSeed(uint32_t seed){
	m_ecmpSeed = seed;
}

void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);
}

void SwitchNode::ClearTable(){
	m_rtTable.clear();
}

// This function can only be called in switch mode
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch){
	SendToDev(packet, ch);
	return true;
}
int cnp_num=0;
void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
	FlowIdTag t;
	p->PeekPacketTag(t);
	if (qIndex != 0){
		uint32_t inDev = t.GetFlowId();
		m_mmu->RemoveFromIngressAdmission(inDev, qIndex, p->GetSize());
		m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
		m_bytes[inDev][ifIndex][qIndex] -= p->GetSize();
		
		// 处理循环包
		if (ifIndex == loop_qbb_index && m_preloadEnabled) {
			// 解析自定义头部
			CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
			p->PeekHeader(ch);
			
			// 检查是否是预加载包
			// if (p->recycle_times_left==-1) {
			// 	// 重新循环这个包
			// 	Simulator::Schedule(MicroSeconds(1), &SwitchNode::HandlePreloadPacket, this, p);
			// 	return;
			// }
			// 重新添加头部
		}
		
		if (m_ecnEnabled){
			CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);

			bool egressCongested = m_mmu->ShouldSendCN(ifIndex, qIndex);
			//if(m_mmu->egress_bytes[ifIndex][qIndex])
				//std::cout<<m_id<<"  "<<m_mmu->egress_bytes[ifIndex][qIndex]<<std::endl;
			if (egressCongested){
				Ipv4Header h;
				PppHeader ppp;
				p->RemoveHeader(ppp);
				p->RemoveHeader(h);
				h.SetEcn((Ipv4Header::EcnType)0x03);
				p->AddHeader(h);
				p->AddHeader(ppp);
				//std::cout<<m_id<<" congested"<<std::endl;
			}
			//nzh:如果有Ecnbit，就发cnp
			// rixin: 下面原本只写了判断有没有ECN标记，导致了一个问题，就是如果有ECN标记，但是不是数据包（ACK），
			// 导致CNP发给了接收端，而接收端可能正在发送DC内部流，导致DC内部流的效果变差
			// rixin: 第一个模块
			p->PeekHeader(ch);
			if(0&&isDataPkt(ch) && ch.GetIpv4EcnBits() && m_id >= 48){
				//std::cout<<ns3::Simulator::Now().GetNanoSeconds()<<": send cnp1"<<std::endl;
				// printf("module 1 is running\n");
				int sid = ip_to_node_id(Ipv4Address(ch.sip)); int did = ip_to_node_id(Ipv4Address(ch.dip));
				//printf("source node id = %d, dst node id = %d, m_node id = %d\n", sid, did,m_id);
				Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
					//if(device->enable_themis){
						Ipv4Header h;
						PppHeader ppp; 
						p->RemoveHeader(ppp);
						p->RemoveHeader(h);
						//printf("send cnp\n");
						h.SetEcn((Ipv4Header::EcnType)0x00);
						p->AddHeader(h);
						p->AddHeader(ppp);
						CnpKey key(ch.sip,ch.dip,ch.udp.pg,ch.udp.sport,ch.udp.dport);
						auto iter = m_ecn_detector.find(key);
						if(iter != m_ecn_detector.end()){
							if(Simulator::Now()-iter->second >= ns3::MicroSeconds(5)){
								Simulator::Schedule(ns3::MicroSeconds(2), &QbbNetDevice::SendCnp, device, p, ch);
								iter->second = Simulator::Now()+ns3::MicroSeconds(2);
								// device->SendCnp(p, ch);
								// iter->second = Simulator::Now();
								
							}
						}
						else{
							Simulator::Schedule(ns3::MicroSeconds(2), &QbbNetDevice::SendCnp, device, p, ch);
							m_ecn_detector[key] = Simulator::Now()+ns3::MicroSeconds(2);
							// device->SendCnp(p, ch);
							// m_ecn_detector[key] = Simulator::Now();
						}

			}
						
						// if(inDev == 5)
						// 	std::cout<<"inDev: "<<inDev<<", ifIndex: "<<ifIndex<<", qIndex: "<<qIndex<<std::endl;
					//}
		}			
		
		//CheckAndSendPfc(inDev, qIndex);
		CheckAndSendResume(inDev, qIndex);
	}
	if (1){
		uint8_t* buf = p->GetBuffer();
		if (buf[PppHeader::GetStaticSize() + 9] == 0x11){ // udp packet
			IntHeader *ih = (IntHeader*)&buf[PppHeader::GetStaticSize() + 20 + 8 + 6]; // ppp, ip, udp, SeqTs, INT
			Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
			if (m_ccMode == 3){ // HPCC
				ih->PushHop(Simulator::Now().GetTimeStep(), m_txBytes[ifIndex], dev->GetQueue()->GetNBytesTotal(), dev->GetDataRate().GetBitRate());
			}else if (m_ccMode == 10){ // HPCC-PINT
				uint64_t t = Simulator::Now().GetTimeStep();
				uint64_t dt = t - m_lastPktTs[ifIndex];
				if (dt > m_maxRtt)
					dt = m_maxRtt;
				uint64_t B = dev->GetDataRate().GetBitRate() / 8; //Bps
				uint64_t qlen = dev->GetQueue()->GetNBytesTotal();
				double newU;

				/**************************
				 * approximate calc
				 *************************/
				int b = 20, m = 16, l = 20; // see log2apprx's paremeters
				int sft = logres_shift(b,l);
				double fct = 1<<sft; // (multiplication factor corresponding to sft)
				double log_T = log2(m_maxRtt)*fct; // log2(T)*fct
				double log_B = log2(B)*fct; // log2(B)*fct
				double log_1e9 = log2(1e9)*fct; // log2(1e9)*fct
				double qterm = 0;
				double byteTerm = 0;
				double uTerm = 0;
				if ((qlen >> 8) > 0){
					int log_dt = log2apprx(dt, b, m, l); // ~log2(dt)*fct
					int log_qlen = log2apprx(qlen >> 8, b, m, l); // ~log2(qlen / 256)*fct
					qterm = pow(2, (
								log_dt + log_qlen + log_1e9 - log_B - 2*log_T
								)/fct
							) * 256;
				}
				if (m_lastPktSize[ifIndex] > 0){
					int byte = m_lastPktSize[ifIndex];
					int log_byte = log2apprx(byte, b, m, l);
					byteTerm = pow(2, (
								log_byte + log_1e9 - log_B - log_T
								)/fct
							);
					// 2^((log2(byte)*fct+log2(1e9)*fct-log2(B)*fct-log2(T)*fct)/fct) ~= byte*1e9 / (B*T)
				}
				if (m_maxRtt > dt && m_u[ifIndex] > 0){
					int log_T_dt = log2apprx(m_maxRtt - dt, b, m, l); // ~log2(T-dt)*fct
					int log_u = log2apprx(int(round(m_u[ifIndex] * 8192)), b, m, l); // ~log2(u*512)*fct
					uTerm = pow(2, (
								log_T_dt + log_u - log_T
								)/fct
							) / 8192;
					// 2^((log2(T-dt)*fct+log2(u*512)*fct-log2(T)*fct)/fct)/512 = (T-dt)*u/T
				}
				newU = qterm+byteTerm+uTerm;

				#if 0
				/**************************
				 * accurate calc
				 *************************/
				double weight_ewma = double(dt) / m_maxRtt;
				double u;
				if (m_lastPktSize[ifIndex] == 0)
					u = 0;
				else{
					double txRate = m_lastPktSize[ifIndex] / double(dt); // B/ns
					u = (qlen / m_maxRtt + txRate) * 1e9 / B;
				}
				newU = m_u[ifIndex] * (1 - weight_ewma) + u * weight_ewma;
				printf(" %lf\n", newU);
				#endif

				/************************
				 * update PINT header
				 ***********************/
				uint16_t power = Pint::encode_u(newU);
				if (power > ih->GetPower())
					ih->SetPower(power);

				m_u[ifIndex] = newU;
			}
		}
	}
	m_txBytes[ifIndex] += p->GetSize();
	m_lastPktSize[ifIndex] = p->GetSize();
	m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();
}

int SwitchNode::logres_shift(int b, int l){
	static int data[] = {0,0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5};
	return l - data[b];
}

int SwitchNode::log2apprx(int x, int b, int m, int l){
	int x0 = x;
	int msb = int(log2(x)) + 1;
	if (msb > m){
		x = (x >> (msb - m) << (msb - m));
		#if 0
		x += + (1 << (msb - m - 1));
		#else
		int mask = (1 << (msb-m)) - 1;
		if ((x0 & mask) > (rand() & mask))
			x += 1<<(msb-m);
		#endif
	}
	return int(log2(x) * (1<<logres_shift(b, l)));
}

//启用预加载功能
void SwitchNode::EnablePreload(uint32_t packetCount) {
	m_preloadEnabled = true;
	m_preloadPacketCount = packetCount;
	m_preloadPackets.clear();
	m_preloadPackets.reserve(packetCount);
	
	// 创建预加载包
	for (uint32_t i = 0; i < packetCount; i++) {
		CreatePreloadPacket(i);
	}
	
	// 延迟注入包，确保网络初始化完成
	Simulator::Schedule(Seconds(2), &SwitchNode::InjectPreloadPackets, this);
}

// 创建预加载包
void SwitchNode::CreatePreloadPacket(uint32_t packetId) {
    // 1. 创建 qbbHeader (L4/自定义传输层)
    qbbHeader seqh;
    seqh.SetSeq(packetId);
    seqh.SetPG(3); // 优先级组 (Priority Group)
    seqh.SetSport(10000 + packetId); // 源端口
    seqh.SetDport(20000 + packetId); // 目标端口
    // 假设 SetIntHeader 是可选的
    
    // 2. 创建数据包的 Payload 部分。
    // 计算 Payload Data Size = max(最小帧长60 - 以太网头14 - IP头20 - qbbHeader大小, 0)
    // 这保证了 L4 Header + Payload Data + L3 Header + L2 Header 的总大小能满足最小帧长要求。
    Ptr<Packet> packet = Create<Packet>(std::max(60-14-20-(int)seqh.GetSerializedSize(), 0));
    
    // L4: 添加 qbbHeader (ns-3中，AddHeader是预先添加，所以从高层开始加)
    packet->AddHeader(seqh);
    
    // 3. 添加 IPv4 头部 (L3 网络层)
    Ipv4Header ipv4Header;
    ipv4Header.SetSource(Ipv4Address(0x0b000001)); // 虚拟源IP
    ipv4Header.SetDestination(Ipv4Address(0x0b000002)); // 虚拟目标IP
    // Protocol 0x11 (UDP) 是 L4 协议号。如果 qbbHeader 是自定义 L4 协议，则使用对应的值。
    ipv4Header.SetProtocol(0x11);
    ipv4Header.SetTtl(64);
    // GetSize() 返回 L4 头部 + Payload Data 的大小 (即 L3 层的 Payload Size)
    ipv4Header.SetPayloadSize(packet->GetSize()); 
    ipv4Header.SetIdentification(packetId);
    
    // L3: 添加 IPv4 头部
    packet->AddHeader(ipv4Header);
    
    // 4. 添加 PPP 头部 (L2 链路层)
    // 这是协议栈最底层，最后添加。
    PppHeader pppHeader;
    pppHeader.SetProtocol(0x0021); // IPv4协议 (EtherType/Protocol)
    
    // L2: 添加 PPP 头部
    packet->AddHeader(pppHeader);
    
    // 5. 设置自定义属性 (Tag)
    // 移除冗余的 CustomHeader ch 结构体，因为它的内容已在实际头部中设置，且未被序列化。
    
    // 设置循环次数标记（假设这是 Ptr<Packet> 上的自定义扩展属性）
    packet->recycle_times_left = -1; 
    
    // 添加流ID标签
    FlowIdTag flowTag(loop_qbb_index); 
    packet->AddPacketTag(flowTag);
    
    // 6. 将创建好的包加入预载列表
    m_preloadPackets.push_back(packet);
}


// 注入预加载包
void SwitchNode::InjectPreloadPackets() {
	if (!m_preloadEnabled) return;
	
	std::cout << "Switch " << m_id << " injecting " << m_preloadPacketCount << " preload packets" << std::endl;
	
	for (uint32_t i = 0; i < m_preloadPacketCount; i++) {
		Ptr<Packet> packet = m_preloadPackets[i]->Copy();
		HandlePreloadPacket(packet);
	}
}

// 处理预加载包
void SwitchNode::HandlePreloadPacket(Ptr<Packet> packet) {
    // 解析自定义头部
    CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
    packet->PeekHeader(ch);

    // 创建CNP handler（如果不存在）
    CnpKey key(ch.sip, ch.dip, ch.udp.pg, ch.udp.sport, ch.udp.dport);
    auto iter = m_cnp_handler.find(key);

    if (iter == m_cnp_handler.end()) {
        // 创建新的CNP handler
        CNP_Handler cnp_handler;
        cnp_handler.cnp_num = 1;
        cnp_handler.loop_num = 5; // 设置正常包的初始循环次数
        cnp_handler.rec_time = Simulator::Now();
        cnp_handler.set_last_loop = Simulator::Now();
        m_cnp_handler[key] = cnp_handler;
        iter = m_cnp_handler.find(key);
    }
	packet->recycle_times_left = -1;
    // 如果是预加载包，确保其循环次数为 -1
    if (packet->recycle_times_left == -1) {
        // 预加载包永久循环，不减少循环次数
        std::cout << "Preload packet detected, recycling indefinitely." << std::endl;
    } else {
        // 正常包减少循环次数
        if (packet->recycle_times_left > 0) {
            packet->recycle_times_left -= 1;
            iter->second.recover[packet->recycle_times_left]++;
        } else {
            std::cout << "Packet recycle_times_left is 0, stopping loop. " <<packet->recycle_times_left<< std::endl;
            return;
        }
    }

    // 将包发送到循环端口
    Ptr<QbbNetDevice> loopDevice = DynamicCast<QbbNetDevice>(m_devices[loop_qbb_index]);
    if (loopDevice) {
        loopDevice->SwitchSend(3, packet, ch); // 使用队列1
        m_bytes[loop_qbb_index][loop_qbb_index][1] += packet->GetSize();
        if (packet->recycle_times_left != -1) {
            m_recyclePacketCount++; // 更新循环包计数器
        }
        std::cout << "Switch " << m_id << " sent packet " << ch.udp.seq
                  << " to loop port " << loop_qbb_index
                  << ", recycle_times_left: " << packet->recycle_times_left << std::endl;
    } else {
        //std::cout << "Switch " << m_id << " loop device " << loop_qbb_index << " is not a QbbNetDevice!" << std::endl;
    }
}

} /* namespace ns3 */

