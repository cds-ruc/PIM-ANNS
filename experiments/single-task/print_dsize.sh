#!/bin/bash

# Project root directory
ROOT_DIR="/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo"
OUTPUT_DIR="$ROOT_DIR/SEN-DSIZE"
RESULTS_CSV="$OUTPUT_DIR/dpu_time_results.csv"
PLOT_SCRIPT="$OUTPUT_DIR/generate_plot.py"

# Extract DPU execution times into CSV
echo "Extracting DPU time data and generating CSV..."

# Write the CSV header
echo "M,DSIZE,DPU_time_ms" > "$RESULTS_CSV"

# Iterate over M directories
for M_DIR in "$OUTPUT_DIR"/M=*; do
    if [ -d "$M_DIR" ]; then
        # Extract M
        M=$(basename "$M_DIR" | cut -d'=' -f2)
        
        # Iterate over DSIZE files for this M
        for DSIZE_FILE in "$M_DIR"/DSIZE=*.txt; do
            if [ -f "$DSIZE_FILE" ]; then
                # Extract DSIZE
                DSIZE=$(basename "$DSIZE_FILE" .txt | cut -d'=' -f2)
                
                # Extract the first occurrence of DPU time
                DPU_TIME=$(grep -m 1 "DPU time:" "$DSIZE_FILE" | awk -F'DPU time:' '{print $2}' | awk '{print $1}' | tr -d 'ms')
                
                # Write a CSV row when extraction succeeds
                if [ ! -z "$DPU_TIME" ]; then
                    echo "$M,$DSIZE,$DPU_TIME" >> "$RESULTS_CSV"
                    echo "Extracted: M=$M, DSIZE=$DSIZE, DPU_TIME=$DPU_TIME ms"
                else
                    echo "Warning: No DPU time found in $DSIZE_FILE"
                fi
            fi
        done
    fi
done

echo "CSV file generated: $RESULTS_CSV"

# Create a Python script for the line plot
cat > "$PLOT_SCRIPT" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Read the CSV file
csv_file = "dpu_time_results.csv"
df = pd.read_csv(csv_file)

# Configure font support (optional)
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# Create the line plot
plt.figure(figsize=(12, 8))

# Get distinct M values
m_values = sorted(df['M'].unique())
colors = plt.cm.Set3(np.linspace(0, 1, len(m_values)))

# Plot one curve per M value
for i, m in enumerate(m_values):
    m_data = df[df['M'] == m]
    # Sort by DSIZE
    m_data = m_data.sort_values('DSIZE')
    plt.plot(m_data['DSIZE'], m_data['DPU_time_ms'], 
             marker='o', linewidth=2, markersize=8, 
             color=colors[i], label=f'M={m}')

# Configure plot properties
plt.xlabel('DSIZE', fontsize=14)
plt.ylabel('DPU Time (ms)', fontsize=14)
plt.title('DPU Time vs DSIZE for Different M Values', fontsize=16)
plt.legend(fontsize=12)
plt.grid(True, alpha=0.3)

# Use the measured DSIZE values as x-axis ticks
plt.xticks(df['DSIZE'].unique())

# Add value labels (optional)
for m in m_values:
    m_data = df[df['M'] == m].sort_values('DSIZE')
    for _, row in m_data.iterrows():
        plt.annotate(f'{row["DPU_time_ms"]:.2f}', 
                    (row['DSIZE'], row['DPU_time_ms']),
                    textcoords="offset points",
                    xytext=(0,10),
                    ha='center',
                    fontsize=9)

plt.tight_layout()

# Save the plot
plt.savefig('dpu_time_vs_dsize.png', dpi=300, bbox_inches='tight')
plt.savefig('dpu_time_vs_dsize.pdf', bbox_inches='tight')

print("Plot saved as 'dpu_time_vs_dsize.png' and 'dpu_time_vs_dsize.pdf'")

# Display the plot when a graphical environment is available
# plt.show()

# Also print the data summary
print("\nData Summary:")
print(df.pivot_table(values='DPU_time_ms', index='DSIZE', columns='M', aggfunc='mean'))
EOF

# Run the Python plotting script
echo "Generating plot..."
cd "$OUTPUT_DIR"
python3 "$PLOT_SCRIPT"

echo "Processing completed!"
echo "Results:"
echo "  - CSV file: $RESULTS_CSV"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_dsize.png"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_dsize.pdf"