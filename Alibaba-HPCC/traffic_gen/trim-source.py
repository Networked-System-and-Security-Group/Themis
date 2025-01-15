import sys
import random
import math
import heapq
from optparse import OptionParser
from custom_rand import CustomRand

#output = "tmp_traffic.txt"
port = 80
parser = OptionParser()
parser.add_option("-o", "--output", dest = "output", help = "the output file", default = "../simulation/mix/01-15/bigFlow/flow.txt")
options,args = parser.parse_args()
output = options.output
# 打开文件读取数据
with open(output, "r") as file:
    lines = file.readlines()

# 输出 lines 的长度
print(f"total flow num: {len(lines) - 1}")

# 第一个数是流的总数，转换为整数
n_flow = int(lines[0].strip())

# 初始化 inter_lines 数组
inter_lines = []

# 从第二行开始处理，筛选符合条件的行
for i in range(1, len(lines)):
    first_num, second_num = map(int, lines[i].split()[:2])  # 提取行中前两个数字
    if first_num < 16 and second_num > 15 or first_num > 15 and second_num < 16:
        inter_lines.append(i)

print(f"Total inter flow num: {len(inter_lines)}")

# 从 inter_lines 中均匀随机选取 n 个行号进行修改
n = len(inter_lines) - (len(lines) - 1) // 6
selected_lines = random.sample(inter_lines, n)
for i in selected_lines:
    first_num, second_num = map(int, lines[i].split()[:2])
    if second_num >= 16:
        second_num = random.randint(0, 15)
        while second_num == first_num:  # 确保新数字与第一个数字不同
            second_num = random.randint(0, 15) 
        parts = lines[i].split()
        parts[0] = str(first_num)  # 修改第一个数字
        parts[1] = str(second_num)  # 修改第二个数字
        if i == 0: print("GG")
        lines[i] = " ".join(parts) + "\n"
    else:
        second_num = random.randint(0, 15)
        first_num = random.randint(0, 15)
        while second_num == first_num:  # 确保新数字与第一个数字不同Z
            first_num = random.randint(0, 15) 
        parts = lines[i].split()
        parts[1] = str(new_second_num)  # 修改第二个数字
        if i == 0: print("GG")
        lines[i] = " ".join(parts) + "\n"

# 将修改后的内容写回文件
with open(output, "w") as file:
    file.writelines(lines)

# 输出总共符合条件的行数和被修改的行数
print(f"Total lines modified: {n}")
