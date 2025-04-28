import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker
plt.rcParams.update({'font.size': 36})
# 数据
data = {
    'DCQCN': [3.0999251281787394, 7.470709061098879, 12.191705270174621],
    'Annulus': [2.487433147208762, 4.2626775744113505, 7.224589160652761],
    'Themis': [2.056527784057202, 2.7316538860972193, 4.664858606581419],
    'BiCC': [4.845120200879513, 6.833868924936687, 8.932973034634855]
}

# 标签和方案
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Annulus', 'BiCC', 'Themis']

# 颜色设置
colors = ['#D15354','#E8B86C', '#C6E3C6',  '#3DA6AE']

# 创建柱状图
fig, ax = plt.subplots(figsize=(12, 8))
bar_width = 0.2
index = np.arange(len(labels))

for k, scheme in enumerate(schemes):
    values = data[scheme]  # 获取对应方案的数据
    ax.bar(index + k * bar_width, values, bar_width, label=scheme, color=colors[k], edgecolor='black')

# 设置图表标签和标题
ax.set_xlabel('Load')
ax.set_ylabel('Normalized FCT')
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
plt.savefig('overall.pdf', bbox_inches='tight')  # 保存为PNG文件
plt.show()