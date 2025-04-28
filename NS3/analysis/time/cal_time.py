import csv
from collections import defaultdict

# 定义文件路径
input_file = "time.csv"

# 存储时间差
time_differences = []

# 读取 CSV 文件
with open(input_file, "r") as file:
    reader = csv.DictReader(file)
    # 使用字典存储每个 Packet Sequence Number 的时间
    packet_times = defaultdict(dict)
    
    for row in reader:
        seq_num = row["Packet Sequence Number"]
        source = row["Source"]
        destination = row["Destination"]
        time = float(row["Time"])
        
        # 记录 192.168.201.4 -> 192.168.201.2 和 192.168.201.2 -> 192.168.201.4 的时间
        if source == "192.168.201.4" and destination == "192.168.201.2":
            packet_times[seq_num]["forward"] = time
        elif source == "192.168.201.2" and destination == "192.168.201.4":
            packet_times[seq_num]["backward"] = time

    # 计算时间差
    for seq_num, times in packet_times.items():
        if "forward" in times and "backward" in times:
            time_diff = abs(times["backward"] - times["forward"])
            time_differences.append(time_diff)

# 计算平均值、最小值和最大值
if time_differences:
    avg_time_diff = sum(time_differences) / len(time_differences) *1000000
    min_time_diff = min(time_differences)*1000000
    max_time_diff = max(time_differences)*1000000

    print(f"Avg:{avg_time_diff:.9f} us")
    print(f"min:{min_time_diff:.9f} us")
    print(f"max:{max_time_diff:.9f} us")
else:
    print("未找到匹配的时间差数据。")