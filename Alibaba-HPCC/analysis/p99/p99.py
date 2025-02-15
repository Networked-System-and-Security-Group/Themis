import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

plt.rcParams.update({'font.size': 36})

DCQCN_30_intra = 26.622335187765973
DCQCN_30_inter = 3.0997553104327733
Themis_30_intra = 21.76176981300918
Themis_30_inter = 10.599391862009087
Annulus_30_intra = 18.193460492223274
Annulus_30_inter = 3.5197604343730475
DCQCN_short_30_intra = 13.919651453393936
DCQCN_short_30_inter = 12.75163424756079

DCQCN_50_intra = 48.16751022700613
DCQCN_50_inter = 4.139842808200316
Themis_50_intra = 20.38954961296905
Themis_50_inter = 20.000069773577575
Annulus_50_intra = 25.239931903839143
Annulus_50_inter = 5.695685336363653
DCQCN_short_50_intra = 20.337761002628188
DCQCN_short_50_inter = 20.726386674617462

DCQCN_70_intra = 105.26244551616817
DCQCN_70_inter = 6.249856854087499
Themis_70_intra = 22.970269918424034
Themis_70_inter = 39.298989626251604
Annulus_70_intra = 47.98398999151483
Annulus_70_inter = 8.225828851681422
DCQCN_short_70_intra = 33.1917254702821
DCQCN_short_70_inter = 34.7777934600955

# 数据
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Themis', 'Annulus', 'DCQCN_short']

# 从全局变量中读取数据
data = {
    'intra': [
        [DCQCN_30_intra, Themis_30_intra, Annulus_30_intra, DCQCN_short_30_intra],
        [DCQCN_50_intra, Themis_50_intra, Annulus_50_intra, DCQCN_short_50_intra],
        [DCQCN_70_intra, Themis_70_intra, Annulus_70_intra, DCQCN_short_70_intra]
    ],
    'inter': [
        [DCQCN_30_inter, Themis_30_inter, Annulus_30_inter, DCQCN_short_30_inter],
        [DCQCN_50_inter, Themis_50_inter, Annulus_50_inter, DCQCN_short_50_inter],
        [DCQCN_70_inter, Themis_70_inter, Annulus_70_inter, DCQCN_short_70_inter]
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
    ax.set_ylabel('Normalized FCT P99')
    ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
    ax.set_xticklabels(labels)
    ax.grid(True)
    
    # 设置统一的y轴范围
    ax.set_ylim(0, max_values + 10.0)
    if max_values < 10:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        ax.yaxis.set_major_locator(ticker.MultipleLocator(30))
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)
    
    # 保存图表到文件
    plt.savefig(f'./p99_{flow_type}.pdf', bbox_inches='tight')