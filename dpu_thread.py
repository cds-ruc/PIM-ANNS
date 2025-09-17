import pandas as pd
import matplotlib.pyplot as plt
import os
import re
import numpy as np

# ==================== 数据抽取函数 ====================
def extract_data():
    """从目录结构中提取性能数据"""
    thread_counts = list(range(2, 17,2))  # 1到16
    base_dir_pattern = "DPU_THREAD_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for thread_count in thread_counts:
        dir_path = base_dir_pattern.format(thread_count)
        
        for nprobe in nprobes:
            file_path = dir_path + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # 提取所有指标
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
                    print(f"处理文件 {file_path} 出错: {str(e)}")
                    # 添加基础数据占位
                    all_data.append({
                        'dpu_threads': thread_count,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"文件不存在: {file_path}")
                # 为缺失文件添加占位数据
                all_data.append({
                    'dpu_threads': thread_count,
                    'nprobe': nprobe,
                    'recall': recall_map.get(nprobe, 0)
                })
    
    df = pd.DataFrame(all_data)
    
    # 数据验证
    print("\n数据列检查:")
    print(df.columns.tolist())
    print("\n数据统计:")
    print(f"总记录数: {len(df)}")
    print(f"DPU线程数配置: {sorted(df['dpu_threads'].unique())}")
    print(f"nprobe值: {sorted(df['nprobe'].unique())}")
    
    df.to_csv("dpu_thread_performance.csv", index=False)
    return df

def extract_metric(content, metric_name):
    """安全提取指标值"""
    try:
        match = re.search(rf'{metric_name}=([\d.]+)', content)
        return float(match.group(1)) if match else np.nan
    except:
        return np.nan

# ==================== 数据分析函数 ====================
def analyze_data(df):
    """DPU线程数对性能影响分析"""
    # 检查数据完整性
    required_cols = ['dpu_threads', 'nprobe', 'recall', 'qps', 'latency', 'latency_tail', 'active_rate']
    missing_cols = [col for col in required_cols if col not in df.columns]
    
    if missing_cols:
        print(f"\n错误: 缺少必要列 {missing_cols}")
        print("可用列:", df.columns.tolist())
        return
    
    # 设置绘图样式
    plt.style.use('ggplot')
    plt.rcParams.update({
        'font.sans-serif': 'SimHei',
        'axes.unicode_minus': False,
        'figure.titlesize': 16,
        'axes.grid': True,
        'grid.linestyle': '--',
        'grid.alpha': 0.6
    })
    
    # 创建分析图表
    fig, axes = plt.subplots(2, 2, figsize=(18, 12))
    fig.suptitle('不同DPU线程数和召回率下的性能指标分析', y=1.02)
    
    # 指标配置
    metrics = [
        ('qps', '查询吞吐量(QPS)', 'linear'),
        ('latency', '延迟(ms)', 'linear'),
        ('latency_tail', '尾部延迟(ms)', 'linear'), 
        ('active_rate', '平均激活率', 'linear')
    ]
    
    # 颜色方案
    colors = [
        '#1f77b4', '#ff7f0e', '#2ca02c', 
        '#d62728', '#9467bd', '#8c564b'
    ]
    
    # 绘制每个指标
    for idx, (metric, name, scale) in enumerate(metrics):
        ax = axes[idx//2, idx%2]
        
        for i, nprobe in enumerate(sorted(df['nprobe'].unique())):
            subset = df[df['nprobe'] == nprobe].sort_values('dpu_threads')
            if subset.empty:
                continue
                
            recall = subset['recall'].iloc[0]
            color = colors[i % len(colors)]
            
            # 主曲线
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
            
            # 数据标签
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
        ax.set_xlabel('DPU线程数')
        ax.set_ylabel(name)
        ax.set_xticks(sorted(df['dpu_threads'].unique()))
    
    # 图例和布局
    handles, labels = axes[0,0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc='upper right',
        bbox_to_anchor=(1.15, 0.9),
        title='召回率 & nprobe值'
    )
    
    plt.tight_layout()
    
    # 保存结果
    os.makedirs("analysis_results", exist_ok=True)
    output_path = os.path.join("analysis_results", "dpu_thread_performance_analysis.png")
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"\n分析结果已保存至: {output_path}")
    plt.show()

# ==================== 扩展分析函数 ====================
def additional_analysis(df):
    """DPU线程数的深度分析"""
    print("\n=== DPU线程数深度分析 ===")
    
    # 1. 各线程数的平均性能统计
    print("\n1. 各DPU线程数的平均性能:")
    thread_stats = df.groupby('dpu_threads').agg({
        'qps': ['mean', 'std', 'max'],
        'latency': ['mean', 'std', 'min'],
        'latency_tail': ['mean', 'std', 'min'],
        'active_rate': ['mean', 'std']
    }).round(2)
    print(thread_stats)
    
    # 2. 线程扩展效率分析
    print("\n2. DPU线程扩展效率分析:")
    thread_counts = sorted(df['dpu_threads'].unique())
    for i in range(1, len(thread_counts)):
        prev_threads = thread_counts[i-1]
        curr_threads = thread_counts[i]
        
        prev_data = df[df['dpu_threads'] == prev_threads]
        curr_data = df[df['dpu_threads'] == curr_threads]
        
        if not prev_data.empty and not curr_data.empty:
            qps_improvement = ((curr_data['qps'].mean() - prev_data['qps'].mean()) / prev_data['qps'].mean()) * 100
            scaling_efficiency = (qps_improvement / (curr_threads - prev_threads)) * 100
            
            print(f"从 {prev_threads} 线程到 {curr_threads} 线程:")
            print(f"  QPS提升: {qps_improvement:.2f}%")
            print(f"  扩展效率: {scaling_efficiency:.2f}% (每线程提升)")
    
    # 3. 创建扩展效率分析图
    plt.figure(figsize=(12, 8))
    
    # 计算每个线程数的平均QPS
    avg_qps_by_thread = df.groupby('dpu_threads')['qps'].mean()
    plt.plot(avg_qps_by_thread.index, avg_qps_by_thread.values, 
             marker='o', linewidth=3, markersize=10, color='#2ca02c',
             label='实际性能')
    
    # 理想线性扩展参考线
    base_qps = avg_qps_by_thread.iloc[0]
    ideal_qps = [base_qps * t for t in avg_qps_by_thread.index]
    plt.plot(avg_qps_by_thread.index, ideal_qps, 
             linestyle='--', color='#ff7f0e', linewidth=2,
             label='理想线性扩展')
    
    plt.title('DPU线程数扩展效率分析')
    plt.xlabel('DPU线程数')
    plt.ylabel('平均QPS')
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.xticks(sorted(df['dpu_threads'].unique()))
    plt.legend()
    
    # 添加扩展效率标注
    for i in range(1, len(avg_qps_by_thread)):
        prev_val = avg_qps_by_thread.iloc[i-1]
        curr_val = avg_qps_by_thread.iloc[i]
        actual_improvement = curr_val - prev_val
        ideal_improvement = base_qps  # 每增加1线程的理想提升
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
    efficiency_path = os.path.join("analysis_results", "dpu_thread_scaling_efficiency.png")
    plt.savefig(efficiency_path, dpi=300, bbox_inches='tight')
    print(f"\n扩展效率分析图已保存至: {efficiency_path}")
    
    # 4. 最佳线程数推荐
    print("\n3. 最佳DPU线程数推荐:")
    for recall in sorted(df['recall'].unique()):
        subset = df[df['recall'] == recall]
        if not subset.empty:
            best_qps = subset.loc[subset['qps'].idxmax()]
            best_cost_eff = subset.loc[(subset['qps'] / subset['dpu_threads']).idxmax()]
            
            print(f"召回率 {recall:.2f}:")
            print(f"  最高性能: {best_qps['qps']:.1f} QPS (线程数={best_qps['dpu_threads']})")
            print(f"  最佳性价比: {best_cost_eff['qps']:.1f} QPS (线程数={best_cost_eff['dpu_threads']}, 效率={best_cost_eff['qps']/best_cost_eff['dpu_threads']:.1f} QPS/线程)")

# ==================== 主程序 ====================
if __name__ == "__main__":
    print("=== 开始DPU线程数性能分析 ===")
    print("DPU线程数配置: 1到16")
    
    # 数据加载或提取
    # try:
    #     df = pd.read_csv("dpu_thread_performance.csv")
    #     print("\n使用现有数据文件进行分析...")
    # except FileNotFoundError:
    #     print("\n未找到数据文件，开始抽取数据...")
    df = extract_data()
    
    # 执行分析
    if df is not None and not df.empty:
        print("\n数据摘要:")
        print(f"有效数据记录: {len(df)}")
        print(f"DPU线程数配置: {sorted(df['dpu_threads'].unique())}")
        print(f"nprobe配置数量: {len(df['nprobe'].unique())}")
        
        analyze_data(df)
        additional_analysis(df)
    else:
        print("\n错误: 没有有效数据可供分析")