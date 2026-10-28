from pathlib import Path

ANALYSIS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = ANALYSIS_DIR.parents[1]

import pandas as pd
import matplotlib.pyplot as plt
import os
import re
import numpy as np

# ==================== Data extraction ====================
def extract_data():
    """Extract performance data from the directory layout."""
    thread_counts = list(range(2, 17,2))  # Even tasklet counts from 2 through 16
    base_dir_pattern = "DPU_THREAD_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for thread_count in thread_counts:
        dir_path = base_dir_pattern.format(thread_count)
        
        for nprobe in nprobes:
            file_path = str(PROJECT_ROOT / dir_path) + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # Extract all metrics
                        data = {
                            'dpu_threads': thread_count,
                            'nprobe': nprobe,
                            'recall': recall_map.get(nprobe, 0),
                            'qps': extract_metric(content, 'qps'),
                            'latency': extract_metric(content, 'latency'),
                            'latency_tail': extract_metric(content, 'latency_tail'),
                            'active_rate': extract_metric(content, 'avg_active_rate'),
                            'actual_recall': extract_metric(content, 'R@10')
                        }
                        all_data.append(data)
                        
                except Exception as e:
                    print(f"Error processing {file_path}: {str(e)}")
                    # Add a placeholder record
                    all_data.append({
                        'dpu_threads': thread_count,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"File not found: {file_path}")
                # Add placeholder records for missing files
                all_data.append({
                    'dpu_threads': thread_count,
                    'nprobe': nprobe,
                    'recall': recall_map.get(nprobe, 0)
                })
    
    df = pd.DataFrame(all_data)
    
    # Validate the data
    print("\nData columns:")
    print(df.columns.tolist())
    print("\nData statistics:")
    print(f"Total records: {len(df)}")
    print(f"DPU tasklet counts: {sorted(df['dpu_threads'].unique())}")
    print(f"nprobe values: {sorted(df['nprobe'].unique())}")
    
    df.to_csv(str(ANALYSIS_DIR / "data" / "dpu_thread_performance.csv"), index=False)
    return df

def extract_metric(content, metric_name):
    """Extract a metric value safely."""
    try:
        match = re.search(rf'{metric_name}=([\d.]+)', content)
        return float(match.group(1)) if match else np.nan
    except:
        return np.nan

# ==================== Data analysis ====================
def analyze_data(df):
    """Analyze the effect of DPU tasklet count on performance."""
    # Check data completeness
    required_cols = ['dpu_threads', 'nprobe', 'recall', 'qps', 'latency', 'latency_tail', 'active_rate']
    missing_cols = [col for col in required_cols if col not in df.columns]
    
    if missing_cols:
        print(f"\nError: missing required columns {missing_cols}")
        print("Available columns:", df.columns.tolist())
        return
    
    # Configure plot styling
    plt.style.use('ggplot')
    plt.rcParams.update({
        'font.sans-serif': 'SimHei',
        'axes.unicode_minus': False,
        'figure.titlesize': 16,
        'axes.grid': True,
        'grid.linestyle': '--',
        'grid.alpha': 0.6
    })
    
    # Create analysis plots
    fig, axes = plt.subplots(2, 2, figsize=(18, 12))
    fig.suptitle('Performance by DPU Tasklet Count and Recall', y=1.02)
    
    # Metric configuration
    metrics = [
        ('qps', 'Query throughput (QPS)', 'linear'),
        ('latency', 'Latency (ms)', 'linear'),
        ('latency_tail', 'Tail latency (ms)', 'linear'),
        ('active_rate', 'Mean active rate', 'linear')
    ]
    
    # Color scheme
    colors = [
        '#1f77b4', '#ff7f0e', '#2ca02c', 
        '#d62728', '#9467bd', '#8c564b'
    ]
    
    # Plot each metric
    for idx, (metric, name, scale) in enumerate(metrics):
        ax = axes[idx//2, idx%2]
        
        for i, nprobe in enumerate(sorted(df['nprobe'].unique())):
            subset = df[df['nprobe'] == nprobe].sort_values('dpu_threads')
            if subset.empty:
                continue
                
            recall = subset['recall'].iloc[0]
            color = colors[i % len(colors)]
            
            # Main curves
            ax.plot(
                subset['dpu_threads'],
                subset[metric],
                color=color,
                marker='o',
                linestyle='-',
                linewidth=2,
                markersize=8,
                label=f'R@{recall:.2f} (n={nprobe})'
            )
            
            # Data labels
            for x, y in zip(subset['dpu_threads'], subset[metric]):
                if not np.isnan(y):
                    ax.text(
                        x, y, f'{y:.1f}',
                        color=color,
                        ha='center',
                        va='bottom',
                        fontsize=8
                    )
        
        ax.set_title(name)
        ax.set_xlabel('DPU tasklet count')
        ax.set_ylabel(name)
        ax.set_xticks(sorted(df['dpu_threads'].unique()))
    
    # Legend and layout
    handles, labels = axes[0,0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc='upper right',
        bbox_to_anchor=(1.15, 0.9),
        title='Recall & nprobe'
    )
    
    plt.tight_layout()
    
    # Save results
    os.makedirs(str(ANALYSIS_DIR / "results"), exist_ok=True)
    output_path = os.path.join(str(ANALYSIS_DIR / "results"), "dpu_thread_performance_analysis.png")
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"\nAnalysis saved to: {output_path}")
    plt.show()

# ==================== Extended analysis ====================
def additional_analysis(df):
    """Analyze DPU tasklet scaling in more detail."""
    print("\n=== DPU Tasklet Scaling Analysis ===")
    
    # 1. Mean performance for each tasklet count
    print("\n1. Mean performance by DPU tasklet count:")
    thread_stats = df.groupby('dpu_threads').agg({
        'qps': ['mean', 'std', 'max'],
        'latency': ['mean', 'std', 'min'],
        'latency_tail': ['mean', 'std', 'min'],
        'active_rate': ['mean', 'std']
    }).round(2)
    print(thread_stats)
    
    # 2. Tasklet scaling efficiency
    print("\n2. DPU tasklet scaling efficiency:")
    thread_counts = sorted(df['dpu_threads'].unique())
    for i in range(1, len(thread_counts)):
        prev_threads = thread_counts[i-1]
        curr_threads = thread_counts[i]
        
        prev_data = df[df['dpu_threads'] == prev_threads]
        curr_data = df[df['dpu_threads'] == curr_threads]
        
        if not prev_data.empty and not curr_data.empty:
            qps_improvement = ((curr_data['qps'].mean() - prev_data['qps'].mean()) / prev_data['qps'].mean()) * 100
            scaling_efficiency = (qps_improvement / (curr_threads - prev_threads)) * 100
            
            print(f"From {prev_threads} to {curr_threads} tasklets:")
            print(f"  QPS improvement: {qps_improvement:.2f}%")
            print(f"  Scaling efficiency: {scaling_efficiency:.2f}% (per-tasklet improvement)")
    
    # 3. Create the scaling efficiency plot
    plt.figure(figsize=(12, 8))
    
    # Compute mean QPS for each tasklet count
    avg_qps_by_thread = df.groupby('dpu_threads')['qps'].mean()
    plt.plot(avg_qps_by_thread.index, avg_qps_by_thread.values, 
             marker='o', linewidth=3, markersize=10, color='#2ca02c',
             label='Measured performance')
    
    # Reference line for ideal linear scaling
    base_qps = avg_qps_by_thread.iloc[0]
    ideal_qps = [base_qps * t for t in avg_qps_by_thread.index]
    plt.plot(avg_qps_by_thread.index, ideal_qps, 
             linestyle='--', color='#ff7f0e', linewidth=2,
             label='Ideal linear scaling')
    
    plt.title('DPU Tasklet Scaling Efficiency')
    plt.xlabel('DPU tasklet count')
    plt.ylabel('Mean QPS')
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.xticks(sorted(df['dpu_threads'].unique()))
    plt.legend()
    
    # Annotate scaling efficiency
    for i in range(1, len(avg_qps_by_thread)):
        prev_val = avg_qps_by_thread.iloc[i-1]
        curr_val = avg_qps_by_thread.iloc[i]
        actual_improvement = curr_val - prev_val
        ideal_improvement = base_qps  # Ideal QPS increase per additional tasklet
        efficiency = (actual_improvement / ideal_improvement) * 100
        
        x_pos = (avg_qps_by_thread.index[i-1] + avg_qps_by_thread.index[i]) / 2
        y_pos = (prev_val + curr_val) / 2
        plt.annotate(f'{efficiency:.1f}%', 
                    xy=(x_pos, y_pos),
                    xytext=(0, 10),
                    textcoords='offset points',
                    ha='center',
                    fontsize=10,
                    color='blue' if efficiency > 80 else 'red')
    
    plt.tight_layout()
    efficiency_path = os.path.join(str(ANALYSIS_DIR / "results"), "dpu_thread_scaling_efficiency.png")
    plt.savefig(efficiency_path, dpi=300, bbox_inches='tight')
    print(f"\nScaling efficiency plot saved to: {efficiency_path}")
    
    # 4. Recommend tasklet counts
    print("\n3. Recommended DPU tasklet counts:")
    for recall in sorted(df['recall'].unique()):
        subset = df[df['recall'] == recall]
        if not subset.empty:
            best_qps = subset.loc[subset['qps'].idxmax()]
            best_cost_eff = subset.loc[(subset['qps'] / subset['dpu_threads']).idxmax()]
            
            print(f"Recall {recall:.2f}:")
            print(f"  Highest throughput: {best_qps['qps']:.1f} QPS (tasklets={best_qps['dpu_threads']})")
            print(f"  Best throughput per tasklet: {best_cost_eff['qps']:.1f} QPS (tasklets={best_cost_eff['dpu_threads']}, efficiency={best_cost_eff['qps']/best_cost_eff['dpu_threads']:.1f} QPS/tasklet)")

# ==================== Main program ====================
if __name__ == "__main__":
    print("=== DPU Tasklet Performance Analysis ===")
    print("DPU tasklet counts: even values from 2 through 16")
    
    # Load or extract data
    # try:
    #     df = pd.read_csv(str(ANALYSIS_DIR / "data" / "dpu_thread_performance.csv"))
    #     print("\nAnalyzing the existing data file...")
    # except FileNotFoundError:
    #     print("\nData file not found; extracting data...")
    df = extract_data()
    
    # Run the analysis
    if df is not None and not df.empty:
        print("\nData summary:")
        print(f"Valid records: {len(df)}")
        print(f"DPU tasklet counts: {sorted(df['dpu_threads'].unique())}")
        print(f"Number of nprobe configurations: {len(df['nprobe'].unique())}")
        
        analyze_data(df)
        additional_analysis(df)
    else:
        print("\nError: no valid data available for analysis")