import numpy as np

def calculate_statistics(filename):
    try:
        with open(filename, 'r') as file:
            inter_numbers = []
            intra_numbers = []
            srcs = []
            dsts = []
            intra_dc_flow = []
            inter_dc_flow = []
            for line in file:
                parts = line.split()
                if len(parts) == 8:  # 确保每行确实有8个数字
                    try:
                        src = int(parts[0], 16)
                        dst = int(parts[1], 16)
                        num = float(parts[6])  # 获取第七个数字
                        src_third_field = (src >> 8) & 0xFF
                        dst_third_field = (dst >> 8) & 0xFF
                        if (src_third_field < 16 and dst_third_field < 16) or (src_third_field > 15 and dst_third_field > 15):
                            intra_numbers.append(num)

                        else:
                            inter_numbers.append(num)
                    except ValueError:
                        print(f"警告: 行 '{line.strip()}' 的第七个元素不是有效的数字。")
                else:
                    print(f"警告: 行 '{line.strip()}' 不包含8个元素。")
        
        if intra_numbers:
            intra_average = np.mean(intra_numbers)
            intra_percentile_99 = np.percentile(intra_numbers, 99)
        if inter_numbers:
            inter_average = np.mean(inter_numbers)
            inter_percentile_99 = np.percentile(inter_numbers, 99)
            return intra_average, intra_percentile_99, inter_average, inter_percentile_99
        else:
            print("没有有效的数字来计算统计数据。")
            return None, None
    except FileNotFoundError:
        print(f"错误：文件 '{filename}' 未找到。")
        return None, None

# 使用函数
filename = 'UnfairPenalty/Inter-DC/ExprGroup/fct.txt'
intra_average, intra_percentile_99, inter_average, inter_percentile_99 = calculate_statistics(filename)
if intra_average is not None:
    print(f"Intra Average: {intra_average:.2f}")
    print(f"Intra Percentile 99th: {intra_percentile_99:.2f}")
if inter_average is not None:
    print(f"Inter Average: {inter_average:.2f}")
    print(f"Inter Percentile 99th: {inter_percentile_99:.2f}")
