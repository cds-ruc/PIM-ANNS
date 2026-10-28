# import numpy as np
# import os

# def generate_expanded_datasets():
#     original_data_path = "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data.bin"
#     original_id_path = "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/id.bin"
#     original_lut_path = "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut.bin"
    
#     SLOT_L = 100000
#     ORIGINAL_M = 20
#     MY_PQ_CLUSTER = 256
#     target_Ms = [20, 40, 60, 80, 100]
    
#     # Read the original data
#     original_data = np.fromfile(original_data_path, dtype=np.uint8).reshape(SLOT_L, ORIGINAL_M)
#     original_ids = np.fromfile(original_id_path, dtype=np.int64)
#     original_lut = np.fromfile(original_lut_path, dtype=np.int32).reshape(ORIGINAL_M, MY_PQ_CLUSTER)
    
#     print(f"Original dataset loaded:")
#     print(f"  - Data shape: {original_data.shape}")
#     print(f"  - IDs count: {len(original_ids)}")
#     print(f"  - LUT shape: {original_lut.shape}")
    
#     for target_M in target_Ms:
#         if target_M % ORIGINAL_M != 0:
#             print(f"Skipping M={target_M} (not multiple of {ORIGINAL_M})")
#             continue
        
#         replication_factor = target_M // ORIGINAL_M
#         print(f"\nGenerating dataset with M={target_M} (replication factor: {replication_factor})...")
        
#         # 1. Generate data by repeating columns
#         new_data = np.tile(original_data, (1, replication_factor))
#         new_data_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M{target_M}.bin"
#         new_data.tofile(new_data_path)
        
#         # 2. Keep the ID file unchanged
#         new_id_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/id_M{target_M}.bin"
#         original_ids.tofile(new_id_path)
        
#         # 3. Generate the LUT by repeating rows
#         new_lut = np.tile(original_lut, (replication_factor, 1))
#         new_lut_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut_M{target_M}.bin"
#         new_lut.tofile(new_lut_path)
        
#         print(f"  Generated: {new_data_path} ({new_data.size / (1024 * 1024):.1f} MB)")
#         print(f"  Generated: {new_id_path}")
#         print(f"  Generated: {new_lut_path}")

# def verify_dataset(target_M):
#     SLOT_L = 100000
#     MY_PQ_CLUSTER = 256
    
#     data_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M{target_M}.bin"
#     lut_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut_M{target_M}.bin"
    
#     # Check file sizes
#     data_size = os.path.getsize(data_path)
#     expected_data_size = SLOT_L * target_M
#     lut_size = os.path.getsize(lut_path)
#     expected_lut_size = target_M * MY_PQ_CLUSTER * 4  # int32 is 4 bytes
    
#     print(f"Verifying M={target_M}:")
#     print(f"  Data: {data_size} bytes (expected: {expected_data_size}) - {'✓' if data_size == expected_data_size else '✗'}")
#     print(f"  LUT: {lut_size} bytes (expected: {expected_lut_size}) - {'✓' if lut_size == expected_lut_size else '✗'}")

# if __name__ == "__main__":
#     print("=== PQ Dataset Expansion Tool ===")
    
#     # Generate datasets
#     generate_expanded_datasets()
    
#     # Verify generated files
#     print("\n=== Verification ===")
#     for target_M in [40, 60, 80, 100]:
#         verify_dataset(target_M)


import numpy as np
import os

