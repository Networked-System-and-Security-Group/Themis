import matplotlib.pyplot as plt

MODE = True  # MODE为False表示单独分析intra或inter流量
prefix = "intra"
#intra_ratio_sum = 0
#intra_flow_cnt = 0
#inter_ratio_sum = 0
#inter_flow_cnt = 0
#inter_fct = 0
#intra_fct = 0
#max_inter_ratio = 0
#max_intra_ratio = 0

def read_and_classify(file_path):
    intra_ratio_sum=0
    intra_flow_cnt=0
    inter_ratio_sum=0
    inter_flow_cnt=0
    max_inter_ratio=0
    max_intra_ratio=0
    inter_fct=0
    intra_fct=0
    intra_dc_flow = []
    inter_dc_flow = []
    ratio_and_flow_size_record = []
    intra_average_fct = {}
    inter_average_fct = {}
    fct_sum = 0
    invalid_line_count = 0  # 记录格式不正确的行数

    with open(file_path, 'r') as file:
        lines = file.readlines()  # 读取所有行到一个列表中

    if MODE:
        lines_to_process = lines
    else:
        lines_to_process = lines[:-1]  # 排除最后一行

    for line_number, line in enumerate(lines_to_process, start=1):
        row = line.strip().split()
        if len(row) < 8:
            invalid_line_count += 1
            print(f"Invalid line {line_number} in file {file_path}: {line.strip()}")
            continue  # 跳过格式不正确的行

        try:
            src = int(row[0], 16)
            dst = int(row[1], 16)
            flow_size = int(row[4])
            measured_fct = int(row[6])
            perfect_fct = int(row[7])
            ratio = measured_fct / perfect_fct
            fct_sum += measured_fct
            time = (int(row[5]) + int(row[6])) / 1000000

            if (ratio > 2):
                flow_size = flow_size / 1000000
                if MODE == False:
                    ratio_and_flow_size_record.append(f"ratio = {ratio}, flow_size = {flow_size}MB")

            ms_measured_fct = measured_fct / 1000000

            src_third_field = (src >> 8) & 0xFF
            dst_third_field = (dst >> 8) & 0xFF

            if (src_third_field < 16 and dst_third_field < 16) or (src_third_field > 15 and dst_third_field > 15):
                max_intra_ratio = max(max_intra_ratio, ratio)
                intra_ratio_sum += ratio
                intra_flow_cnt += 1
                intra_fct += measured_fct
                intra_dc_flow.append(line.strip())
                intra_average_fct[time] = (intra_ratio_sum / intra_flow_cnt)
            else:
                max_inter_ratio = max(max_inter_ratio, ratio)
                inter_ratio_sum += ratio
                inter_flow_cnt += 1
                inter_fct += measured_fct
                inter_dc_flow.append(line.strip())
                inter_average_fct[time] = (inter_ratio_sum / inter_flow_cnt)
        except ValueError as e:
            invalid_line_count += 1
            print(f"Invalid line {line_number} in file {file_path}: {line.strip()} - {e}")

    if invalid_line_count > 0:
        print(f"Total invalid lines in file {file_path}: {invalid_line_count}")

    if MODE: 
        if intra_flow_cnt > 0: 
            print(f"intra_ratio_sum = {intra_ratio_sum}, intra_flow_cnt = {intra_flow_cnt}, intra_ratio = {intra_ratio_sum / intra_flow_cnt}")
            print(f"max_intra_ratio = {max_intra_ratio}")
            print(f"intra_average_fct = {intra_fct / intra_flow_cnt}")
            intra_dc_flow.append(f"Average intra_dc ratio: {intra_ratio_sum / intra_flow_cnt}")
        if inter_flow_cnt > 0:
            print(f"inter_ratio_sum = {inter_ratio_sum}, inter_flow_cnt = {inter_flow_cnt}, inter_ratio = {inter_ratio_sum / inter_flow_cnt}")
            print(f"max_inter_ratio = {max_inter_ratio}")
            print(f"inter_average_fct = {inter_fct / inter_flow_cnt}")
            inter_dc_flow.append(f"Average inter_dc ratio: {inter_ratio_sum / inter_flow_cnt}")
        print(f"Average ratio: {(intra_ratio_sum + inter_ratio_sum) / (intra_flow_cnt + inter_flow_cnt)}")
        print(f"Average fct: {fct_sum / (intra_flow_cnt + inter_flow_cnt)}")
    
    return intra_dc_flow, inter_dc_flow, ratio_and_flow_size_record

def plot_data(ratio_and_flow_size_record, output_file):
    flow_sizes = [x[0] for x in ratio_and_flow_size_record]
    ratios = [x[1] for x in ratio_and_flow_size_record]
    
    plt.figure()
    plt.scatter(flow_sizes, ratios)
    plt.xlabel('Flow Size')
    plt.ylabel('Ratio')
    plt.title('Flow Size vs Ratio')
    plt.savefig(output_file)
    plt.close()

def main():
    ratios = [0.3, 0.5, 0.7]
    for ratio in ratios:
        file_path = f"mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Web_{ratio}/fct.txt"
        output_file = f"plot_Ali_{ratio}.png"
        _, _, ratio_and_flow_size_record = read_and_classify(file_path)
        # plot_data(ratio_and_flow_size_record, output_file)

if __name__ == "__main__":
    main()
