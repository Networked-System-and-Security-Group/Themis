import matplotlib.pyplot as plt
plt.rcParams.update({'font.size': 34})
import numpy as np
import re

class FlowEvent:
    def __init__(self, time, event_type, sip, dip, sport, dport):
        self.time = time
        self.event_type = event_type  # 'create' or 'destroy'
        self.sip = sip
        self.dip = dip
        self.sport = sport
        self.dport = dport

def parse_sip_dip(hex_value):
    # 将十六进制值转换为字符串
    hex_str = hex(hex_value)[2:]  # 去掉前缀 '0x'
    # 确保字符串长度至少为4位
    hex_str = hex_str.zfill(4)
    # 提取倒数第三和第四位
    return int(hex_str[-4:-2], 16)

def main():
    # 读取out.txt中的创建事件
    create_events = []
    create_dict = {}  # 用于存储创建的流，键为(sip, dip, sport, dport)
    with open('./out.txt', 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 6:
                time = int(parts[0])
                dip = int(parts[1])
                sip = int(parts[2])
                sport = int(parts[4])
                dport = int(parts[5])
                event = FlowEvent(time, 'create', sip, dip, sport, dport)
                create_events.append(event)
                key = (sip, dip, sport, dport)
                create_dict[key] = event
    
    # 读取fct.txt中的销毁事件
    destroy_events = []
    fct_data = []
    with open('./fct.txt', 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 6:
                fct_data.append(parts)
    
    # 为每个创建事件查找对应的销毁事件
    for create_event in create_events:
        key = (create_event.sip, create_event.dip, create_event.sport, create_event.dport)
        for parts in fct_data:
            # 解析sip和dip
            sip_hex = int(parts[0], 16)
            dip_hex = int(parts[1], 16)
            sip = parse_sip_dip(sip_hex)
            dip = parse_sip_dip(dip_hex)
            
            # 解析sport和dport
            sport = int(parts[2])
            dport = int(parts[3])
            
            # 解析完成时间
            complete_time = int(parts[5]) + int(parts[6])
            
            if (sip, dip, sport, dport) == key:
                if complete_time + 3000 > create_event.time:
                    destroy_events.append(FlowEvent(complete_time + 3000, 'destroy', sip, dip, sport, dport))
                    break  # 找到第一个满足条件的事件后停止
    
    # 合并创建和销毁事件
    all_events = create_events + destroy_events
    all_events.sort(key=lambda x: x.time)
    
    # 计算只创建不销毁的表项数量
    create_timeline = []
    current_count = 0
    for event in create_events:
        current_count += 1
        create_timeline.append((event.time, current_count))
    
    # 计算既创建又销毁的表项数量
    destroy_timeline = []
    current_count = 0
    for event in all_events:
        if event.event_type == 'create':
            current_count += 1
        else:
            current_count -= 1
        destroy_timeline.append((event.time, current_count))
    
    # 截止时间（2.04秒 = 2040000000纳秒）
    cutoff_time = 2040000000
    
    # 处理create_timeline
    if create_timeline:
        last_time, last_count = create_timeline[-1]
        if last_time < cutoff_time:
            create_timeline.append((cutoff_time, last_count))
        else:
            # 找到第一个超过截止时间的索引
            idx = next((i for i, (t, _) in enumerate(create_timeline) if t > cutoff_time), None)
            if idx is not None:
                create_timeline = create_timeline[:idx]
                create_timeline.append((cutoff_time, create_timeline[-1][1]))
    
    # 处理destroy_timeline
    if destroy_timeline:
        last_time, last_count = destroy_timeline[-1]
        if last_time < cutoff_time:
            destroy_timeline.append((cutoff_time, last_count))
        else:
            # 找到第一个超过截止时间的索引
            idx = next((i for i, (t, _) in enumerate(destroy_timeline) if t > cutoff_time), None)
            if idx is not None:
                destroy_timeline = destroy_timeline[:idx]
                destroy_timeline.append((cutoff_time, destroy_timeline[-1][1]))
    
    # 时间偏移（减去2秒 = 2000000000纳秒）
    time_offset = 2000000000
    
    # 准备绘图数据，转换为毫秒
    create_times = [(t[0] - time_offset) / 1000000 for t in create_timeline]  # 纳秒转毫秒
    create_counts = [t[1] for t in create_timeline]
    
    destroy_times = [(t[0] - time_offset) / 1000000 for t in destroy_timeline]  # 纳秒转毫秒
    destroy_counts = [t[1] for t in destroy_timeline]
    
    # 绘图
    plt.figure(figsize=(10, 6))
    
    # 绘制只创建不销毁的曲线
    plt.plot(create_times, create_counts, label='without destroy', color='blue', linestyle='-')
    
    # 绘制既创建又销毁的曲线
    plt.plot(destroy_times, destroy_counts, label='proactive destroy', color='red', linestyle='--')
    
    plt.xlabel('Time (ms)')
    plt.ylabel('number of entries')
    plt.legend(loc='upper left')
    plt.grid(True)
    plt.tight_layout()  # 自动调整布局，使得图表元素不会重叠
    plt.savefig('event-driven-comparison.png')

if __name__ == "__main__":
    main()