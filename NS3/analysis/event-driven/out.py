def deduplicate_out_file(input_file, output_file):
    seen = set()  # 用于存储已经处理过的行
    with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
        for line in infile:
            parts = line.strip().split()
            if len(parts) >= 6:
                # 提取时间戳外的5个值
                key = tuple(parts[1:6])
                if key not in seen:
                    seen.add(key)
                    outfile.write(line)
            else:
                # 如果行格式不正确，直接写入
                outfile.write(line)

if __name__ == "__main__":
    input_file = './out.txt'
    output_file = './out_deduplicated.txt'
    deduplicate_out_file(input_file, output_file)
    print(f"去重完成，结果已保存到 {output_file}")