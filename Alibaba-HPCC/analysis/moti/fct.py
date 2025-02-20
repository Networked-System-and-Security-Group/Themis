DCQCN_30_intra = 3.787822408972657
DCQCN_30_inter = 1.1508936037026862

DCQCN_50_intra = 7.1372127997665205
DCQCN_50_inter = 1.265274724694818

DCQCN_70_intra = 13.661249892419512
DCQCN_70_inter = 1.5039229580145075

DCQCN_short_30_intra = 2.766863668603996
DCQCN_short_30_inter = 2.678158098473256

DCQCN_short_50_intra = 4.25084803175487
DCQCN_short_50_inter = 3.9571922087543547

DCQCN_short_70_intra =  6.266539733900149
DCQCN_short_70_inter = 5.808553868608757

import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 36})

# 数据
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'DCQCN_short']

# 从全局变量中读取数据
data = {
    'intra': [
        [DCQCN_30_intra,  DCQCN_short_30_intra],
        [DCQCN_50_intra,  DCQCN_short_50_intra],
        [DCQCN_70_intra,  DCQCN_short_70_intra]
    ],
    'inter': [
        [DCQCN_30_inter,  DCQCN_short_30_inter],
        [DCQCN_50_inter,  DCQCN_short_50_inter],
        [DCQCN_70_inter,  DCQCN_short_70_inter]
    ]
}

max_values = max(np.max(data['intra']), np.max(data['inter']))

# 绘制柱状图
for flow_type in ['intra', 'inter']:
    fig, ax = plt.subplots(figsize=(10, 6))
    values = np.array(data[flow_type])
    bar_width = 0.2
    index = np.arange(len(labels))
    
    for k, scheme in enumerate(schemes):
        if scheme == 'DCQCN':
            color = '#D15354'
        elif scheme == 'Themis':
            color = '#3DA6AE'
        elif scheme == 'Annulus':
            color = '#E8B86C'
        elif scheme == 'DCQCN_short':
            color = '#C6E3C6'
        else:
            color = 'white'
        
        ax.bar(index + k * bar_width, values[:, k], bar_width, label=scheme, color=color, edgecolor='black')
    
    ax.set_xlabel('Load')
    ax.set_ylabel('Normalized FCT')
    ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
    ax.set_xticklabels(labels)
    # if flow_type == 'inter':
    #     ax.legend(loc='upper right')
    ax.grid(True)
    
    # 设置统一的y轴范围
    ax.set_ylim(0, max_values + 1.0)
    if max_values < 10:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(2))
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)
    
    # 保存图表到文件
    plt.savefig(f'./{flow_type}.pdf', bbox_inches='tight')

fig_legend, ax_legend = plt.subplots(figsize=(10, 1))
ax_legend.axis('off')  # 隐藏坐标轴
handles, labels = ax.get_legend_handles_labels()
fig_legend.legend(handles, labels, loc='center', ncol=2)  # 横着展示为一排
plt.savefig('legend.pdf', bbox_inches='tight')