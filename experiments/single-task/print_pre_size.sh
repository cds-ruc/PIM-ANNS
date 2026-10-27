#!/bin/bash

# 项目根目录
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
OUTPUT_DIR="$ROOT_DIR/SEN-PRE_COMPUTE"
RESULTS_CSV="$OUTPUT_DIR/dpu_time_pre_compute_results.csv"
PLOT_SCRIPT="$OUTPUT_DIR/generate_pre_compute_plot.py"

# 提取DPU时间数据并生成CSV
echo "Extracting DPU time data and generating CSV..."

# 创建CSV文件头部
echo "M,PRE_COMPUTE,DPU_time_ms" > "$RESULTS_CSV"

# 遍历所有M文件夹
for M_DIR in "$OUTPUT_DIR"/M=*; do
    if [ -d "$M_DIR" ]; then
        # 提取M值
        M=$(basename "$M_DIR" | cut -d'=' -f2)
        
        # 遍历该M下的所有PRE_COMPUTE文件
        for PRE_FILE in "$M_DIR"/PRE_COMPUTE=*.txt; do
            if [ -f "$PRE_FILE" ]; then
                # 提取PRE_COMPUTE值
                PRE_COMPUTE=$(basename "$PRE_FILE" .txt | cut -d'=' -f2)
                
                # 提取第一个DPU time值
                DPU_TIME=$(grep -m 1 "DPU time:" "$PRE_FILE" | awk -F'DPU time:' '{print $2}' | awk '{print $1}' | tr -d 'ms')
                
                # 如果成功提取到数据，写入CSV
                if [ ! -z "$DPU_TIME" ]; then
                    echo "$M,$PRE_COMPUTE,$DPU_TIME" >> "$RESULTS_CSV"
                    echo "Extracted: M=$M, PRE_COMPUTE=$PRE_COMPUTE, DPU_TIME=$DPU_TIME ms"
                else
                    echo "Warning: No DPU time found in $PRE_FILE"
                fi
            fi
        done
    fi
done

echo "CSV file generated: $RESULTS_CSV"

# 创建Python脚本生成折线图
cat > "$PLOT_SCRIPT" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# 读取CSV文件
csv_file = "dpu_time_pre_compute_results.csv"
df = pd.read_csv(csv_file)

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# 创建折线图
plt.figure(figsize=(12, 8))

# 获取唯一的M值
m_values = sorted(df['M'].unique())
colors = plt.cm.Set3(np.linspace(0, 1, len(m_values)))

# 为每个M值绘制一条折线
for i, m in enumerate(m_values):
    m_data = df[df['M'] == m]
    # 按PRE_COMPUTE排序
    m_data = m_data.sort_values('PRE_COMPUTE')
    
    # 创建等间距的x轴位置
    pre_compute_values = m_data['PRE_COMPUTE'].values
    x_positions = np.arange(len(pre_compute_values))
    
    plt.plot(x_positions, m_data['DPU_time_ms'], 
             marker='o', linewidth=2, markersize=8, 
             color=colors[i], label=f'M={m}')

# 设置图表属性
plt.xlabel('PRE_COMPUTE', fontsize=14)
plt.ylabel('DPU Time (ms)', fontsize=14)
plt.title('DPU Time vs PRE_COMPUTE for Different M Values', fontsize=16)
plt.legend(fontsize=12)
plt.grid(True, alpha=0.3)

# 设置x轴刻度为等间距，标签为实际的PRE_COMPUTE值
pre_compute_labels = sorted(df['PRE_COMPUTE'].unique())
plt.xticks(range(len(pre_compute_labels)), pre_compute_labels)

# 添加数值标签
for m in m_values:
    m_data = df[df['M'] == m].sort_values('PRE_COMPUTE')
    pre_compute_values = m_data['PRE_COMPUTE'].values
    x_positions = np.arange(len(pre_compute_values))
    
    for idx, row in m_data.iterrows():
        plt.annotate(f'{row["DPU_time_ms"]:.2f}', 
                    (x_positions[list(pre_compute_values).index(row['PRE_COMPUTE'])], row['DPU_time_ms']),
                    textcoords="offset points",
                    xytext=(0,10),
                    ha='center',
                    fontsize=9)

plt.tight_layout()

# 保存图像
plt.savefig('dpu_time_vs_pre_compute.png', dpi=300, bbox_inches='tight')
plt.savefig('dpu_time_vs_pre_compute.pdf', bbox_inches='tight')

print("Plot saved as 'dpu_time_vs_pre_compute.png' and 'dpu_time_vs_pre_compute.pdf'")

# 输出数据摘要
print("\nData Summary:")
print(df.pivot_table(values='DPU_time_ms', index='PRE_COMPUTE', columns='M', aggfunc='mean'))
EOF

# 运行Python脚本生成图表
echo "Generating plot..."
cd "$OUTPUT_DIR"
python3 "$PLOT_SCRIPT"

echo "Fourth experiment processing completed!"
echo "Results:"
echo "  - CSV file: $RESULTS_CSV"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_pre_compute.png"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_pre_compute.pdf"