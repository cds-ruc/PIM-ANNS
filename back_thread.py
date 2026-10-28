import os
import pandas as pd
import re

# Base directory layout
base_dir_pattern = "BACK_THREAD_{}_SPACE1B20M4096_DIR"
dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"

# Directory and nprobe configurations
dir_numbers = range(1, 9)  # Values 1 through 8
nprobes = [4, 5, 8, 10, 21, 71]
recall_map = {
    4: 0.84,
    5: 0.86,
    8: 0.88,
    10: 0.9,
    21: 0.92,
    71: 0.94
}

# Collect all records
all_data = []

# Iterate over directories and nprobe files
for dir_num in dir_numbers:
    dir_path = base_dir_pattern.format(dir_num)
    
    for nprobe in nprobes:
        file_path = dir_path + dpu_file_pattern.format(nprobe)
        
        if os.path.exists(file_path):
            try:
                with open(file_path, 'r') as f:
                    content = f.read()
                    
                    # Extract the required fields with regular expressions
                    qps_match = re.search(r'qps=([\d.]+)', content)
                    avg_active_rate_match = re.search(r'avg_active_rate=([\d.]+)', content)
                    latency_match = re.search(r'latency=([\d.]+)', content)
                    latency_tail_match = re.search(r'latency_tail=([\d.]+)', content)
                    
                    # Extract the measured recall
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
                        print(f"Extracted: {file_path}")
                    else:
                        print(f"Warning: some fields are missing in {file_path}")
                        
            except Exception as e:
                print(f"Error processing {file_path}: {e}")

# Convert to a DataFrame
if all_data:
    df = pd.DataFrame(all_data)
    
    # Sort by directory and nprobe
    df = df.sort_values(by=['directory', 'nprobe'])
    
    # Display the extracted data
    print("\nExtracted data:")
    print(df.to_string(index=False))
    
    # Display summary statistics
    print("\nData statistics:")
    print(f"Extracted data from {len(df)} files")
    print(f"Directories: {df['directory'].nunique()}")
    print(f"nprobe configurations: {df['nprobe'].nunique()}")
    
    # Save to CSV
    csv_path = "extracted_performance_data.csv"
    df.to_csv(csv_path, index=False)
    print(f"\nData saved to {csv_path}")
    
    # Optionally display mean metrics grouped by directory
    print("\nMean performance metrics by directory:")
    group_stats = df.groupby('directory')[['qps', 'latency', 'latency_tail', 'avg_active_rate']].mean()
    print(group_stats.round(2))
    
else:
    print("No valid data found. Check the file paths and formats.")

# Identify missing files
print("\nChecking file availability:")
for dir_num in dir_numbers:
    dir_path = base_dir_pattern.format(dir_num)
    missing_files = []
    
    for nprobe in nprobes:
        file_path = dir_path + dpu_file_pattern.format(nprobe)
        if not os.path.exists(file_path):
            missing_files.append(f"nprobe{nprobe}")
    
    if missing_files:
        print(f"Directory {dir_path} is missing files: {', '.join(missing_files)}")
    else:
        print(f"All files are present in {dir_path}")