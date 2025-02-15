DCQCN_30_intra_ratio_low = 3.743194213219743
DCQCN_30_intra_ratio_middle = 2.752064820635088
DCQCN_30_intra_ratio_high = 4.649529581966721
DCQCN_30_inter_ratio_low = 1.0221070099848355
DCQCN_30_inter_ratio_middle = 1.0267941629162893
DCQCN_30_inter_ratio_high = 1.2986369100034478

Themis_30_intra_ratio_low = 2.1028000378954546
Themis_30_intra_ratio_middle = 2.2014319471692443
Themis_30_intra_ratio_high = 3.8085426410110603
Themis_30_inter_ratio_low = 1.127732279160492
Themis_30_inter_ratio_middle = 1.1969662656036253
Themis_30_inter_ratio_high = 2.2500925392109425

Annulus_30_intra_ratio_low = 2.0480968551472345
Annulus_30_intra_ratio_middle = 2.259021314853643
Annulus_30_intra_ratio_high = 4.120447132049434
Annulus_30_inter_ratio_low = 1.0137085999887496
Annulus_30_inter_ratio_middle = 1.0165852754226579
Annulus_30_inter_ratio_high = 1.4377594274197996

DCQCN_short_30_intra_ratio_low =  1.6193644615927678
DCQCN_short_30_intra_ratio_middle = 1.794859265098919
DCQCN_short_30_intra_ratio_high = 3.94981577451061
DCQCN_short_30_inter_ratio_low = 1.2339195603278625
DCQCN_short_30_inter_ratio_middle = 1.5171541980645726
DCQCN_short_30_inter_ratio_high = 4.173843496994727

DCQCN_50_intra_ratio_low = 6.763036398437469
DCQCN_50_intra_ratio_middle = 6.4206160657573434
DCQCN_50_intra_ratio_high = 7.803495978534738
DCQCN_50_inter_ratio_low = 1.0577474935561815
DCQCN_50_inter_ratio_middle = 1.0669492826925269
DCQCN_50_inter_ratio_high = 1.5044832375426385

Themis_50_intra_ratio_low = 2.6738573399747176
Themis_50_intra_ratio_middle = 2.67496987979568
Themis_50_intra_ratio_high = 5.572107615428324
Themis_50_inter_ratio_low = 2.0004396686226893
Themis_50_inter_ratio_middle = 2.058910566988038
Themis_50_inter_ratio_high = 4.712539237454388

Annulus_50_intra_ratio_low = 3.5883006240090993
Annulus_50_intra_ratio_middle = 3.2207470501962945
Annulus_50_intra_ratio_high = 6.448231983546405
Annulus_50_inter_ratio_low = 1.0186935483923754
Annulus_50_inter_ratio_middle = 1.0265147433421207
Annulus_50_inter_ratio_high = 1.8210655967882936

DCQCN_short_50_intra_ratio_low = 2.2541073199637194
DCQCN_short_50_intra_ratio_middle = 2.5369068581238063
DCQCN_short_50_intra_ratio_high = 6.1939578572085825
DCQCN_short_50_inter_ratio_low = 1.9800879263731368
DCQCN_short_50_inter_ratio_middle = 2.104183044768263
DCQCN_short_50_inter_ratio_high = 6.205003650965304

DCQCN_70_intra_ratio_low = 16.624328032658607
DCQCN_70_intra_ratio_middle = 13.448509520626363
DCQCN_70_intra_ratio_high = 12.827010107197918
DCQCN_70_inter_ratio_low =  1.1924626283387774
DCQCN_70_inter_ratio_middle = 1.176125744335935
DCQCN_70_inter_ratio_high = 1.8985236187282872

Themis_70_intra_ratio_low = 2.976093913725767
Themis_70_intra_ratio_middle = 2.911433579876146
Themis_70_intra_ratio_high = 6.615860570322376
Themis_70_inter_ratio_low = 4.881762686216705
Themis_70_inter_ratio_middle = 5.0758678308684075
Themis_70_inter_ratio_high = 10.794735292110088

Annulus_70_intra_ratio_low = 7.614766696314694
Annulus_70_intra_ratio_middle = 6.566926470280743
Annulus_70_intra_ratio_high = 10.332495040560456
Annulus_70_inter_ratio_low = 1.06134588088536
Annulus_70_inter_ratio_middle = 1.076376560294338
Annulus_70_inter_ratio_high = 2.3217571514311244

DCQCN_short_70_intra_ratio_low = 3.537176518257445
DCQCN_short_70_intra_ratio_middle = 3.714388185564939
DCQCN_short_70_intra_ratio_high = 9.282080559792755
DCQCN_short_70_inter_ratio_low = 3.171071610962165
DCQCN_short_70_inter_ratio_middle = 2.8693736284122355
DCQCN_short_70_inter_ratio_high = 9.298177365196217

import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker
plt.rcParams.update({'font.size': 38})
# 数据
labels = ['30%', '50%', '70%']
schemes = ['DCQCN', 'Themis', 'Annulus', 'DCQCN_short']
hatches = ['' '', '', '']  # 填充样式

