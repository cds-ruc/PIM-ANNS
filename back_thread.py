import os
import pandas as pd
import re

# 基础目录结构
base_dir_pattern = "BACK_THREAD_{}_SPACE1B20M4096_DIR"
dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"

# 可能的目录和nprobe值
dir_numbers = range(1, 9)  # 1到8
nprobes = [4, 5, 8, 10, 21, 71]
recall_map = {
    4: 0.84,
    5: 0.86,
    8: 0.88,
    10: 0.9,
    21: 0.92,
    71: 0.94
}

# 存储所有数据的列表
all_data = []

# 遍历所有目录和nprobe文件
for dir_num in dir_numbers:
    dir_path = base_dir_pattern.format(dir_num)
    
    for nprobe in nprobes:
        file_path = dir_path + dpu_file_pattern.format(nprobe)
        
        if os.path.exists(file_path):
            try:
                with open(file_path, 'r') as f:
                    content = f.read()
                    
                    # 使用正则表达式提取需要的字段
                    qps_match = re.search(r'qps=([\d.]+)', content)
                    avg_active_rate_match = re.search(r'avg_active_rate=([\d.]+)', content)
                    latency_match = re.search(r'latency=([\d.]+)', content)
                    latency_tail_match = re.search(r'latency_tail=([\d.]+)', content)
                    
                    # 提取实际召回率
                    recall_match = re.search(r'R@10=([\d.]+)', content)
                    actual_recall = float(recall_match.group(1)) if recall_match else recall_map.get(nprobe, 0)
                    
                    if all([qps_match, avg_active_rate_match, latency_match, latency_tail_match]):
                        data_dict = {
                            'directory': dir_num,
                            'dir_name': dir_path,
                            'nprobe': nprobe,
                            'expected_recall': recall_map.get(nprobe, 0),
                            'actual_recall': actual_recall,
                            'qps': float(qps_match.group(1)),
                            'avg_active_rate': float(avg_active_rate_match.group(1)),
                            'latency': float(latency_match.group(1)),
                            'latency_tail': float(latency_tail_match.group(1))
                        }
                        all_data.append(data_dict)
                        print(f"成功提取: {file_path}")
                    else:
                        print(f"警告: {file_path} 中缺少某些字段")
                        
            except Exception as e:
                print(f"处理文件 {file_path} 时出错: {e}")

# 转换为DataFrame
if all_data:
    df = pd.DataFrame(all_data)
    
    # 按目录和nprobe排序
    df = df.sort_values(by=['directory', 'nprobe'])
    
    # 显示提取的数据
    print("\n提取的数据表格：")
    print(df.to_string(index=False))
    
    # 显示统计信息
    print("\n数据统计：")
    print(f"总共提取了 {len(df)} 个文件的数据")
    print(f"涉及 {df['directory'].nunique()} 个目录")
    print(f"涉及 {df['nprobe'].nunique()} 种nprobe配置")
    
    # 保存到CSV文件
    csv_path = "extracted_performance_data.csv"
    df.to_csv(csv_path, index=False)
    print(f"\n数据已保存到 {csv_path}")
    
    # 可选：按目录分组显示平均值
    print("\n按目录分组的平均性能指标：")
    group_stats = df.groupby('directory')[['qps', 'latency', 'latency_tail', 'avg_active_rate']].mean()
    print(group_stats.round(2))
    
else:
    print("没有找到任何有效数据，请检查文件路径和格式是否正确。")

# 检查哪些文件缺失
print("\n检查文件存在情况：")
for dir_num in dir_numbers:
    dir_path = base_dir_pattern.format(dir_num)
    missing_files = []
    
    for nprobe in nprobes:
        file_path = dir_path + dpu_file_pattern.format(nprobe)
        if not os.path.exists(file_path):
            missing_files.append(f"nprobe{nprobe}")
    
    if missing_files:
        print(f"目录 {dir_path} 缺少文件: {', '.join(missing_files)}")
    else:
        print(f"目录 {dir_path} 所有文件完整")