import matplotlib.pyplot as plt
import numpy as np

# 读取文件数据
def read_data(file_path):
    times = []
    flow_types = []
    rates = []
    with open(file_path, 'r') as file:
        for line in file:
            parts = line.strip().split()
            times.append((int(parts[0]) / 1e6)-2000)  # 将时间从纳秒转换为毫秒
            flow_types.append(parts[1])
            rates.append(float(parts[2]))
    return times, flow_types, rates

# 绘制数据
def plot_data(times, flow_types, rates):
    plt.rcParams.update({'font.size': 30})
    intra_times = []
    intra_rates = []
    inter_times = []
    inter_rates = []

    # 分离intra和inter流的数据
    for time, flow_type, rate in zip(times, flow_types, rates):
        if flow_type == 'intra':
            intra_times.append(time)
            intra_rates.append(rate)
        else:
            inter_times.append(time)
            inter_rates.append(rate)

    fig, ax = plt.subplots(figsize=(10, 6))

    # 绘制intra流数据
    intra_line, = ax.step(intra_times, intra_rates, where='post', color='red', label='Intra-DC')
    ax.fill_between(intra_times, 0, intra_rates, step='post', facecolor='none', hatch='//', edgecolor='red')

# 绘制inter流数据
    inter_line, = ax.step(inter_times, inter_rates, where='post', color='blue', label='Inter-DC')
    ax.fill_between(inter_times, 0, inter_rates, step='post', facecolor='none', hatch='xx', edgecolor='blue')

    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('Rate (Gbps)')
    ax.legend()
    ax.grid(True)
    plt.tight_layout()
    plt.savefig('long.png')

if __name__ == "__main__":
    file_path = 'long.txt'
    times, flow_types, rates = read_data(file_path)
    plot_data(times, flow_types, rates)