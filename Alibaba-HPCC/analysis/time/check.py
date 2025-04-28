import csv

# 输入和输出文件路径
input_file = "time_50.csv"
output_file = "time_50_filtered.csv"

# 打开输入文件并处理
with open(input_file, mode='r', encoding='utf-8') as infile, open(output_file, mode='w', encoding='utf-8', newline='') as outfile:
    reader = csv.reader(infile)
    writer = csv.writer(outfile)
    
    for row in reader:
        # 写入不包含 "RC Acknowledge QP=0x0000cf" 的行
        if "RC Acknowledge QP=0x0000cf" not in row[-1]:
            writer.writerow(row)

print(f"处理完成，结果已保存到 {output_file}")