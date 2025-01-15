import random

output = "../simulation/mix/01-15/bigFlow/flow.txt"

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
n = len(inter_lines) - (len(lines) - 1) // 11
selected_lines = random.sample(inter_lines, n)
for i in selected_lines:
    first_num, second_num = map(int, lines[i].split()[:2])
    if first_num < 16:
        new_second_num = random.randint(0, 15)  # 在 0 到 15 之间随机选取一个数字
        while new_second_num == first_num:  # 确保新数字与第一个数字不同
            new_second_num = random.randint(0, 15)
        parts = lines[i].split()
        parts[1] = str(new_second_num)  # 修改第二个数字
        lines[i] = " ".join(parts) + "\n"
    else:
        new_second_num = random.randint(16, 31) 
        while new_second_num == first_num:  # 确保新数字与第一个数字不同
            new_second_num = random.randint(16, 31) 
        parts = lines[i].split()
        parts[1] = str(new_second_num)  # 修改第二个数字
        lines[i] = " ".join(parts) + "\n"

# 将修改后的内容写回文件
with open(output, "w") as file:
    file.writelines(lines)

# 输出总共符合条件的行数和被修改的行数
print(f"Total lines modified: {n}")
