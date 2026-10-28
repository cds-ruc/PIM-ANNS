import pandas as pd
import matplotlib.pyplot as plt
import os
import re
import numpy as np

# ==================== Data extraction ====================
def extract_data():
    """Extract performance data from the directory layout."""
    cache_sizes = [2,4,6, 8, 10,12, 16, 20]
    base_dir_pattern = "CACHE_SIZE_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for cache_size in cache_sizes:
        dir_path = base_dir_pattern.format(cache_size)
        
        for nprobe in nprobes:
            file_path = dir_path + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # Extract all metrics
                        data = {
                            'cache_size': cache_size,
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
                        'cache_size': cache_size,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"File not found: {file_path}")
                # Add placeholder records for missing files
                all_data.append({
                    'cache_size': cache_size,
                    'nprobe': nprobe,
                    'recall': recall_map.get(nprobe, 0)
                })
    
    df = pd.DataFrame(all_data)
    
    # Validate the data
    print("\nData columns:")
    print(df.columns.tolist())
    print("\nData statistics:")
    print(f"Total records: {len(df)}")
    print(f"Cache sizes: {sorted(df['cache_size'].unique())}")
    print(f"nprobe values: {sorted(df['nprobe'].unique())}")
    
    df.to_csv("cache_performance.csv", index=False)
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
    """Analyze the effect of cache size on performance."""
    # Check data completeness
    required_cols = ['cache_size', 'nprobe', 'recall', 'qps', 'latency', 'latency_tail', 'active_rate']
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
    fig.suptitle('Performance by Cache Size and Recall', y=1.02)
    
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
            subset = df[df['nprobe'] == nprobe].sort_values('cache_size')
            if subset.empty:
                continue
                
            recall = subset['recall'].iloc[0]
            color = colors[i % len(colors)]
            
            # Main curves
            ax.plot(
                subset['cache_size'],
                subset[metric],
                color=color,
                marker='o',
                linestyle='-',
                linewidth=2,
                markersize=8,
                label=f'R@{recall:.2f} (n={nprobe})'
            )
            
            # Data labels
            for x, y in zip(subset['cache_size'], subset[metric]):
                if not np.isnan(y):
                    ax.text(
                        x, y, f'{y:.1f}',
                        color=color,
                        ha='center',
                        va='bottom',
                        fontsize=8
                    )
        
        ax.set_title(name)
        ax.set_xlabel('Cache size')
        ax.set_ylabel(name)
        ax.set_xticks(sorted(df['cache_size'].unique()))
    
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
    os.makedirs("analysis_results", exist_ok=True)
    output_path = os.path.join("analysis_results", "cache_performance_analysis.png")
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"\nAnalysis saved to: {output_path}")
    plt.show()

# ==================== Extended analysis ====================
def additional_analysis(df):
    """Analyze cache performance in more detail."""
    print("\n=== Cache Performance Analysis ===")
    
    # 1. Mean performance for each cache size
    print("\n1. Mean performance by cache size:")
    cache_stats = df.groupby('cache_size').agg({
        'qps': ['mean', 'std', 'max'],
        'latency': ['mean', 'std', 'min'],
        'active_rate': ['mean', 'std']
    }).round(2)
    print(cache_stats)
    
    # 2. Cache efficiency: percentage improvement
    print("\n2. Performance improvement with increasing cache size:")
    cache_sizes = sorted(df['cache_size'].unique())
    for i in range(1, len(cache_sizes)):
        prev_size = cache_sizes[i-1]
        curr_size = cache_sizes[i]
        
        prev_data = df[df['cache_size'] == prev_size]
        curr_data = df[df['cache_size'] == curr_size]
        
        if not prev_data.empty and not curr_data.empty:
            qps_improvement = ((curr_data['qps'].mean() - prev_data['qps'].mean()) / prev_data['qps'].mean()) * 100
            latency_improvement = ((prev_data['latency'].mean() - curr_data['latency'].mean()) / prev_data['latency'].mean()) * 100
            
            print(f"From {prev_size} to {curr_size}:")
            print(f"  QPS improvement: {qps_improvement:.2f}%")
            print(f"  Latency reduction: {latency_improvement:.2f}%")
    
    # 3. Create the cache efficiency plot
    plt.figure(figsize=(12, 8))
    
    # Compute mean QPS for each cache size
    avg_qps_by_cache = df.groupby('cache_size')['qps'].mean()
    plt.plot(avg_qps_by_cache.index, avg_qps_by_cache.values, 
             marker='o', linewidth=3, markersize=10, color='#2ca02c')
    
    plt.title('Effect of Cache Size on Mean QPS')
    plt.xlabel('Cache size')
    plt.ylabel('Mean QPS')
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.xticks(sorted(df['cache_size'].unique()))
    
    # Annotate performance improvements
    for i in range(1, len(avg_qps_by_cache)):
        prev_val = avg_qps_by_cache.iloc[i-1]
        curr_val = avg_qps_by_cache.iloc[i]
        improvement = ((curr_val - prev_val) / prev_val) * 100
        x_pos = (avg_qps_by_cache.index[i-1] + avg_qps_by_cache.index[i]) / 2
        y_pos = (prev_val + curr_val) / 2
        plt.annotate(f'+{improvement:.1f}%', 
                    xy=(x_pos, y_pos),
                    xytext=(0, 10),
                    textcoords='offset points',
                    ha='center',
                    fontsize=10,
                    color='green')
    
    plt.tight_layout()
    efficiency_path = os.path.join("analysis_results", "cache_efficiency_analysis.png")
    plt.savefig(efficiency_path, dpi=300, bbox_inches='tight')
    print(f"\nCache efficiency plot saved to: {efficiency_path}")

# ==================== Main program ====================
if __name__ == "__main__":
    print("=== Cache Size Performance Analysis ===")
    
    # Load or extract data
    try:
        df = pd.read_csv("cache_performance.csv")
        print("\nAnalyzing the existing data file...")
    except FileNotFoundError:
        print("\nData file not found; extracting data...")
        df = extract_data()
    
    # Run the analysis
    if df is not None and not df.empty:
        print("\nData summary:")
        print(f"Valid records: {len(df)}")
        print(f"Cache sizes: {sorted(df['cache_size'].unique())}")
        print(f"Number of nprobe configurations: {len(df['nprobe'].unique())}")
        
        analyze_data(df)
        additional_analysis(df)
    else:
        print("\nError: no valid data available for analysis")