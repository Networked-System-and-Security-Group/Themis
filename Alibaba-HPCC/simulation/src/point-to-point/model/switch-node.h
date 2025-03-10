#ifndef SWITCH_NODE_H
#define SWITCH_NODE_H

#include <unordered_map>
#include <ns3/node.h>
#include "ns3/qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"
#include <map>

namespace ns3 {

class Packet;

class SwitchNode : public Node{

  	static const uint32_t init_loop_num = 1;
	static const uint32_t loop_increase_num = 1;
	static const uint32_t response_times = 40;
	static const uint32_t pCnt = 257;	// Number of ports used
	static const uint32_t qCnt = 16;	// Number of queues/priorities used
	uint32_t m_ecmpSeed;
	// monitor of PFC
	uint32_t m_bytes[pCnt][pCnt][qCnt]; // m_bytes[inDev][outDev][qidx] is the bytes from inDev enqueued for outDev at qidx
	
	uint64_t m_txBytes[pCnt]; // counter of tx bytes

	uint32_t m_lastPktSize[pCnt];
	uint64_t m_lastPktTs[pCnt]; // ns
	double m_u[pCnt];

	public:
	
	std::unordered_map<uint32_t, std::vector<int> > m_rtTable; // map from ip address (u32) to possible ECMP port (index of dev)
	long long m_BiCC[qCnt][2];



//zxc:以下是为switch_node新增的cnp-handler和cnp-key
	class CNP_Handler{
  		public:
		uint32_t cnp_num; // 这个流累计收到了多少cnp
    	Time rec_time;//最后一次接到CNP的时间
		Time set_last_loop;//zxc:最后一次为包打标记的时间 调试
    	Time finish_time;// 调试
    	uint32_t first;//第一个序列号 废弃
    	uint32_t biggest;//最大序列号 废弃
    	uint32_t loop_num;//需要resubmit n遍
    	Time delay;//第一个包resubmit带来的延迟 废弃
    	bool finished;// n次resubmit是否完成 废弃
    	bool sended;//是否发送过 废弃
		uint32_t recovered;//被废弃
		int recover[3005];//还差n次循环的包数量
    	CNP_Handler(){
			cnp_num = 0;
      		rec_time = Time(0);
      		finish_time = Time(0);
      		delay = Time(0);
      		first = 0;
      		biggest = 0;
      		loop_num = init_loop_num;
      		finished = false;
      		sended = true;
			set_last_loop = Simulator::Now();
			recovered = 0;
			memset(recover, 0, sizeof(recover));
		};
 	};

	struct CnpKey {
    	uint32_t sip;
    	uint32_t dip;
		uint32_t sport;
		uint32_t dport;
    	uint16_t qindex;
    	// 重载小于运算符，用于 map 的键比较
    	bool operator<(const CnpKey& other) const {
      		if (sip != other.sip) {
        		return sip < other.sip;
      		}
      		if (dip != other.dip) {
       			 return dip < other.dip;
      		}
			return qindex < other.qindex;
    	}
    	CnpKey(uint32_t sip, uint32_t dip, uint16_t qindex, uint32_t sport, uint32_t dport) {
	  		this->sip = sip;
	  		this->dip = dip;
	  		this->qindex = qindex;
			this->sport = sport;
			this->dport = dport;
		};
	};




protected:
	bool m_ecnEnabled;
	uint32_t m_ccMode;
	uint64_t m_maxRtt;

	uint32_t m_ackHighPrio; // set high priority for ACK/NACK

private:
	int GetOutDev(Ptr<const Packet>, CustomHeader &ch);
	void SendToDev(Ptr<Packet>p, CustomHeader &ch);
	static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);
	void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
	void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);
public:
	int ReceiveCnp(Ptr<Packet>p, CustomHeader &ch);//zxc:为交换机添加实现控制逻辑所需函数
	std::map<CnpKey, CNP_Handler> m_cnp_handler;
	std::map<CnpKey, Time> m_cnp_time;
	std::map<CnpKey, Time> m_ecn_detector;
	Ptr<SwitchMmu> m_mmu;
	void CheckAndSendCnp(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
	static TypeId GetTypeId (void);
	SwitchNode();
	void SetEcmpSeed(uint32_t seed);
	void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
	void ClearTable();
	bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
	void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
	// for approximate calc in PINT
	int logres_shift(int b, int l);
	int log2apprx(int x, int b, int m, int l); // given x of at most b bits, use most significant m bits of x, calc the result in l bits
	uint32_t ExternalSwitch = 0;//zxc:if this is an external switch
	uint32_t loop_qbb_index=7; //zxc: the index of the loop-decelerating qbb-net-device that installed in this switch
};

} /* namespace ns3 */






































#endif /* SWITCH_NODE_H */
#ifndef SWITCH_NODE2_H
#define SWITCH_NODE2_H

#include <unordered_map>
#include <ns3/node.h>
#include "qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"

namespace ns3 {

class Packet;

class SwitchNode2 : public Node{
	
    static const uint32_t num = 15;
	static const uint32_t pCnt = 257;	// Number of ports used
	static const uint32_t qCnt = 8;	// Number of queues/priorities used
	uint32_t m_ecmpSeed;
	std::unordered_map<uint32_t, std::vector<int> > m_rtTable; // map from ip address (u32) to possible ECMP port (index of dev)

	// monitor of PFC
	uint32_t m_bytes[pCnt][pCnt][qCnt]; // m_bytes[inDev][outDev][qidx] is the bytes from inDev enqueued for outDev at qidx
	
	uint64_t m_txBytes[pCnt]; // counter of tx bytes

	uint32_t m_lastPktSize[pCnt];
	uint64_t m_lastPktTs[pCnt]; // ns
	double m_u[pCnt];

protected:
	bool m_ecnEnabled;
	uint32_t m_ccMode;
	uint64_t m_maxRtt;

	uint32_t m_ackHighPrio; // set high priority for ACK/NACK

private:
	int GetOutDev(Ptr<const Packet>, CustomHeader &ch);
	void SendToDev(Ptr<Packet>p, CustomHeader &ch);
	static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);
	void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
	void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);
public:
	Ptr<SwitchMmu> m_mmu;
	void CheckAndSendCnp(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
	static TypeId GetTypeId (void);
	SwitchNode2();
	void SetEcmpSeed(uint32_t seed);
	void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
	void ClearTable();
	bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
	void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);
	// for approximate calc in PINT
	int logres_shift(int b, int l);
	int log2apprx(int x, int b, int m, int l); // given x of at most b bits, use most significant m bits of x, calc the result in l bits
};

} /* namespace ns3 */

#endif /* SWITCH_NODE_H */
