#ifndef __GLOBAL_SETTINGS_H__
#define __GLOBAL_SETTINGS_H__

#include <stdbool.h>
#include <stdint.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>

#include "ns3/callback.h"
#include "ns3/custom-header.h"
#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/net-device.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/string.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"
#include "ns3/node.h"

namespace ns3 {



/**
 * @brief Global setting parameters
 */

class GlobalSettings {
   public:
    GlobalSettings() {}
    struct tuple_hash {
        template <class T1, class T2, class T3, class T4>
        std::size_t operator () (const std::tuple<T1, T2, T3, T4>& tuple) const {
            auto hash1 = std::hash<T1>{}(std::get<0>(tuple));
            auto hash2 = std::hash<T2>{}(std::get<1>(tuple));
            auto hash3 = std::hash<T3>{}(std::get<2>(tuple));
            auto hash4 = std::hash<T4>{}(std::get<3>(tuple));
            return hash1 ^ (hash2 << 1) ^ (hash3 << 2) ^ (hash4 << 3); // 使用位运算合并哈希
        }
    };
    static std::unordered_map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t, tuple_hash> PacketId2FlowId;
    static uint32_t getFlowId(CustomHeader &ch);
    static std::map<Ptr<Node>, std::map<uint32_t, uint32_t> > if2id;

    static void record_flow_distribution(CustomHeader &ch, Ptr<Node> srcNode, uint32_t outDev, uint32_t size);
    static void print_flow_distribution(FILE *out, Time nextTime);

};

}  // namespace ns3

#endif
