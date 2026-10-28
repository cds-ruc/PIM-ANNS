#!/bin/bash

# Project root directory
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
OUTPUT_DIR="$ROOT_DIR/TECH-TOPK"
RESULTS_CSV="$OUTPUT_DIR/tech_topk_comparison_results.csv"
PLOT_SCRIPT="$OUTPUT_DIR/generate_tech_topk_comparison_plot.py"

# Extract DPU execution times into CSV
echo "Extracting DPU time data and generating CSV..."

# Write the CSV header
echo "TOPK,Method,DPU_time_ms" > "$RESULTS_CSV"

# Iterate over TOPK directories
for TOPK_DIR in "$OUTPUT_DIR"/TOPK=*; do
    if [ -d "$TOPK_DIR" ]; then
        # Extract TOPK
        TOPK=$(basename "$TOPK_DIR" | cut -d'=' -f2)
        
        # Iterate over method files for this TOPK
        for METHOD_FILE in "$TOPK_DIR"/*.txt; do
            if [ -f "$METHOD_FILE" ]; then
                # Extract the method name
                METHOD=$(basename "$METHOD_FILE" .txt)
                
                # Extract the first DPU time value
                DPU_TIME=$(grep -m 1 "DPU time:" "$METHOD_FILE" | awk -F'DPU time:' '{print $2}' | awk '{print $1}' | tr -d 'ms')
                
                # Write a CSV row when extraction succeeds
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

# Create a Python script for the comparison plot
cat > "$PLOT_SCRIPT" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Read the CSV file
csv_file = "tech_topk_comparison_results.csv"
df = pd.read_csv(csv_file)

# Configure font support
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# Create grouped bars
plt.figure(figsize=(14, 8))

# Get distinct TOPK values and methods
topk_values = sorted(df['TOPK'].unique())
methods = df['Method'].unique()
x = np.arange(len(topk_values))
width = 0.35

# Prepare data for each method
for i, method in enumerate(methods):
    method_data = df[df['Method'] == method]
    method_data = method_data.sort_values('TOPK')
    plt.bar(x + i*width - width/2, method_data['DPU_time_ms'], 
            width, label=method, alpha=0.8)

# Configure plot properties
plt.xlabel('TOPK Value', fontsize=14)
plt.ylabel('DPU Time (ms)', fontsize=14)
plt.title('Performance Comparison: Baseline vs PIM-ANN for Different TOPK', fontsize=16)
plt.xticks(x, topk_values)
plt.legend(fontsize=12)
plt.grid(True, alpha=0.3, axis='y')

# Add value labels
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

# Save the plot
plt.savefig('tech_topk_comparison.png', dpi=300, bbox_inches='tight')
plt.savefig('tech_topk_comparison.pdf', bbox_inches='tight')

print("Plot saved as 'tech_topk_comparison.png' and 'tech_topk_comparison.pdf'")

# Compute the percentage performance improvement
print("\nPerformance Improvement Analysis:")
for topk in topk_values:
    baseline_time = df[(df['TOPK'] == topk) & (df['Method'] == 'baseline')]['DPU_time_ms'].values
    pimann_time = df[(df['TOPK'] == topk) & (df['Method'] == 'pimann')]['DPU_time_ms'].values
    
    if len(baseline_time) > 0 and len(pimann_time) > 0:
        improvement = ((baseline_time[0] - pimann_time[0]) / baseline_time[0]) * 100
        print(f"TOPK={topk}: Baseline={baseline_time[0]:.2f}ms, PIM-ANN={pimann_time[0]:.2f}ms, Improvement={improvement:.2f}%")

# Print the data summary
print("\nData Summary:")
print(df.pivot_table(values='DPU_time_ms', index='TOPK', columns='Method', aggfunc='mean'))
EOF

# Run the Python plotting script
echo "Generating comparison plot..."
cd "$OUTPUT_DIR"
python3 "$PLOT_SCRIPT"

echo "Sixth experiment processing completed!"
echo "Results:"
echo "  - CSV file: $RESULTS_CSV"
echo "  - Plot: $OUTPUT_DIR/tech_topk_comparison.png"
echo "  - Plot: $OUTPUT_DIR/tech_topk_comparison.pdf"