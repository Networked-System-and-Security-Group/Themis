import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker
plt.rcParams.update({'font.size': 30})
# 数据
data = {
    'w/o aligning with DCQCN': [  10.670745994808671,5.353541008553544, 8.13798188683516],
    'aligning with DCQCN': [ 5.244815204406885, 4.912151116381316, 5.086356077038108],

}

# 标签和方案
labels = ['Inter', 'Intra', 'Average']
schemes = ['w/o aligning with DCQCN', 'aligning with DCQCN']

# 颜色设置
colors = ['#D15354', '#E8B86C']

# 创建柱状图
fig, ax = plt.subplots(figsize=(10, 6))
bar_width = 0.2
index = np.arange(len(labels))

for k, scheme in enumerate(schemes):
    values = data[scheme]  # 获取对应方案的数据
    ax.bar(index + k * bar_width, values, bar_width, label=scheme, color=colors[k], edgecolor='black')

# 设置图表标签和标题
#ax.set_xlabel('Category')
ax.set_ylabel('Normalized FCT')
#ax.set_title('Comparison')
ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
ax.set_xticklabels(labels)
ax.grid(True)

# 设置统一的y轴范围
max_values = max(max(data['aligning with DCQCN']), max(data['w/o aligning with DCQCN']))
ax.set_ylim(0, max_values + 2.5)
if max_values < 10:
    ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
else:
    ax.yaxis.set_major_locator(ticker.MultipleLocator(5))
ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)

# 添加图例
ax.legend(loc='upper right')

# 保存图表到文件
plt.tight_layout()
plt.savefig('ecn.pdf', bbox_inches='tight')  # 保存为PNG文件
plt.show()