#!/bin/bash

# 项目根目录
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
OUTPUT_DIR="$ROOT_DIR/TECH-TOPK"
RESULTS_CSV="$OUTPUT_DIR/tech_topk_comparison_results.csv"
PLOT_SCRIPT="$OUTPUT_DIR/generate_tech_topk_comparison_plot.py"

# 提取DPU时间数据并生成CSV
echo "Extracting DPU time data and generating CSV..."

# 创建CSV文件头部
echo "TOPK,Method,DPU_time_ms" > "$RESULTS_CSV"

# 遍历所有TOPK文件夹
for TOPK_DIR in "$OUTPUT_DIR"/TOPK=*; do
    if [ -d "$TOPK_DIR" ]; then
        # 提取TOPK值
        TOPK=$(basename "$TOPK_DIR" | cut -d'=' -f2)
        
        # 遍历该TOPK下的所有方法文件
        for METHOD_FILE in "$TOPK_DIR"/*.txt; do
            if [ -f "$METHOD_FILE" ]; then
                # 提取方法名称
                METHOD=$(basename "$METHOD_FILE" .txt)
                
                # 提取第一个DPU time值
                DPU_TIME=$(grep -m 1 "DPU time:" "$METHOD_FILE" | awk -F'DPU time:' '{print $2}' | awk '{print $1}' | tr -d 'ms')
                
                # 如果成功提取到数据，写入CSV
                if [ ! -z "$DPU_TIME" ]; then
                    echo "$TOPK,$METHOD,$DPU_TIME" >> "$RESULTS_CSV"
                    echo "Extracted: TOPK=$TOPK, Method=$METHOD, DPU_TIME=$DPU_TIME ms"
                else
                    echo "Warning: No DPU time found in $METHOD_FILE"
                fi
            fi
        done
    fi
done

echo "CSV file generated: $RESULTS_CSV"

# 创建Python脚本生成对比图表
cat > "$PLOT_SCRIPT" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# 读取CSV文件
csv_file = "tech_topk_comparison_results.csv"
df = pd.read_csv(csv_file)

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# 创建分组柱状图
plt.figure(figsize=(14, 8))

# 获取唯一的TOPK值和方法
topk_values = sorted(df['TOPK'].unique())
methods = df['Method'].unique()
x = np.arange(len(topk_values))
width = 0.35

# 为每种方法创建数据
for i, method in enumerate(methods):
    method_data = df[df['Method'] == method]
    method_data = method_data.sort_values('TOPK')
    plt.bar(x + i*width - width/2, method_data['DPU_time_ms'], 
            width, label=method, alpha=0.8)

# 设置图表属性
plt.xlabel('TOPK Value', fontsize=14)
plt.ylabel('DPU Time (ms)', fontsize=14)
plt.title('Performance Comparison: Baseline vs PIM-ANN for Different TOPK', fontsize=16)
plt.xticks(x, topk_values)
plt.legend(fontsize=12)
plt.grid(True, alpha=0.3, axis='y')

# 添加数值标签
for i, topk in enumerate(topk_values):
    for j, method in enumerate(methods):
        value = df[(df['TOPK'] == topk) & (df['Method'] == method)]['DPU_time_ms'].values
        if len(value) > 0:
            plt.annotate(f'{value[0]:.2f}', 
                        (x[i] + j*width - width/2, value[0]),
                        textcoords="offset points",
                        xytext=(0,10),
                        ha='center',
                        fontsize=9)

plt.tight_layout()

# 保存图像
plt.savefig('tech_topk_comparison.png', dpi=300, bbox_inches='tight')
plt.savefig('tech_topk_comparison.pdf', bbox_inches='tight')

print("Plot saved as 'tech_topk_comparison.png' and 'tech_topk_comparison.pdf'")

# 计算性能提升百分比
print("\nPerformance Improvement Analysis:")
for topk in topk_values:
    baseline_time = df[(df['TOPK'] == topk) & (df['Method'] == 'baseline')]['DPU_time_ms'].values
    pimann_time = df[(df['TOPK'] == topk) & (df['Method'] == 'pimann')]['DPU_time_ms'].values
    
    if len(baseline_time) > 0 and len(pimann_time) > 0:
        improvement = ((baseline_time[0] - pimann_time[0]) / baseline_time[0]) * 100
        print(f"TOPK={topk}: Baseline={baseline_time[0]:.2f}ms, PIM-ANN={pimann_time[0]:.2f}ms, Improvement={improvement:.2f}%")

# 输出数据摘要
print("\nData Summary:")
print(df.pivot_table(values='DPU_time_ms', index='TOPK', columns='Method', aggfunc='mean'))
EOF

# 运行Python脚本生成图表
echo "Generating comparison plot..."
cd "$OUTPUT_DIR"
python3 "$PLOT_SCRIPT"

echo "Sixth experiment processing completed!"
echo "Results:"
echo "  - CSV file: $RESULTS_CSV"
echo "  - Plot: $OUTPUT_DIR/tech_topk_comparison.png"
echo "  - Plot: $OUTPUT_DIR/tech_topk_comparison.pdf"