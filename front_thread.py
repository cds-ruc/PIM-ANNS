import pandas as pd
import matplotlib.pyplot as plt
import os
import re
import numpy as np

# ==================== 数据抽取函数 ====================
def extract_data():
    """从目录结构中提取性能数据"""
    thread_counts = [5, 10, 15, 20, 25, 30, 35, 40]
    base_dir_pattern = "FRONT_THREAD_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for thread in thread_counts:
        dir_path = base_dir_pattern.format(thread)
        
        for nprobe in nprobes:
            file_path = dir_path + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # 提取所有指标（带错误处理）
                        data = {
                            'thread_count': thread,
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
                    all_data.append({
                        'thread_count': thread,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"文件不存在: {file_path}")
    
    df = pd.DataFrame(all_data)
    
    # 数据验证
    print("\n数据列检查:")
    print(df.columns.tolist())
    print("\n前5行数据样例:")
    print(df.head())
    
    df.to_csv("thread_performance.csv", index=False)
    return df

def extract_metric(content, metric_name):
    """安全提取指标值"""
    try:
        match = re.search(rf'{metric_name}=([\d.]+)', content)
        return float(match.group(1)) if match else np.nan
    except:
        return np.nan


# ==================== 主程序 ====================
if __name__ == "__main__":
    print("=== 开始线程数性能分析 ===")
    
    df = extract_data()
    
