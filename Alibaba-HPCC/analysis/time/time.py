import pandas as pd
import matplotlib.pyplot as plt
plt.rcParams.update({'font.size': 34})
# 加载和预处理数据
data = pd.read_csv("time.csv")  # 加载CSV文件

# 过滤出符合条件的行
filtered_data = data[
    ((data["Source"] == "192.168.201.4") & (data["Destination"] == "192.168.201.2")) |
    ((data["Source"] == "192.168.201.2") & (data["Destination"] == "192.168.201.4"))
]

filtered_data["Time"] = filtered_data["Time"].astype(float) - 17.683493

filtered_data.head()  # 显示预处理后的数据以验证

# 计算包数量
# 初始化两个计数器
count_4_to_2 = 0
count_2_to_4 = 0

# 创建两个列表用于存储累计包数量
cumulative_4_to_2 = []
cumulative_2_to_4 = []

# 遍历过滤后的数据，计算累计包数量
for _, row in filtered_data.iterrows():
    if row["Source"] == "192.168.201.4" and row["Destination"] == "192.168.201.2":
        count_4_to_2 += 1
    elif row["Source"] == "192.168.201.2" and row["Destination"] == "192.168.201.4":
        count_2_to_4 += 1
    
    cumulative_4_to_2.append(count_4_to_2)
    cumulative_2_to_4.append(count_2_to_4)

# 将累计包数量添加到数据框中
filtered_data["Cumulative_4_to_2"] = cumulative_4_to_2
filtered_data["Cumulative_2_to_4"] = cumulative_2_to_4

filtered_data.head()  # 显示计算结果以验证

# 绘制并保存图像
plt.figure(figsize=(10, 6))  # 设置图像大小

# 绘制两条折线图
plt.plot(filtered_data["Time"]*1000, filtered_data["Cumulative_4_to_2"], label="Data Packet", color="blue")
plt.plot(filtered_data["Time"]*1000, filtered_data["Cumulative_2_to_4"], label="CNP", color="orange")


plt.xlabel("Time(ms)")
plt.ylabel("Packet Number")

plt.legend()

plt.legend(loc='upper left')
plt.grid(True)
plt.tight_layout() 

# 保存图像为time.png
plt.savefig("time.pdf")