# 从全局变量中读取数据
data = {
    'low': {
        'intra': [
            [DCQCN_30_intra_ratio_low, Themis_30_intra_ratio_low, Annulus_30_intra_ratio_low, DCQCN_short_30_intra_ratio_low],
            [DCQCN_50_intra_ratio_low, Themis_50_intra_ratio_low, Annulus_50_intra_ratio_low, DCQCN_short_50_intra_ratio_low],
            [DCQCN_70_intra_ratio_low, Themis_70_intra_ratio_low, Annulus_70_intra_ratio_low, DCQCN_short_70_intra_ratio_low]
        ],
        'inter': [
            [DCQCN_30_inter_ratio_low, Themis_30_inter_ratio_low, Annulus_30_inter_ratio_low, DCQCN_short_30_inter_ratio_low],
            [DCQCN_50_inter_ratio_low, Themis_50_inter_ratio_low, Annulus_50_inter_ratio_low, DCQCN_short_50_inter_ratio_low],
            [DCQCN_70_inter_ratio_low, Themis_70_inter_ratio_low, Annulus_70_inter_ratio_low, DCQCN_short_70_inter_ratio_low]
        ]
    },
    'middle': {
        'intra': [
            [DCQCN_30_intra_ratio_middle, Themis_30_intra_ratio_middle, Annulus_30_intra_ratio_middle, DCQCN_short_30_intra_ratio_middle],
            [DCQCN_50_intra_ratio_middle, Themis_50_intra_ratio_middle, Annulus_50_intra_ratio_middle, DCQCN_short_50_intra_ratio_middle],
            [DCQCN_70_intra_ratio_middle, Themis_70_intra_ratio_middle, Annulus_70_intra_ratio_middle, DCQCN_short_70_intra_ratio_middle]
        ],
        'inter': [
            [DCQCN_30_inter_ratio_middle, Themis_30_inter_ratio_middle, Annulus_30_inter_ratio_middle, DCQCN_short_30_inter_ratio_middle],
            [DCQCN_50_inter_ratio_middle, Themis_50_inter_ratio_middle, Annulus_50_inter_ratio_middle, DCQCN_short_50_inter_ratio_middle],
            [DCQCN_70_inter_ratio_middle, Themis_70_inter_ratio_middle, Annulus_70_inter_ratio_middle, DCQCN_short_70_inter_ratio_middle]
        ]
    },
    'high': {
        'intra': [
            [DCQCN_30_intra_ratio_high, Themis_30_intra_ratio_high, Annulus_30_intra_ratio_high, DCQCN_short_30_intra_ratio_high],
            [DCQCN_50_intra_ratio_high, Themis_50_intra_ratio_high, Annulus_50_intra_ratio_high, DCQCN_short_50_intra_ratio_high],
            [DCQCN_70_intra_ratio_high, Themis_70_intra_ratio_high, Annulus_70_intra_ratio_high, DCQCN_short_70_intra_ratio_high]
        ],
        'inter': [
            [DCQCN_30_inter_ratio_high, Themis_30_inter_ratio_high, Annulus_30_inter_ratio_high, DCQCN_short_30_inter_ratio_high],
            [DCQCN_50_inter_ratio_high, Themis_50_inter_ratio_high, Annulus_50_inter_ratio_high, DCQCN_short_50_inter_ratio_high],
            [DCQCN_70_inter_ratio_high, Themis_70_inter_ratio_high, Annulus_70_inter_ratio_high, DCQCN_short_70_inter_ratio_high]
        ]
    }
}

# 计算每个负载类型的最大值
max_values = {
    'low': max(np.max(data['low']['intra']), np.max(data['low']['inter'])),
    'middle': max(np.max(data['middle']['intra']), np.max(data['middle']['inter'])),
    'high': max(np.max(data['high']['intra']), np.max(data['high']['inter']))
}

# 绘制柱状图
for load in ['low', 'middle', 'high']:
    for flow_type in ['intra', 'inter']:
        fig, ax = plt.subplots(figsize=(10, 6))
        values = np.array(data[load][flow_type])
        bar_width = 0.2
        index = np.arange(len(labels))
        
        for k, scheme in enumerate(schemes):
            if scheme == 'DCQCN':
                color = '#D15354'
                hatch = ''
            elif scheme == 'Themis':
                color = '#3DA6AE'
                hatch = ''
            elif scheme == 'Annulus':
                color = '#E8B86C'
                hatch = ''
            elif scheme == 'DCQCN_short':
                color = '#C6E3C6'
                hatch = ''
            else:
                color = 'white'
                hatch = ''
            
            ax.bar(index + k * bar_width, values[:, k], bar_width, label=scheme, hatch=hatch, color=color, edgecolor='black')
        
        ax.set_xlabel('Load')
        ax.set_ylabel('Normalized FCT')
        ax.set_xticks(index + bar_width * (len(schemes) - 1) / 2)
        ax.set_xticklabels(labels)
        ax.grid(True)
        
        # 设置统一的y轴范围
        ax.set_ylim(0, max_values[load]+1)
        ax.yaxis.set_major_locator(ticker.MultipleLocator(2))
        ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
        ax.grid(True, which='major', linestyle='dashed', linewidth=0.5)
        # 保存图表到文件
        plt.savefig(f'./webSearch_1_1_{load}_{flow_type}.pdf', bbox_inches='tight')
fig_legend, ax_legend = plt.subplots(figsize=(10, 1))
ax_legend.axis('off')  # 隐藏坐标轴
handles, labels = ax.get_legend_handles_labels()
fig_legend.legend(handles, labels, loc='center', ncol=4)  # 横着展示为一排
plt.savefig('legend.pdf', bbox_inches='tight')