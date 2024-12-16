# -*- coding: utf-8 -*-
MODE = True # MODE为False表示单独分析intra或inter流量
prefix = "inter"
intra_ratio_sum = 0
intra_flow_cnt = 0
inter_ratio_sum = 0
inter_flow_cnt = 0

def read_and_classify(file_path):
    global intra_ratio_sum
    global intra_flow_cnt
    global inter_ratio_sum
    global inter_flow_cnt
    intra_dc_flow = []
    inter_dc_flow = []
    ratio_and_flow_size_record = []

    with open(file_path, 'r') as file:
        lines = file.readlines()  # 读取所有行到一个列表中

    if MODE:
        lines_to_process = lines
    else:
        lines_to_process = lines[:-1]  # 排除最后一行

    for line in lines_to_process:
            row = line.strip().split()
            src = int(row[0], 16)
            dst = int(row[1], 16)
            flow_size = int(row[4])
            measured_fct = int(row[6])
            perfect_fct = int(row[7])
            ratio = measured_fct / perfect_fct

            if (ratio > 2):
                flow_size = flow_size / 1000000
                # print(f"ratio = {ratio}, flow_size = {flow_size}MB")
                if MODE == False:
                    ratio_and_flow_size_record.append(f"ratio = {ratio}, flow_size = {flow_size}MB")

            ms_measured_fct = measured_fct / 1000000

            src_third_field = (src >> 8) & 0xFF
            dst_third_field = (dst >> 8) & 0xFF

            # print(src_third_field)
            # print(dst_third_field)

            if (src_third_field < 16 and dst_third_field < 16) or (src_third_field > 15 and dst_third_field > 15):
                intra_ratio_sum += ratio
                intra_flow_cnt += 1
                intra_dc_flow.append(line.strip())

            else:
                inter_ratio_sum += ratio
                inter_flow_cnt += 1
                inter_dc_flow.append(line.strip())
    
    if MODE: 
        if intra_flow_cnt > 0: 
            print(f"intra_ratio_sum = {intra_ratio_sum}, intra_flow_cnt = {intra_flow_cnt}, intra_ratio = {intra_ratio_sum / intra_flow_cnt}")
            intra_dc_flow.append(f"Average intra_dc ratio: {intra_ratio_sum / intra_flow_cnt}")
        if inter_flow_cnt > 0:
            print(f"inter_ratio_sum = {inter_ratio_sum}, inter_flow_cnt = {inter_flow_cnt}, inter_ratio = {inter_ratio_sum / inter_flow_cnt}")
            inter_dc_flow.append(f"Average intra_dc ratio: {inter_ratio_sum / inter_flow_cnt}")

    return intra_dc_flow, inter_dc_flow, ratio_and_flow_size_record

def write_to_file(data, file_path):
    with open(file_path, 'w') as file:
        for line in data:
            file.write(line + '\n')

if __name__ == "__main__":
    if MODE:
        input_file = './Inter-DC/ExprGroup/fct.txt'
    else:
        input_file = f'./Inter-DC/ExprGroup/fct_{prefix}_dc.txt'
    intra_dc_flow_file = './Inter-DC/ExprGroup/fct_intra_dc.txt'
    inter_dc_flow_file = './Inter-DC/ExprGroup/fct_inter_dc.txt'
    ratio_and_flow_size_file = f'./Inter-DC/ExprGroup/{prefix}_ratio_and_flow_size_record.txt'

    intra_dc_flow, inter_dc_flow, ratio_and_flow_size_record = read_and_classify(input_file)

    if MODE: 
        write_to_file(intra_dc_flow, intra_dc_flow_file)
        write_to_file(inter_dc_flow, inter_dc_flow_file)
    else:
        write_to_file(ratio_and_flow_size_record, ratio_and_flow_size_file)

