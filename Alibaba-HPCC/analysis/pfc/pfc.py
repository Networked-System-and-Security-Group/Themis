import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker
plt.rcParams.update({'font.size': 26})
# 数据
data = {
    'DCQCN': [4.32, 14.38, 16.76],
    'Annulus': [3.75, 9.61, 14.78],
    'Themis': [0.17, 1.83, 1.12]
    
}

# 标签和方案
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Annulus', 'Themis']

# 颜色设置
colors = ['#D15354', '#E8B86C', '#3DA6AE']

# 创建柱状图
fig, ax = plt.subplots(figsize=(10, 6))
bar_width = 0.2
index = np.arange(len(labels))

for k, scheme in enumerate(schemes):
    values = data[scheme]  # 获取对应方案的数据
    ax.bar(index + k * bar_width, values, bar_width, label=scheme, color=colors[k], edgecolor='black')

# 设置图表标签和标题
ax.set_xlabel('Load')
ax.set_ylabel('Fraction of pause time(%)')
#ax.set_title('Comparison')
ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
ax.set_xticklabels(labels)
ax.grid(True)

# 设置统一的y轴范围
max_values = max(max(data['DCQCN']), max(data['Annulus']))
ax.set_ylim(0, max_values + 2.5)
if max_values < 10:
    ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
else:
    ax.yaxis.set_major_locator(ticker.MultipleLocator(5))
ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)

# 添加图例
ax.legend(loc='upper left')

# 保存图表到文件
plt.tight_layout()
plt.savefig('pfc.pdf', bbox_inches='tight')  # 保存为PNG文件
plt.show()