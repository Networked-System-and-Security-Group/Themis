def process_file(file_name, delete_condition):
    data = []
    with open(file_name, 'r') as file:
        for line_number, line in enumerate(file, start=1):
            if line_number == 1:  # 跳过第一行
                continue
            parts = line.split()
            # 检查行是否符合预期格式
            if len(parts) != 6:
                print(f"Skipping malformed line {line_number} in {file_name}: {line.strip()}")
                continue
            try:
                # 将前五个数转换为整数，第六个数转换为浮点数
                nums = [int(float(x)) for x in parts[:5]] + [float(parts[5])]
                if delete_condition(nums[0]):  # 判断是否删除该行
                    continue
                data.append(nums)
            except ValueError:
                print(f"Skipping line with invalid data {line_number} in {file_name}: {line.strip()}")
                continue
    return data

def save_to_file(file_name, data):
    with open(file_name, 'w') as file:
        # 在文件的第一行添加流的数量
        file.write(f"{len(data)}\n")
        for row in data:
            # 确保前五个数为整数，第六个数保留浮点格式
            file.write(" ".join(map(str, row[:5])) + f" {row[5]:.9f}\n")

def main():
    ratios = [0.3, 0.5, 0.7]
    for ratio in ratios:
        # 设置文件路径
        ali_file = f"flow_Ali_{ratio}_1:10.txt"
        web_file = f"flow_Web_{ratio}_1:10.txt"
        output_file = f"testflow_mix_Ali_and_Web_{ratio}_1:10.txt"

        # 处理 flow_Ali 文件，删除第一个数 <= 16 的行
        data_ali = process_file(ali_file, lambda x: x < 16)
        
        # 处理 flow_web 文件，删除第一个数 > 16 的行
        data_web = process_file(web_file, lambda x: x < 16)

        # 合并数据并按 time 排序
        merged_data = data_ali + data_web
        merged_data = data_web
        merged_data.sort(key=lambda x: x[5])  # 按最后一个数字（time）排序
        
        # 存储到新的文件
        save_to_file(output_file, merged_data)
        print(f"Processed and saved to {output_file} with {len(merged_data)} streams")

if __name__ == "__main__":
    main()
