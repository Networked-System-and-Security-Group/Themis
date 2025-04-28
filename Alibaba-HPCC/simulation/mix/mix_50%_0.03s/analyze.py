import matplotlib.pyplot as plt
import numpy as np

# -*- coding: utf-8 -*-
MODE = True  # MODE为False表示单独分析intra或inter流量
prefix = "intra"

# 初始化全局变量
intra_ratio_sum = 0
intra_flow_cnt = 0
inter_ratio_sum = 0
inter_flow_cnt = 0
max_intra_ratio = 0
max_inter_ratio = 0
intra_fct = 0
inter_fct = 0

# 新增：按流量大小区间分类统计
intra_flow_cnt_by_size = {"low": 0, "middle": 0, "high": 0}
intra_ratio_sum_by_size = {"low": 0, "middle": 0, "high": 0}
inter_flow_cnt_by_size = {"low": 0, "middle": 0, "high": 0}
inter_ratio_sum_by_size = {"low": 0, "middle": 0, "high": 0}

def classify_flow_size(flow_size):
    """根据流量大小分类"""
    if flow_size <= 10 * 1024:  # 0-10KB
        return "low"
    elif flow_size <= 100 * 1024:  # 10KB-100KB
        return "middle"
    else:  # >100KB
        return "high"

# 在全局变量中新增一个列表，用于存储所有 intra 和 inter flow 的 ratio 值
intra_ratios = []
inter_ratios = []

# 在 read_and_classify 函数中，更新 intra_ratios 和 inter_ratios
def read_and_classify(file_path):
    global intra_ratio_sum, intra_flow_cnt, inter_ratio_sum, inter_flow_cnt
    global max_intra_ratio, max_inter_ratio, intra_fct, inter_fct
    global intra_ratios, inter_ratios  # 新增
    intra_dc_flow = []
    inter_dc_flow = []
    ratio_and_flow_size_record = []

    with open(file_path, 'r') as file:
        lines = file.readlines()

    if MODE:
        lines_to_process = lines
    else:
        lines_to_process = lines[:-1]

    for line in lines_to_process:
        row = line.strip().split()
        src = int(row[0], 16)
        dst = int(row[1], 16)
        flow_size = int(row[4])
        measured_fct = int(row[6])
        perfect_fct = int(row[7])
        ratio = measured_fct / perfect_fct

        # 分类流量大小
        flow_size_category = classify_flow_size(flow_size)

        src_third_field = (src >> 8) & 0xFF
        dst_third_field = (dst >> 8) & 0xFF

        if (src_third_field < 16 and dst_third_field < 16) or (src_third_field > 15 and dst_third_field > 15):
            intra_ratio_sum += ratio
            intra_flow_cnt += 1
            intra_fct += measured_fct
            intra_dc_flow.append(line.strip())

            # 更新按流量大小分类的统计
            intra_flow_cnt_by_size[flow_size_category] += 1
            intra_ratio_sum_by_size[flow_size_category] += ratio

            # 新增：将 ratio 值添加到 intra_ratios 列表中
            intra_ratios.append(ratio)

        else:
            inter_ratio_sum += ratio
            inter_flow_cnt += 1
            inter_fct += measured_fct
            inter_dc_flow.append(line.strip())

            # 更新按流量大小分类的统计
            inter_flow_cnt_by_size[flow_size_category] += 1
            inter_ratio_sum_by_size[flow_size_category] += ratio

            # 新增：将 ratio 值添加到 inter_ratios 列表中
            inter_ratios.append(ratio)

        # 更新最大 ratio
        max_intra_ratio = max(max_intra_ratio, ratio)
        max_inter_ratio = max(max_inter_ratio, ratio)

    # 输出统计结果
    print(f"intra_flow_cnt = {intra_flow_cnt}, intra_ratio = {intra_ratio_sum / intra_flow_cnt if intra_flow_cnt else 0}")
    print(f"max_intra_ratio = {max_intra_ratio}")
    print(f"intra_ratio_p99 = {np.percentile(intra_ratios, 99) if intra_flow_cnt else 0}")  # 修改
    print(f"intra_average_fct = {intra_fct / intra_flow_cnt if intra_flow_cnt else 0}")
    print(f"low_intra_flow_cnt = {intra_flow_cnt_by_size['low']}, low_intra_ratio = {intra_ratio_sum_by_size['low'] / intra_flow_cnt_by_size['low'] if intra_flow_cnt_by_size['low'] else 0}")
    print(f"middle_intra_flow_cnt = {intra_flow_cnt_by_size['middle']}, middle_intra_ratio = {intra_ratio_sum_by_size['middle'] / intra_flow_cnt_by_size['middle'] if intra_flow_cnt_by_size['middle'] else 0}")
    print(f"high_intra_flow_cnt = {intra_flow_cnt_by_size['high']}, high_intra_ratio = {intra_ratio_sum_by_size['high'] / intra_flow_cnt_by_size['high'] if intra_flow_cnt_by_size['high'] else 0}")

    print(f"inter_flow_cnt = {inter_flow_cnt}, inter_ratio = {inter_ratio_sum / inter_flow_cnt if inter_flow_cnt else 0}")
    print(f"max_inter_ratio = {max_inter_ratio}")
    print(f"inter_ratio_p99 = {np.percentile(inter_ratios, 99) if inter_flow_cnt else 0}")  # 修改
    print(f"inter_average_fct = {inter_fct / inter_flow_cnt if inter_flow_cnt else 0}")
    print(f"low_inter_flow_cnt = {inter_flow_cnt_by_size['low']}, low_inter_ratio = {inter_ratio_sum_by_size['low'] / inter_flow_cnt_by_size['low'] if inter_flow_cnt_by_size['low'] else 0}")
    print(f"middle_inter_flow_cnt = {inter_flow_cnt_by_size['middle']}, middle_inter_ratio = {inter_ratio_sum_by_size['middle'] / inter_flow_cnt_by_size['middle'] if inter_flow_cnt_by_size['middle'] else 0}")
    print(f"high_inter_flow_cnt = {inter_flow_cnt_by_size['high']}, high_inter_ratio = {inter_ratio_sum_by_size['high'] / inter_flow_cnt_by_size['high'] if inter_flow_cnt_by_size['high'] else 0}")

    print(f"Average ratio: {(intra_ratio_sum + inter_ratio_sum) / (intra_flow_cnt + inter_flow_cnt) if (intra_flow_cnt + inter_flow_cnt) else 0}")
    print(f"Average fct: {(intra_fct + inter_fct) / (intra_flow_cnt + inter_flow_cnt) if (intra_flow_cnt + inter_flow_cnt) else 0}")

    return intra_dc_flow, inter_dc_flow, ratio_and_flow_size_record

if __name__ == "__main__":
    if MODE:
        input_file = './fct_test2.txt'
    else:
        input_file = f'.fct_{prefix}_dc.txt'
    intra_dc_flow_file = './fct_intra_dc.txt'
    inter_dc_flow_file = './fct_inter_dc.txt'
    ratio_and_flow_size_file = f'./{prefix}_ratio_and_flow_size_record.txt'

    intra_dc_flow, inter_dc_flow, ratio_and_flow_size_record = read_and_classify(input_file)
