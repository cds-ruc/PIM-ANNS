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
    dpu_counts = [1280, 1600, 1920, 2240, 2558]
    base_dir_pattern = "NR_DPU_{}_SPACE1B20M4096_DIR"
    dpu_file_pattern = "/DPU_DIR/dpu-time-nprobe{}.txt"
    
    nprobes = [4, 5, 8, 10, 21, 71]
    recall_map = {4:0.84, 5:0.86, 8:0.88, 10:0.9, 21:0.92, 71:0.94}
    
    all_data = []
    
    for dpu_count in dpu_counts:
        dir_path = base_dir_pattern.format(dpu_count)
        
        for nprobe in nprobes:
            file_path = str(PROJECT_ROOT / dir_path) + dpu_file_pattern.format(nprobe)
            
            if os.path.exists(file_path):
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        
                        # Extract all metrics
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
                    print(f"Error processing {file_path}: {str(e)}")
                    # Add a placeholder record
                    all_data.append({
                        'dpu_count': dpu_count,
                        'nprobe': nprobe,
                        'recall': recall_map.get(nprobe, 0)
                    })
            else:
                print(f"File not found: {file_path}")
                # Add placeholder records for missing files
                all_data.append({
                    'dpu_count': dpu_count,
                    'nprobe': nprobe,
                    'recall': recall_map.get(nprobe, 0)
                })
    
    df = pd.DataFrame(all_data)
    
    # Validate the data
    print("\nData columns:")
    print(df.columns.tolist())
    print("\nData statistics:")
    print(f"Total records: {len(df)}")
    print(f"DPU counts: {sorted(df['dpu_count'].unique())}")
    print(f"nprobe values: {sorted(df['nprobe'].unique())}")
    
    df.to_csv(str(ANALYSIS_DIR / "data" / "dpu_performance.csv"), index=False)
    return df

def extract_metric(content, metric_name):
    """Extract a metric value safely."""
    try:
        match = re.search(rf'{metric_name}=([\d.]+)', content)
        return float(match.group(1)) if match else np.nan
    except:
        return np.nan



# ==================== Main program ====================
if __name__ == "__main__":
    print("=== DPU Count Performance Analysis ===")
    print("DPU counts: [1280, 1600, 1920, 2240, 2558]")
    
  
    df = extract_data()
    
   