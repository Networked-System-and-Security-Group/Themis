import matplotlib.pyplot as plt

MODE = True  # MODE为False表示单独分析intra或inter流量
prefix = "intra"
intra_ratio_sum = 0
intra_flow_cnt = 0
inter_ratio_sum = 0
inter_flow_cnt = 0
inter_fct = 0
intra_fct = 0
max_inter_ratio = 0
max_intra_ratio = 0

def read_and_classify(file_path):
    global intra_ratio_sum
    global intra_flow_cnt
    global inter_ratio_sum
    global inter_flow_cnt
    global max_inter_ratio
    global max_intra_ratio
    global inter_fct 
    global intra_fct
    intra_dc_flow = []
    inter_dc_flow = []
    ratio_and_flow_size_record = []
    intra_average_fct = {}
    inter_average_fct = {}
    
    with open(file_path, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) != 3:
                continue
            flow_size, fct, ratio = map(float, parts)
            if "intra" in file_path:
                intra_dc_flow.append(line.strip())
                intra_ratio_sum += ratio
                intra_flow_cnt += 1
                intra_fct += fct
                max_intra_ratio = max(max_intra_ratio, ratio)
                intra_average_fct[flow_size] = intra_ratio_sum / intra_flow_cnt
            else:
                inter_dc_flow.append(line.strip())
                inter_ratio_sum += ratio
                inter_flow_cnt += 1
                inter_fct += fct
                max_inter_ratio = max(max_inter_ratio, ratio)
                inter_average_fct[flow_size] = inter_ratio_sum / inter_flow_cnt
            ratio_and_flow_size_record.append((flow_size, ratio))
    
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
        print(f"Average fct: {(intra_fct + inter_fct) / (intra_flow_cnt + inter_flow_cnt)}")
    
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
        file_path = f"mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{ratio}/fct.txt"
        output_file = f"plot_Ali_{ratio}.png"
        _, _, ratio_and_flow_size_record = read_and_classify(file_path)
        plot_data(ratio_and_flow_size_record, output_file)

if __name__ == "__main__":
    main()