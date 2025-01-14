#include "ns3/global-settings.h"

#include <limits> // for std::numeric_limits
#include <map>
#include <vector>
#include <set>
#include <algorithm> // for std::max
#include <functional>
#include "ns3/simulator.h"
#include <queue>


namespace ns3 {
    uint32_t ip_to_node_id(uint32_t ip){
        return (ip >> 8) & 0xffff;
    }
/* helper function */
    std::unordered_map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t, GlobalSettings::tuple_hash> GlobalSettings::PacketId2FlowId;
    std::map<Ptr<Node>, std::map<uint32_t, uint32_t> > GlobalSettings::if2id;

    struct FlowTraceItem {
        Time last_time;
        int total_size;
    };
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, struct FlowTraceItem>> flowRecorder; //(src,dst)->(flow_id, active_time)

    uint32_t GlobalSettings::getFlowId(CustomHeader &ch) {
        auto itr = GlobalSettings::PacketId2FlowId.find(std::make_tuple(ip_to_node_id(ch.sip), ip_to_node_id(ch.dip), ch.udp.sport, ch.udp.dport));
        if (itr == GlobalSettings::PacketId2FlowId.end()) {
            return 65535;
        }
        return itr->second;
    }


    void GlobalSettings::record_flow_distribution(CustomHeader &ch, Ptr<Node> srcNode, uint32_t outDev, uint32_t size) {
        if (ch.l3Prot != 0x11) {
            return;
        }
        uint32_t srcId = srcNode->GetId();
        uint32_t dstId = GlobalSettings::if2id[srcNode][outDev];
        uint64_t linkKey = (static_cast<uint64_t>(srcId) << 32) | static_cast<uint64_t>(dstId);
        if (dstId == ip_to_node_id(ch.dip)) {
            return;
        }
        uint32_t flowId = GlobalSettings::getFlowId(ch);
        flowRecorder[linkKey][flowId].last_time = Simulator::Now(); 
        flowRecorder[linkKey][flowId].total_size += size;
    }

    void GlobalSettings::print_flow_distribution(FILE *out, Time nextTime) {
        // 打印当前时间
        fprintf(out, "#####Time[%ld]#####\n", Simulator::Now().GetNanoSeconds());

        for (auto linkEntry = flowRecorder.begin(); linkEntry != flowRecorder.end(); ++linkEntry) {
            uint64_t linkKey = linkEntry->first;//try->first;
            auto& flowMap = linkEntry->second;
            uint32_t srcId = static_cast<uint32_t>(linkKey >> 32);
            uint32_t dstId = static_cast<uint32_t>(linkKey & 0xFFFFFFFF);
            //去除不活跃的流
            for (auto flowEntry = flowMap.begin(); flowEntry != flowMap.end();) {
                Time flowTime = flowEntry->second.last_time;
                if (Simulator::Now() - flowTime > nextTime) {
                    flowEntry = flowMap.erase(flowEntry); 
                    continue;
                }
                ++flowEntry;
            }

            fprintf(out, "Link: srcId=%u, dstId=%u, flowNum=%zu, active flow:", srcId, dstId, flowMap.size());

            for (auto flowEntry = flowMap.begin(); flowEntry != flowMap.end();) {
                uint32_t flowId = flowEntry->first;
                fprintf(out, "%u:%d, ", flowId, flowEntry->second.total_size);
                flowEntry->second.total_size = 0;
                ++flowEntry;
            }
            fprintf(out, "\n");
        }
        fprintf(out, "\n");
        Simulator::Schedule(nextTime, &GlobalSettings::print_flow_distribution, out, nextTime);
    }
}  // namespace ns3
