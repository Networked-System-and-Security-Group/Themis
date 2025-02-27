import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 30})

# 数据
schemes = ['DCQCN_short','DCQCN_long']
data = {
    'intra': [5.808553868608757, 1.5039229580145075],
    'inter': [6.266539733900149, 13.661249892419512, ]
}
max_values = max(np.max(data['intra']), np.max(data['inter']))
# 颜色映射
colors = ['#D15354', '#3DA6AE', '#E8B86C', '#C6E3C6']
light_colors = ['#F5A6A8', '#AED8E6', '#FDE5A6', '#E2F5E2']  # 更浅的颜色版本

# 创建柱状图
fig, ax = plt.subplots(figsize=(10, 8))

# 柱子的宽度和位置
bar_width = 0.2
index = np.arange(len(schemes))

# 绘制 intra 和 inter 的柱子，并添加数值标签
for i, scheme in enumerate(schemes):
    # 绘制 intra 柱子
    intra_bar = ax.bar(index[i] - bar_width/2, data['intra'][i], bar_width, 
                       label='Intra' if i == 0 else "", color=colors[i], edgecolor='black')
    # 在 intra 柱子上添加数值标签
    ax.text(index[i] - bar_width/2, data['intra'][i], f'{data["intra"][i]:.2f}', 
            ha='center', va='bottom', fontsize=24, color='black')
    
    # 绘制 inter 柱子
    inter_bar = ax.bar(index[i] + bar_width/2, data['inter'][i], bar_width, 
                       label='Inter' if i == 0 else "", color=light_colors[i], edgecolor='black')
    # 在 inter 柱子上添加数值标签
    ax.text(index[i] + bar_width/2, data['inter'][i], f'{data["inter"][i]:.2f}', 
            ha='center', va='bottom', fontsize=24, color='black')

# 设置标签和标题
ax.set_xlabel('Scheme')
ax.set_ylabel('Normalized FCT')
ax.set_xticks(index)
ax.set_xticklabels(schemes)
ax.set_ylim(0, max_values + 1.0)
#ax.legend()

# 设置网格线
ax.grid(True, which='major', linestyle='--', linewidth=0.5)

# 保存图表到文件
plt.savefig('./Normalized_FCT.png', bbox_inches='tight')
plt.show()