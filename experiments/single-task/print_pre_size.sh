#!/bin/bash

# Project root directory
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1
OUTPUT_DIR="$ROOT_DIR/SEN-PRE_COMPUTE"
RESULTS_CSV="$OUTPUT_DIR/dpu_time_pre_compute_results.csv"
PLOT_SCRIPT="$OUTPUT_DIR/generate_pre_compute_plot.py"

# Extract DPU execution times into CSV
echo "Extracting DPU time data and generating CSV..."

# Write the CSV header
echo "M,PRE_COMPUTE,DPU_time_ms" > "$RESULTS_CSV"

# Iterate over M directories
for M_DIR in "$OUTPUT_DIR"/M=*; do
    if [ -d "$M_DIR" ]; then
        # Extract M
        M=$(basename "$M_DIR" | cut -d'=' -f2)
        
        # Iterate over PRE_COMPUTE files for this M
        for PRE_FILE in "$M_DIR"/PRE_COMPUTE=*.txt; do
            if [ -f "$PRE_FILE" ]; then
                # Extract PRE_COMPUTE
                PRE_COMPUTE=$(basename "$PRE_FILE" .txt | cut -d'=' -f2)
                
                # Extract the first DPU time value
                DPU_TIME=$(grep -m 1 "DPU time:" "$PRE_FILE" | awk -F'DPU time:' '{print $2}' | awk '{print $1}' | tr -d 'ms')
                
                # Write a CSV row when extraction succeeds
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

# Create a Python script for the line plot
cat > "$PLOT_SCRIPT" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Read the CSV file
csv_file = "dpu_time_pre_compute_results.csv"
df = pd.read_csv(csv_file)

# Configure font support
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
    # Sort by PRE_COMPUTE
    m_data = m_data.sort_values('PRE_COMPUTE')
    
    # Create evenly spaced x-axis positions
    pre_compute_values = m_data['PRE_COMPUTE'].values
    x_positions = np.arange(len(pre_compute_values))
    
    plt.plot(x_positions, m_data['DPU_time_ms'], 
             marker='o', linewidth=2, markersize=8, 
             color=colors[i], label=f'M={m}')

# Configure plot properties
plt.xlabel('PRE_COMPUTE', fontsize=14)
plt.ylabel('DPU Time (ms)', fontsize=14)
plt.title('DPU Time vs PRE_COMPUTE for Different M Values', fontsize=16)
plt.legend(fontsize=12)
plt.grid(True, alpha=0.3)

# Use evenly spaced ticks labeled with the actual PRE_COMPUTE values
pre_compute_labels = sorted(df['PRE_COMPUTE'].unique())
plt.xticks(range(len(pre_compute_labels)), pre_compute_labels)

# Add value labels
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

# Save the plot
plt.savefig('dpu_time_vs_pre_compute.png', dpi=300, bbox_inches='tight')
plt.savefig('dpu_time_vs_pre_compute.pdf', bbox_inches='tight')

print("Plot saved as 'dpu_time_vs_pre_compute.png' and 'dpu_time_vs_pre_compute.pdf'")

# Print the data summary
print("\nData Summary:")
print(df.pivot_table(values='DPU_time_ms', index='PRE_COMPUTE', columns='M', aggfunc='mean'))
EOF

# Run the Python plotting script
echo "Generating plot..."
cd "$OUTPUT_DIR"
python3 "$PLOT_SCRIPT"

echo "Fourth experiment processing completed!"
echo "Results:"
echo "  - CSV file: $RESULTS_CSV"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_pre_compute.png"
echo "  - Plot: $OUTPUT_DIR/dpu_time_vs_pre_compute.pdf"