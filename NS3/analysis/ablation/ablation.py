import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 36})

# 数据
schemes = ['DCQCN', 'PNP', 'TRP', 'Themis']
data = {
    'intra': [23.57516886943873, 8.247635475633123, 7.682883614231474, 5.175305242259282],
    'inter': [1.83039458043768, 1.7045903072652404, 10.536207208668609, 4.200549621619747]
}

# 颜色映射
colors = ['#D15354', '#3DA6AE', '#E8B86C', '#C6E3C6']
light_colors = ['#F5A6A8', '#AED8E6', '#FDE5A6', '#E2F5E2']  # 更浅的颜色版本

# 创建柱状图
fig, ax = plt.subplots(figsize=(10, 6))

# 柱子的宽度和位置
bar_width = 0.35
index = np.arange(len(schemes))

# 存储图例句柄
legend_handles = []
legend_labels = []

# 绘制 intra 和 inter 的柱子，并添加数值标签
for i, scheme in enumerate(schemes):
    # 绘制 intra 柱子
    intra_bar = ax.bar(index[i] - bar_width/2, data['intra'][i], bar_width, 
                       label=f'{scheme} Intra', color=colors[i], edgecolor='black')
    
    # 绘制 inter 柱子
    inter_bar = ax.bar(index[i] + bar_width/2, data['inter'][i], bar_width, 
                       label=f'{scheme} Inter', color=light_colors[i], edgecolor='black')
    
    # 添加图例句柄和标签
    legend_handles.extend([intra_bar, inter_bar])
    legend_labels.extend([f'{scheme} Intra', f'{scheme} Inter'])

# 设置标签和标题
#ax.set_xlabel('Scheme')
ax.set_ylabel('Normalized FCT')
ax.set_xticks(index)
ax.set_xticklabels(schemes)

# 添加图例
ax.legend(handles=legend_handles, labels=legend_labels, loc='upper right', fontsize=22, ncol=2)

# 设置网格线
ax.grid(True, which='major', linestyle='--', linewidth=0.5)
plt.tight_layout()
# 保存图表到文件
plt.savefig('./Ablation.pdf', bbox_inches='tight')
plt.show()