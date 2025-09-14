import pandas as pd
import matplotlib.pyplot as plt
import os
import re
import numpy as np

# ==================== 数据抽取函数 ====================
def extract_data():
    """从目录结构中提取性能数据"""
    dpu_counts = [1280, 1600, 1920, 2240, 2558]
    base_dir_pattern = "NR_DPU_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for dpu_count in dpu_counts:
        dir_path = base_dir_pattern.format(dpu_count)
        
        for nprobe in nprobes:
            file_path = dir_path + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # 提取所有指标
                        data = {
                            'dpu_count': dpu_count,
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
                        'dpu_count': dpu_count,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"文件不存在: {file_path}")
                # 为缺失文件添加占位数据
                all_data.append({
                    'dpu_count': dpu_count,
                    'nprobe': nprobe,
                    'recall': recall_map.get(nprobe, 0)
                })
    
    df = pd.DataFrame(all_data)
    
    # 数据验证
    print("\n数据列检查:")
    print(df.columns.tolist())
    print("\n数据统计:")
    print(f"总记录数: {len(df)}")
    print(f"DPU数量: {sorted(df['dpu_count'].unique())}")
    print(f"nprobe值: {sorted(df['nprobe'].unique())}")
    
    df.to_csv("dpu_performance.csv", index=False)
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
    print("=== 开始DPU数量性能分析 ===")
    print("DPU数量: [1280, 1600, 1920, 2240, 2558]")
    
  
    df = extract_data()
    
   