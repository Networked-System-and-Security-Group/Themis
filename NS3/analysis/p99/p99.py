import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 36})

DCQCN_30_intra = 47.922062486808684
DCQCN_30_inter = 2.657482069317924
Themis_30_intra = 21.642301411329626
Themis_30_inter = 3.6716299139535424
Annulus_30_intra = 39.20095680494186
Annulus_30_inter = 2.716834268326197
DCQCN_short_30_intra = 12.68617340242797
DCQCN_short_30_inter = 12.503061936848779
BiCC_30_intra = 45.71729383198816
BiCC_30_inter = 272.04598915426925

DCQCN_50_intra = 114.75866206954558
DCQCN_50_inter = 5.256495457076515
Themis_50_intra = 16.878254361362988
Themis_50_inter = 13.808769021312324
Annulus_50_intra = 59.40140971234783
Annulus_50_inter = 4.0913730624956015
DCQCN_short_50_intra = 34.21098710384513
DCQCN_short_50_inter = 25.099383831723408
BiCC_50_intra = 73.77160935290345
BiCC_50_inter = 279.37311321046036

DCQCN_70_intra = 153.1594140882082
DCQCN_70_inter = 7.311303338030217
Themis_70_intra = 25.104862597272394
Themis_70_inter = 27.230479605977568
Annulus_70_intra = 91.76991541982909
Annulus_70_inter = 5.364803413474869
DCQCN_short_70_intra = 50.986367275354844
DCQCN_short_70_inter = 39.2814371556321
BiCC_70_intra = 89.01739109228531
BiCC_70_inter = 181.3333237339305

# 数据
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Themis', 'Annulus','BiCC', 'DCQCN_short']

# 从全局变量中读取数据
data = {
    'intra': [
        [DCQCN_30_intra, Themis_30_intra, Annulus_30_intra, BiCC_30_intra, DCQCN_short_30_intra],
        [DCQCN_50_intra, Themis_50_intra, Annulus_50_intra, BiCC_50_intra, DCQCN_short_50_intra],
        [DCQCN_70_intra, Themis_70_intra, Annulus_70_intra, BiCC_70_intra, DCQCN_short_70_intra]
    ],
    'inter': [
        [DCQCN_30_inter, Themis_30_inter, Annulus_30_inter, BiCC_30_inter, DCQCN_short_30_inter],
        [DCQCN_50_inter, Themis_50_inter, Annulus_50_inter, BiCC_50_inter, DCQCN_short_50_inter],
        [DCQCN_70_inter, Themis_70_inter, Annulus_70_inter, BiCC_70_inter, DCQCN_short_70_inter]
    ]
}

max_values = max(np.max(data['intra']), np.max(data['inter']))

# 绘制柱状图
for flow_type in ['intra', 'inter']:
    fig, ax = plt.subplots(figsize=(10, 6))
    values = np.array(data[flow_type])
    bar_width = 0.18
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
            color = '#BEB8DC'
        
        ax.bar(index + k * bar_width, values[:, k], bar_width, label=scheme, color=color, edgecolor='black')
    
    ax.set_xlabel('Load')
    ax.set_ylabel('Normalized FCT P99',fontsize=32)
    ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
    ax.set_xticklabels(labels)
    ax.grid(True)
    
    # 设置统一的y轴范围
    ax.set_ylim(0, max_values + 10.0)
    if max_values < 10:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(50))
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)
    
    # 保存图表到文件
    plt.savefig(f'./p99_{flow_type}.pdf', bbox_inches='tight')