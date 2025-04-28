import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 36})

DCQCN_30_intra = 5.2134085921559175
DCQCN_30_inter = 1.152093070837611
Themis_30_intra = 2.759711673257584
Themis_30_inter =  1.2297164620280596
Annulus_30_intra = 3.961587589178529
Annulus_30_inter =  1.1288205398798687
DCQCN_short_30_intra = 2.944367771180545
DCQCN_short_30_inter = 2.745148584995012
BiCC_30_intra = 3.668505178341206
BiCC_30_inter = 5.929514045975628

DCQCN_50_intra = 14.01681547952418
DCQCN_50_inter = 1.455170239471604
Themis_50_intra = 3.77480872038226
Themis_50_inter =  2.0972482812201036
Annulus_50_intra = 7.4930822353633735
Annulus_50_inter = 1.2941000100708668
DCQCN_short_50_intra = 5.406088867325246
DCQCN_short_50_inter = 4.7639138120972495
BiCC_50_intra = 6.571438576913168
BiCC_50_inter = 7.103853588360132

DCQCN_70_intra = 23.57516886943873
DCQCN_70_inter = 1.83039458043768
Themis_70_intra = 4.912151116381316
Themis_70_inter = 5.244815204406885
Annulus_70_intra = 13.516062854420836
Annulus_70_inter = 1.5017817984546835
DCQCN_short_70_intra = 9.93293706902557
DCQCN_short_70_inter = 8.66125152285964
BiCC_70_intra = 11.05322896601822
BiCC_70_inter = 6.081552968363342

# 数据
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Annulus','BiCC', 'Themis']

# 从全局变量中读取数据
data = {
    'intra': [
        [DCQCN_30_intra, Annulus_30_intra, BiCC_30_intra, Themis_30_intra],
        [DCQCN_50_intra, Annulus_50_intra, BiCC_50_intra, Themis_50_intra],
        [DCQCN_70_intra, Annulus_70_intra, BiCC_70_intra, Themis_70_intra]
    ],
    'inter': [
        [DCQCN_30_inter, Annulus_30_inter, BiCC_30_inter, Themis_30_inter],
        [DCQCN_50_inter, Annulus_50_inter, BiCC_50_inter, Themis_50_inter],
        [DCQCN_70_inter, Annulus_70_inter, BiCC_70_inter, Themis_70_inter]
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
        elif scheme == 'BiCC':
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
    ax.set_ylim(0, 30)
    if max_values < 10:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(5))
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)
    
    # 保存图表到文件
    plt.savefig(f'./p99_{flow_type}2.png', bbox_inches='tight')