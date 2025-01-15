from collections import OrderedDict, defaultdict
from dataclasses import dataclass, field
import os.path as op
import re
from typing import Dict, Union

@dataclass
class Flow:
    sip_id: int
    dip_id: int
    sport: int
    dport: int
    m_size: int
    start_time: float
    elapsed_time: float
    standalone_fct: float
    flow_id: int
    fct_slowdown: Union[float, None] = field(init=False)
    flow_type: Union[float, None] = field(init=False)

    def __post_init__(self):
        if self.standalone_fct == 0:
            self.fct_slowdown = float('inf')
        else:
            self.fct_slowdown = self.elapsed_time / self.standalone_fct
        dc_check = lambda x : 1 if 0 <= x <= 15 else 2
        self.flow_type = {(1,1):"DC1内部流", (1,2):"DC1->DC2",(2,1):"DC2->DC1",(2,2):"DC2内部流",}[(dc_check(self.sip_id), dc_check(self.dip_id))]

flow_trace = defaultdict(list)# flow_id -> list[tuple<time, sid, did, size>]
flows = []
id_to_flow = {}
defaultdict_int = lambda: defaultdict(int)
link_state = defaultdict(defaultdict_int)# (src, dst) -> dict[time, total_size]

def parse_flow_trace_file():
    """
    解析流轨迹文件，记录每个流在每个时间点的源节点和目标节点信息。
    """
    current_time = None
    with open('./Inter-DC/ExprGroup/flow_distribution.txt', 'r') as file:
        for line in file:
            time_match = re.match(r'#####Time\[(\d+)\]#####', line)
            if time_match:
                current_time = int(time_match.group(1))
            elif current_time is not None:
                link_match = re.match(r'Link: srcId=(\d+), dstId=(\d+), flowNum=\d+, active flow:([\d:\d, ]+)', line)
                if link_match:
                    src_id = int(link_match.group(1))
                    dst_id = int(link_match.group(2))
                    flows = [(int(flow.strip().split(':')[0]), int(flow.strip().split(':')[1])) for flow in link_match.group(3).split(',') if flow.strip()]
                    total_size = 0
                    for flow, size in flows:
                        flow_trace[flow].append((current_time, src_id, dst_id, size))
                        total_size += size
                    link_state[(src_id, dst_id)][current_time] = total_size

def parse_fct_file():
    with open('./Inter-DC/ExprGroup/fct.txt', 'r') as file:
        for line in file:
            data = line.split()
            # if len(data) != 9:
            #     continue
            sip_id = ((int(data[0],16)) >> 8) & 0xFF
            dip_id = ((int(data[1],16)) >> 8) & 0xFF
            sport = int(data[2])
            dport = int(data[3])
            m_size = int(data[4])
            start_time = int(data[5])
            elapsed_time = int(data[6])
            standalone_fct = int(data[7])
            flow_id = int(data[8])
            
            flow = Flow(sip_id, dip_id, sport, dport, m_size, start_time, elapsed_time, standalone_fct, flow_id)
            flows.append(flow)
            id_to_flow[flow_id] = flow

parse_fct_file()
print(len(flows))
parse_flow_trace_file()
print(len(flow_trace))
flows.sort(key = lambda x : x.fct_slowdown, reverse=True)

print(sum([f.fct_slowdown for f in flows]) / len(flows))

for flow in flows[:30]:
    print(flow)
    trace = flow_trace[flow.flow_id]
    trace.sort(key=lambda x: (x[0], x[1] >= x[2], x[1] if x[1] < x[2] else -x[1]))
    full_path = list(OrderedDict.fromkeys([(x[1], x[2]) for x in trace]))#set([(x[1], x[2]) for x in trace])
    for time, src, dst, size in trace:
        full_path_info = [(path[0], path[1], link_state[path][time]) for path in full_path]
        print(f'{time}: {src} -> {dst}, total packet size = {size}, path info={full_path_info}')