def inspect_generated_files():
    target_Ms = [40, 60, 80, 100]
    
    for target_M in target_Ms:
        print(f"\n{'='*60}")
        print(f"INSPECTING M={target_M} DATASET")
        print(f"{'='*60}")
        
        # Inspect the data file
        data_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M{target_M}.bin"
        if os.path.exists(data_path):
            data = np.fromfile(data_path, dtype=np.uint8)
            print(f"\nData file (first 2 vectors, M={target_M}):")
            print(f"Total elements: {len(data)}")
            print(f"Shape: {len(data)//target_M} vectors × {target_M} dimensions")
            
            for i in range(2):  # First two vectors
                start_idx = i * target_M
                end_idx = start_idx + target_M
                vector_data = data[start_idx:end_idx]
                print(f"Vector {i}: {vector_data}")
                print(f"  First 10 values: {vector_data[:10]}")
                print(f"  Last 10 values: {vector_data[-10:]}")
        else:
            print(f"Data file not found: {data_path}")
        
        # Inspect the ID file
        id_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/id_M{target_M}.bin"
        if os.path.exists(id_path):
            ids = np.fromfile(id_path, dtype=np.int64)
            print(f"\nID file (first 2 IDs):")
            print(f"Total IDs: {len(ids)}")
            print(f"First 2 IDs: {ids[:2]}")
        else:
            print(f"ID file not found: {id_path}")
        
        # Inspect the LUT file
        lut_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut_M{target_M}.bin"
        if os.path.exists(lut_path):
            lut = np.fromfile(lut_path, dtype=np.int32)
            print(f"\nLUT file (first 2 groups, M={target_M}):")
            print(f"Total elements: {len(lut)}")
            print(f"Shape: {target_M} groups × 256 clusters")
            
            # Display the first two groups
            for i in range(2):
                start_idx = i * 256
                end_idx = start_idx + 256
                group_data = lut[start_idx:end_idx]
                print(f"Group {i}:")
                print(f"  First 10 values: {group_data[:10]}")
                print(f"  Last 10 values: {group_data[-10:]}")
                print(f"  Min: {np.min(group_data)}, Max: {np.max(group_data)}, Mean: {np.mean(group_data):.2f}")
        else:
            print(f"LUT file not found: {lut_path}")

def compare_replication_pattern():
    """Verify the replication pattern."""
    print(f"\n{'='*60}")
    print("COMPARING REPLICATION PATTERNS")
    print(f"{'='*60}")
    
    # Read the original M=20 data
    original_data_path = "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data.bin"
    original_data = np.fromfile(original_data_path, dtype=np.uint8).reshape(100000, 20)
    
    print("Original M=20 data (first vector):")
    print(original_data[0])
    
    # Inspect the M=40 data
    data_40_path = "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M40.bin"
    if os.path.exists(data_40_path):
        data_40 = np.fromfile(data_40_path, dtype=np.uint8).reshape(100000, 40)
        print("\nM=40 data (first vector):")
        print(data_40[0])
        print("Replication check - first 20 vs second 20:")
        print("First 20:  ", data_40[0][:20])
        print("Second 20: ", data_40[0][20:40])
        print("Match: ", np.array_equal(data_40[0][:20], data_40[0][20:40]))
        print("Match with original: ", np.array_equal(original_data[0], data_40[0][:20]))

if __name__ == "__main__":
    print("=== INSPECTING GENERATED DATASETS ===")
    
    # Inspect all generated files
    inspect_generated_files()
    
    # Compare replication patterns
    compare_replication_pattern()
    
    # Check file sizes
    print(f"\n{'='*60}")
    print("FILE SIZES")
    print(f"{'='*60}")
    
    target_Ms = [20, 40, 60, 80, 100]
    for target_M in target_Ms:
        data_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data_M{target_M}.bin" if target_M != 20 else "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/data.bin"
        lut_path = f"/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut_M{target_M}.bin" if target_M != 20 else "/home/wupuqing/workspace/PIM-ANNS/dpu/dpu_demo/input/lut.bin"
        
        if os.path.exists(data_path):
            size_mb = os.path.getsize(data_path) / (1024 * 1024)
            print(f"data_M{target_M}.bin: {size_mb:.2f} MB")
        
        if os.path.exists(lut_path):
            size_kb = os.path.getsize(lut_path) / 1024
            print(f"lut_M{target_M}.bin: {size_kb:.2f} KB")