import os
import pandas as pd
import re

# 配置参数
slot_values = [50000, 100000, 150000, 200000, 250000]
base_dir_pattern = "SLOT_L_{}_SPACE1B20M4096_DIR"
dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"

nprobes = [4, 5, 8, 10, 21, 71]
recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}

# 存储所有数据
all_data = []

# 遍历所有目录和文件
for slot in slot_values:
    dir_path = base_dir_pattern.format(slot)
    
    for nprobe in nprobes:
        file_path = dir_path + dpu_file_pattern.format(nprobe)
        
        if os.path.exists(file_path):
            try:
                with open(file_path, 'r') as f:
                    content = f.read()
                    
                    # 提取关键指标
                    qps = float(re.search(r'qps=([\d.]+)', content).group(1))
                    active_rate = float(re.search(r'avg_active_rate=([\d.]+)', content).group(1))
                    latency = float(re.search(r'latency=([\d.]+)', content).group(1))
                    tail_latency = float(re.search(r'latency_tail=([\d.]+)', content).group(1))
                    actual_recall = float(re.search(r'R@10=([\d.]+)', content).group(1))
                    
                    all_data.append({
                        'slot_size': slot,
                        'directory': dir_path,
                        'nprobe': nprobe,
                        'expected_recall': recall_map[nprobe],
                        'actual_recall': actual_recall,
                        'qps': qps,
                        'avg_active_rate': active_rate,
                        'latency': latency,
                        'latency_tail': tail_latency
                    })
                    
            except Exception as e:
                print(f"处理文件 {file_path} 出错: {str(e)}")
        else:
            print(f"文件不存在: {file_path}")

# 转换为DataFrame
if all_data:
    df = pd.DataFrame(all_data)
    
    # 按slot大小和nprobe排序
    df = df.sort_values(by=['slot_size', 'nprobe'])
    
    # 保存结果
    output_file = "slot_performance_data.csv"
    df.to_csv(output_file, index=False)
    print(f"数据已保存到 {output_file}")
    
    # 显示前5行
    print("\n抽取的数据样例:")
    print(df.head())
else:
    print("未找到任何有效数据")