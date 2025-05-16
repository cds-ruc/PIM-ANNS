#!/usr/bin/env python3
import sys
import importlib.util
from typing import List, Dict, Callable
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.table import Table
import warnings
warnings.filterwarnings("ignore", message="This figure includes Axes")


def exp1():
    """Execute code for EXP1"""
    print("Running EXP1...")
    # Your EXP1 code here
    # Example: plt.plot(...)
    
    import matplotlib.pyplot as plt


    def load_exp1_data(filename):
        data = {
            'SIFT': {'DPU': {}, 'Batch DPU': {}, 'CPU': {}},
            'SPACE': {'DPU': {}, 'Batch DPU': {}, 'CPU': {}}
        }
        
      
        recall_map = {
            'SIFT': {6:0.84, 7:0.86, 9:0.88, 11:0.90, 15:0.92, 24:0.94},
            'SPACE': {4:0.84, 5:0.86, 8:0.88, 11:0.90, 21:0.92, 71:0.94}
        }

        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                    
                parts = [p.strip() for p in line.split(',')]
                dataset = parts[0]
                method = parts[1]
                nprobe = int(parts[2].split('=')[1])
                qps = float(parts[3].split('=')[1])
                
                recall = recall_map[dataset][nprobe]
                data[dataset][method][recall] = qps
        
        return data

    def calculate_y_axis_limits(data, dataset):
      
        max_qps = max([max(data[dataset][method].values()) for method in data[dataset]])
        y_max = np.ceil(max_qps * 1.1) 
        
    
        if y_max <= 2000:
            step = 500
        elif y_max <= 5000:
            step = 1000
        elif y_max <= 10000:
            step = 2000
        else:
            step = 5000
        
        y_ticks = np.arange(0, y_max + step, step)
        return y_max, y_ticks

    def plot_figure9(data):
       
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.sans-serif': ['DejaVu Sans', 'Liberation Sans'],
            'font.size': 10,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc',
            'figure.facecolor': '#f5f5dc'
        })

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
        
 
        recalls = [0.84, 0.86, 0.88, 0.90, 0.92, 0.94]
        bar_width = 0.25
        x = np.arange(len(recalls))
        colors = {
            'CPU': '#ff9999',      
            'Batch DPU': '#66b3ff', # PIMANN-Batch ()
            'DPU': '#99ff99'        # PIMANN ()
        }
        labels = {
            'CPU': 'Faiss-CPU',
            'Batch DPU': 'PIMANN-Batch',
            'DPU': 'PIMANN'
        }
        
        # ===== (a): SPACE-1B =====
        space_y_max, space_y_ticks = calculate_y_axis_limits(data, 'SPACE')
        for i, method in enumerate(['CPU', 'Batch DPU', 'DPU']):
            qps = [data['SPACE'][method][r] for r in recalls]
            ax1.bar(x + i*bar_width, qps, width=bar_width, 
                    color=colors[method], 
                    label=labels[method])
        
        ax1.set_title('(a) SPACE-1B', y=-0.3, fontsize=11)
        ax1.set_xticks(x + bar_width)
        ax1.set_xticklabels(recalls)
        ax1.set_ylim(0, space_y_max)
        ax1.set_yticks(space_y_ticks)
        ax1.set_ylabel('Throughput (QPS)', fontsize=10)
        ax1.set_xlabel('Recall@10', fontsize=10)
        
        # ===== (b): SIFT-1B =====
        sift_y_max, sift_y_ticks = calculate_y_axis_limits(data, 'SIFT')
        for i, method in enumerate(['CPU', 'Batch DPU', 'DPU']):
            qps = [data['SIFT'][method][r] for r in recalls]
            ax2.bar(x + i*bar_width, qps, width=bar_width, 
                    color=colors[method],
                    label=labels[method])
        
        ax2.set_title('(b) SIFT-1B', y=-0.3, fontsize=11)
        ax2.set_xticks(x + bar_width)
        ax2.set_xticklabels(recalls)
        ax2.set_ylim(0, sift_y_max)
        ax2.set_yticks(sift_y_ticks)
        ax2.set_xlabel('Recall@10', fontsize=10)
        
        # 
        handles, labels = ax1.get_legend_handles_labels()
        fig.legend(handles, labels, 
                loc='upper center', 
                ncol=3, 
                bbox_to_anchor=(0.5, 1.05),
                framealpha=1,
                fontsize=10)
        
        # 
        fig.suptitle('Figure 9: (Exp #1) Throughput under different recalls',
                    y=1.1, fontsize=12)

        plt.tight_layout()
        plt.savefig('figure9_throughput.png', dpi=300, bbox_inches='tight')
        plt.show()



 
    data = load_exp1_data('../exp1.txt')

    plot_figure9(data)

def exp2():
    """Execute code for EXP2"""
    print("Running EXP2...")
    # Your EXP2 code here
    
   


    def load_exp2_data(filename):
        data = {
            'average_latency': {'DPU': {}, 'Batch DPU': {}, 'CPU': {}},
            'tail_latency': {'DPU': {}, 'Batch DPU': {}, 'CPU': {}}
        }
        
        # nprobeRecall@10SPACE
        recall_map = {4:0.84, 5:0.86, 8:0.88, 11:0.90, 21:0.92, 71:0.94}

        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                    
                parts = [p.strip() for p in line.split(',')]
                method = parts[0]
                nprobe = int(parts[1].split('=')[1])
                avg_lat = float(parts[2].split('=')[1])
                tail_lat = float(parts[3].split('=')[1])
                
                recall = recall_map[nprobe]
                data['average_latency'][method][recall] = avg_lat
                data['tail_latency'][method][recall] = tail_lat
        
        return data

    def calculate_log_scale_limits(data_dict):
        """Y"""
        all_values = []
        for method in data_dict.values():
            all_values.extend(method.values())
        
        min_val = min(all_values)
        max_val = max(all_values)
        
        # 
        log_min = np.floor(np.log10(min_val))
        log_max = np.ceil(np.log10(max_val))
        
        y_min = 10**log_min
        y_max = 10**log_max
        
        # 
        y_ticks = [10**i for i in range(int(log_min), int(log_max)+1)]
        
        return y_min, y_max, y_ticks

    def plot_figure10(data):
        """Figure 10"""
        # 
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.size': 10,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc'
        })

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
        
        # 
        recalls = [0.84, 0.86, 0.88, 0.90, 0.92, 0.94]
        markers = {
            'CPU': ('o', '#1f77b4'),    # 
            'Batch DPU': ('^', '#ff7f0e'), # 
            'DPU': ('s', '#9467bd')       # 
        }
        
        # Y
        avg_min, avg_max, avg_ticks = calculate_log_scale_limits(data['average_latency'])
        tail_min, tail_max, tail_ticks = calculate_log_scale_limits(data['tail_latency'])
        
        # ===== (a):  =====
        for method, (marker, color) in markers.items():
            latencies = [data['average_latency'][method][r] for r in recalls]
            ax1.plot(recalls, latencies, 
                    marker=marker, color=color, 
                    label='Faiss-CPU' if method == 'CPU' else f'PIMANN{"-Batch" if "Batch" in method else ""}',
                    markersize=8, linewidth=2)
        
        ax1.set_title('(a) Average latency', y=-0.3)
        ax1.set_xlabel('Recall@10')
        ax1.set_ylabel('Latency (ms)')
        ax1.set_yscale('log')
        ax1.set_ylim(avg_min, avg_max)
        ax1.set_yticks(avg_ticks)
        ax1.set_yticklabels([f"{x:.1f}" if x < 1 else f"{int(x)}" for x in avg_ticks])
        
        # ===== (b): (P99) =====
        for method, (marker, color) in markers.items():
            latencies = [data['tail_latency'][method][r] for r in recalls]
            ax2.plot(recalls, latencies, 
                    marker=marker, color=color, 
                    label='Faiss-CPU' if method == 'CPU' else f'PIMANN{"-Batch" if "Batch" in method else ""}',
                    markersize=8, linewidth=2)
        
        ax2.set_title('(b) Tail latency (P99)', y=-0.3)
        ax2.set_xlabel('Recall@10')
        ax2.set_ylabel('Latency (ms)')
        ax2.set_yscale('log')
        ax2.set_ylim(tail_min, tail_max)
        ax2.set_yticks(tail_ticks)
        ax2.set_yticklabels([f"{x:.1f}" if x < 1 else f"{int(x)}" for x in tail_ticks])
        
        # 
        handles, labels = ax1.get_legend_handles_labels()
        fig.legend(handles, labels, 
                loc='upper center', 
                ncol=3, 
                bbox_to_anchor=(0.5, 1.05),
                framealpha=1)
        
        # 
        fig.suptitle('Figure 10: (Exp #2) Latency under different recalls',
                    y=1.1, fontsize=12)

        plt.tight_layout()
        plt.savefig('figure10_latency.png', dpi=300, bbox_inches='tight')
        plt.show()

    # 
    data = load_exp2_data('../exp2.txt')
    
    # 
    plot_figure10(data)

def exp3():
    """Execute code for EXP3"""
    print("Running EXP3...")
    # Your EXP3 code here
 

    def load_exp3_data(filename):
        dpu = []
        batch_dpu = []
        
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                    
                parts = [p.strip() for p in line.split(',')]
                dpu_val = int(parts[0].split('=')[1])
                batch_val = int(parts[1].split('=')[1])
                
                dpu.append(dpu_val)
                batch_dpu.append(batch_val)
        
        # 10ms
        time = np.arange(len(dpu)) * 0.01  # 
        
        return time, dpu, batch_dpu

    def plot_figure11(time, dpu, batch_dpu):
        """Figure 1110ms"""
        # 
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.sans-serif': ['DejaVu Sans', 'Liberation Sans'],
            'font.size': 9,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc',
            'figure.facecolor': '#f5f5dc'
        })

        fig, ax = plt.subplots(figsize=(8, 5))
        
        # 
        ax.plot(time, dpu, 'r-', label='PIMANN', linewidth=1.5)
        ax.plot(time, batch_dpu, 'b--', label='PIMANN-Batch', linewidth=1.5)
        
        # 
        # max_pus = max(max(dpu), max(batch_dpu))
        max_pus = 2560
        ax.axhline(y=max_pus, color='purple', linestyle=':', linewidth=1, label=f'MAX ({max_pus} PUs)')
        
        # 10ms
        ax.set_xlabel('Time (s)', fontsize=10)
        ax.set_ylabel('# of active PUs', fontsize=10)
        ax.set_ylim(0, max_pus * 1.1)
        ax.set_yticks([0, 1000, 2000, max_pus])
        
        # x
        total_time = time[-1]
        ax.set_xlim(0, total_time)
        ax.set_xticks(np.arange(0, total_time + 0.5, 0.5))  # 0.5
        
        # 
        ax.legend(loc='upper right', fontsize=9, framealpha=1)
        plt.title('Figure 11: (Exp #3) The number of active PUs over time\n'
                '(Data interval: 10ms)', 
                y=1.05, fontsize=11)
        
        plt.tight_layout()
        plt.savefig('figure11_active_pus_10ms.png', dpi=300, bbox_inches='tight')
        plt.show()


    # 
    time, dpu, batch_dpu = load_exp3_data('../exp3-fig11.txt')
    
    # 
    plot_figure11(time, dpu, batch_dpu)
    
    

    def read_data(filename):
        """nprobeRecall@10"""
        data = {
            "SIFT": {"DPU": [], "Batch DPU": [], "recalls": []},
            "SPACE": {"DPU": [], "Batch DPU": [], "recalls": []}
        }
        
        # nprobeRecall@10SIFTSPACE
        recall_map = {
            "SIFT": {6:0.84, 7:0.86, 9:0.88, 11:0.90, 15:0.92, 24:0.94},
            "SPACE": {4:0.84, 5:0.86, 8:0.88, 11:0.90, 21:0.92, 71:0.94}
        }

        with open(filename, 'r') as file:
            for line in file:
                line = line.strip()
                if not line:
                    continue
                
                parts = [p.strip() for p in line.split(',')]
                dataset = parts[0]
                method = parts[1]
                nprobe = int(parts[2].split('=')[1])
                rate = float(parts[3].split('=')[1])
                
                recall = recall_map[dataset][nprobe]
                if recall not in data[dataset]["recalls"]:
                    data[dataset]["recalls"].append(recall)
                
                if method == "DPU":
                    data[dataset]["DPU"].append(rate)
                elif method == "Batch DPU":
                    data[dataset]["Batch DPU"].append(rate)
        
        # recall
        for dataset in data:
            sorted_indices = np.argsort(data[dataset]["recalls"])
            data[dataset]["recalls"] = np.array(data[dataset]["recalls"])[sorted_indices].tolist()
            data[dataset]["DPU"] = np.array(data[dataset]["DPU"])[sorted_indices].tolist()
            data[dataset]["Batch DPU"] = np.array(data[dataset]["Batch DPU"])[sorted_indices].tolist()
        
        return data

    def plot_figure12(data):
        """Figure 12"""
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.size': 10,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc'
        })

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
        
        
        
        # ===== SPACE Dataset =====
        ax1.plot(data["SPACE"]["recalls"], data["SPACE"]["DPU"], 
                marker='o', color='#1f77b4', label='PIMANN', linewidth=2)
        ax1.plot(data["SPACE"]["recalls"], data["SPACE"]["Batch DPU"], 
                marker='^', color='#ff7f0e', label='PIMANN-Batch', linewidth=2)
        
        ax1.set_title('(b) SPACE-1B', y=-0.3)
        ax1.set_xlabel('Recall@10')
        ax1.set_xticks(data["SPACE"]["recalls"])
        ax1.grid(True, linestyle='--', alpha=0.4)
        ax1.legend()
        
        
        # ===== SIFT Dataset =====
        ax2.plot(data["SIFT"]["recalls"], data["SIFT"]["DPU"], 
                marker='o', color='#1f77b4', label='PIMANN', linewidth=2)
        ax2.plot(data["SIFT"]["recalls"], data["SIFT"]["Batch DPU"], 
                marker='^', color='#ff7f0e', label='PIMANN-Batch', linewidth=2)
        
        ax2.set_title('(a) SIFT-1B', y=-0.3)
        ax2.set_xlabel('Recall@10')
        ax2.set_ylabel('Average Active Rate')
        ax2.set_xticks(data["SIFT"]["recalls"])
        ax2.grid(True, linestyle='--', alpha=0.4)
        ax2.legend()
        
        # 
        fig.suptitle('Figure 12: (Exp #3) Active PU Rate under Different Recalls', 
                    y=1.05, fontsize=12)
        
        plt.tight_layout()
        plt.savefig('figure12_active_rate.png', dpi=300, bbox_inches='tight')
        plt.show()

    data = read_data("../exp3-fig12.txt")
    plot_figure12(data)

def exp4():
    """Execute code for EXP4"""
    print("Running EXP4...")
    # Your EXP4 code here
    

    def read_exp4_data(filename):
        data = {}
        recall_map = {4: 0.84, 5: 0.86, 8: 0.88, 11: 0.90}  # nprobeRecall@10
        
        with open(filename, 'r') as f:
            for line in f:
                if not line.strip():
                    continue
                parts = [p.strip() for p in line.split(',')]
                coroutines = int(parts[1].split('=')[1])
                nprobe = int(parts[2].split('=')[1])
                qps = float(parts[3].split('=')[1])
                latency = float(parts[4].split('=')[1])
                
                recall = recall_map[nprobe]
                if recall not in data:
                    data[recall] = {'coroutines': [], 'qps': [], 'latency': []}
                data[recall]['coroutines'].append(coroutines)
                data[recall]['qps'].append(qps)
                data[recall]['latency'].append(latency)
        return data

    def get_y_axis_limits(values):
        """Y"""
        max_val = max(max(vals) for vals in values)
        upper_limit = max_val * 1.2  # 20%
        step = round(upper_limit / 5, -2)  # 5
        return 0, upper_limit, np.arange(0, upper_limit + step, step)

    # seaborn
    plt.rcParams.update({
        'font.family': 'sans-serif',
        'font.sans-serif': ['DejaVu Sans', 'Arial'],
        'axes.grid': True,
        'grid.linestyle': '--',
        'grid.alpha': 0.6,
        'axes.facecolor': '#f5f5dc',  # 
        'figure.facecolor': '#f5f5dc'
    })

    # 、、、
    colors = ['#1f77b4', '#9467bd', '#d62728', '#2ca02c']

    # 
    fig = plt.figure(figsize=(14, 5))
    gs = fig.add_gridspec(1, 2, wspace=0.3)
    ax1 = fig.add_subplot(gs[0])
    ax2 = fig.add_subplot(gs[1])

    # 
    fig.suptitle('Figure 13: (Exp #4): Effectiveness of coroutine-based bus ownership switching',
                y=1.05, fontsize=14)

    # 
    data = read_exp4_data('../exp4.txt')

    # Y
    qps_values = [vals['qps'] for vals in data.values()]
    latency_values = [vals['latency'] for vals in data.values()]

    qps_min, qps_max, qps_ticks = get_y_axis_limits(qps_values)
    latency_min, latency_max, latency_ticks = get_y_axis_limits(latency_values)

    # (QPS)
    for i, (recall, vals) in enumerate(sorted(data.items())):
        ax1.plot(vals['coroutines'], vals['qps'], 
                marker='o', linestyle='-', color=colors[i],
                label=f'Recall@10={recall}')
    ax1.set_xlabel('# of coroutines', fontsize=12)
    ax1.set_ylabel('Throughput (QPS)', fontsize=12)
    ax1.set_xticks([1, 2, 4, 8, 16])
    ax1.set_xscale('log', base=2)
    ax1.set_ylim(qps_min, qps_max)
    ax1.set_yticks(qps_ticks)
    ax1.legend(title='Recall@10', fontsize=10, title_fontsize=11)
    ax1.set_title('(a) Throughput', y=-0.3)

    # (Latency)
    for i, (recall, vals) in enumerate(sorted(data.items())):
        ax2.plot(vals['coroutines'], vals['latency'], 
                marker='s', linestyle='--', color=colors[i],
                label=f'Recall@10={recall}')
    ax2.set_xlabel('# of coroutines', fontsize=12)
    ax2.set_ylabel('Latency (ms)', fontsize=12)
    ax2.set_xticks([1, 2, 4, 8, 16])
    ax2.set_xscale('log', base=2)
    ax2.set_ylim(latency_min, latency_max)
    ax2.set_yticks(latency_ticks)
    ax2.legend(title='Recall@10', fontsize=10, title_fontsize=11)
    ax2.set_title('(b) Latency', y=-0.3)

    plt.tight_layout()
    plt.savefig('figure13_coroutine_performance.png', dpi=300, bbox_inches='tight', facecolor='#f5f5dc')
    plt.show()

def exp5():
    """Execute code for EXP5"""
    print("Running EXP5...")
    # Your EXP5 code here
    

    # ====================== (a) Per-PU ======================
    def load_pu_data(wo_file, w_file):
        """PU"""
        with open(wo_file) as f:
            wo_data = [float(line.strip()) for line in f if line.strip()]
        with open(w_file) as f:
            w_data = [float(line.strip()) for line in f if line.strip()]
        return wo_data, w_data

    # ====================== (b)  ======================
    def load_throughput_data(filename):
        """"""
        copy_rates, qps = [], []
        with open(filename) as f:
            for line in f:
                if line.startswith('COPY_RATE'):  # 
                    continue
                if ',' in line:
                    parts = line.strip().split(',')
                    if len(parts) == 2:
                        copy_rates.append(float(parts[0]))
                        qps.append(float(parts[1]))
        return copy_rates, qps

    # 
    try:
        # (a)
        wo_load, w_load = load_pu_data('../exp5a-wo.txt', '../exp5a-w.txt')
        pu_ids = range(len(wo_load))
        
        # (b)
        copy_rates, throughput = load_throughput_data('../exp5b.txt')
        mem_gb = [36, 48, 60, 72, 84]  # 
        
    except FileNotFoundError as e:
        print(f": {e}")
        exit()
    except ValueError as e:
        print(f": {e}")
        exit()

    # ======================  ======================
    plt.style.use('default')
    plt.rcParams.update({
        'font.family': 'sans-serif',
        'font.size': 10,
        'axes.grid': True,
        'grid.linestyle': '--',
        'grid.alpha': 0.6,
        'axes.facecolor': '#f5f5dc'
    })

    fig = plt.figure(figsize=(12, 5), facecolor='#f5f5dc')
    gs = fig.add_gridspec(1, 2, width_ratios=[1.2, 1])

    # ====================== (a) ======================
    ax1 = fig.add_subplot(gs[0])
    ax1.plot(pu_ids, wo_load, 'b-', label='w/o selective replication', linewidth=2)
    ax1.plot(pu_ids, w_load, 'r-', label='w/ selective replication', linewidth=2)

    ax1.set_xlabel('PU IDs', fontsize=11)
    ax1.set_ylabel('Normalized load', fontsize=11)
    ax1.set_ylim(0, 1.25)
    ax1.set_yticks([0, 0.5, 1.0], ['0%', '50%', '100%'])
    ax1.legend(loc='upper right', framealpha=1)
    ax1.set_title('(a)', y=-0.3, fontsize=12)

    # ====================== (b) ======================
    ax2 = fig.add_subplot(gs[1])
    ax2.plot(mem_gb, throughput, 'ko-', markersize=6, linewidth=2)

    ax2.set_xlabel('Memory (GB)', fontsize=11)
    ax2.set_ylabel('Throughput (QPS)', fontsize=11)
    ax2.set_xticks(mem_gb)
    # ax2.set_yticks(range(1000, 6000, 1000))
    ax2.set_yticks(range(1000, 1500, 100))
    ax2.set_title('(b)', y=-0.3, fontsize=12)

    # ======================  ======================
    plt.suptitle('Figure 14: (Exp #5): Effect of per-PU dispatching\n'
                '(a) Per-PU load w/ or w/o selective replication, '
                '(b) Throughput with different capacity budgets', 
                y=1.05, fontsize=12)

    plt.tight_layout()
    plt.savefig('figure14_per_pu_dispatching.png', dpi=300, bbox_inches='tight', facecolor='#f5f5dc')
    plt.show()

def exp6():
    """Execute code for EXP6"""
    print("Running EXP6...")
    # Your EXP6 code here
    
    
    

    def set_font():
        """Arial"""
        try:
            # Arial，
            font_manager.findfont('Arial', fallback_to_default=False)
            plt.rcParams['font.family'] = 'Arial'
        except:
            plt.rcParams['font.family'] = 'sans-serif'
            plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Liberation Sans', 'Helvetica']

    def load_exp6_data(filename):
        throughput = {'BasicPIM': [], '+K': [], '+K+D': []}
        latency = {'Faiss-CPU': [], 'BasicPIM': [], '+K': [], '+K+D': []}
        current_section = None
        recall_map = {4:0.84, 5:0.86, 8:0.88, 11:0.90, 21:0.92, 71:0.94}

        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line: continue
                    
                if line.startswith('latency breakdown:'):
                    current_section = 'latency'
                    continue
                    
                if current_section == 'latency':
                    parts = [x.strip() for x in line.split(',')]
                    if 'CPU' in line:
                        latency['Faiss-CPU'] = [float(x) for x in parts[1:8]]
                    elif 'Batch DPU' in line:
                        latency['BasicPIM'] = [float(x) for x in parts[1:8]]
                    elif 'DPU, ENABLE_REPLICA=0' in line:
                        latency['+K'] = [float(x) for x in parts[2:9]]
                    elif 'DPU, ENABLE_REPLICA=1' in line:
                        latency['+K+D'] = [float(x) for x in parts[2:9]]
                else:
                    parts = [x.strip() for x in line.split(',')]
                    if 'Batch DPU' in line:
                        nprobe = int(parts[1].split('=')[1])
                        throughput['BasicPIM'].append((recall_map[nprobe], float(parts[2].split('=')[1])))
                    elif 'DPU, ENABLE_REPLICA=0' in line:
                        nprobe = int(parts[2].split('=')[1])
                        throughput['+K'].append((recall_map[nprobe], float(parts[3].split('=')[1])))
                    elif 'DPU, ENABLE_REPLICA=1' in line:
                        nprobe = int(parts[2].split('=')[1])
                        throughput['+K+D'].append((recall_map[nprobe], float(parts[3].split('=')[1])))
        
        # 
        for tech in throughput:
            throughput[tech] = sorted(throughput[tech], key=lambda x: x[0])
            throughput[tech] = {'recall': [x[0] for x in throughput[tech]], 
                            'qps': [x[1] for x in throughput[tech]]}
        
        return throughput, latency

    def plot_figure15(throughput, latency):
        """Figure 15"""
        # 
        set_font()
        
        # 
        plt.style.use('default')
        plt.rcParams.update({
            'font.size': 9,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc',
            'figure.facecolor': '#f5f5dc'
        })

        fig = plt.figure(figsize=(12, 5))
        gs = fig.add_gridspec(1, 2, width_ratios=[1, 1.2], wspace=0.35)

        # ===== (a):  =====
        ax1 = fig.add_subplot(gs[0])
        recall = throughput['BasicPIM']['recall']
        bar_width = 0.25
        x = np.arange(len(recall))
        colors = ['#2ca02c', '#ff7f0e', '#1f77b4']  # --
        
        for i, tech in enumerate(['BasicPIM', '+K', '+K+D']):
            ax1.bar(x + i*bar_width, throughput[tech]['qps'],
                    width=bar_width, color=colors[i], label=tech,
                    edgecolor='white', linewidth=0.5)
        
        ax1.set_xlabel('Recall@10', fontsize=10)
        ax1.set_ylabel('Throughput (QPS)', fontsize=10)
        ax1.set_xticks(x + bar_width)
        ax1.set_xticklabels(recall)
        ax1.set_ylim(0, 4000)
        ax1.set_yticks(range(0, 4001, 1000))
        ax1.legend(fontsize=8, framealpha=1)
        ax1.set_title('(a) Throughput', y=-0.2, fontsize=10)

        # ===== (b):  =====
        ax2 = fig.add_subplot(gs[1])
        latency_colors = [
            '#8c564b', '#e377c2', '#7f7f7f', 
            '#bcbd22', '#17becf', '#9467bd', '#d62728'
        ]
        latency_labels = [
            'Cluster filtering', 'LUT construction', 'Task construct',
            'Copy data', 'Merge', 'Distance computing', 'Identifying Top-K'
        ]
        
        # 
        total_latency = {tech: sum(x/1000 for x in vals) for tech, vals in latency.items()}
        y_max = np.ceil(max(total_latency.values()) * 1.1)
        
        # 
        bottom = np.zeros(4)
        for i, (color, label) in enumerate(zip(latency_colors, latency_labels)):
            heights = [latency[tech][i]/1000 for tech in ['Faiss-CPU', 'BasicPIM', '+K', '+K+D']]
            ax2.bar(['Faiss-CPU', 'BasicPIM', '+K', '+K+D'], 
                    heights, bottom=bottom, color=color, label=label, width=0.6)
            bottom += heights
        
        ax2.set_ylabel('Latency (s)', fontsize=10)
        ax2.set_ylim(0, y_max)
        ax2.set_yticks(np.arange(0, y_max + 1, max(1, int(y_max/6))))
        ax2.legend(fontsize=7, ncol=2, bbox_to_anchor=(1.05, 1))
        ax2.set_title('(b) Latency breakdown', y=-0.2, fontsize=10)

        # 
        fig.suptitle('Figure 15: (Exp #6): Contributions of techniques\n'
                    '(a) Throughput, (b) latency breakdown of a query\n'
                    'BasicPIM: PIMANN-Batch w/o selective replication\n'
                    'K: persistent PIM kernel; D: per-PU query dispatching',
                    y=1.05, fontsize=11)

        plt.tight_layout()
        plt.savefig('figure15.png', dpi=300, bbox_inches='tight')
        plt.show()

    # 
    throughput, latency = load_exp6_data('../exp6.txt')
    plot_figure15(throughput, latency)

def exp7():
    """Execute code for EXP7"""
    print("Running EXP7...")
    # Your EXP7 code here
    
    

    def load_exp7_data(filename):
        data = {
            'recalls': [],  # 
            'GPU': {'QPS': [], 'QPSW': []},
            'DPU': {'QPS': [], 'QPSW': []}
        }
        
        # nprobeRecall@10SPACE
        recall_map = {4:0.84, 5:0.86, 8:0.88, 11:0.90, 21:0.92, 71:0.94}
        temp_data = {}

        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or not line.startswith('nprobe:'):
                    continue
                    
                parts = [p.strip() for p in line.split(',')]
                nprobe = int(parts[0].split(':')[1])
                recall = recall_map[nprobe]
                
                # 
                if recall not in temp_data:
                    temp_data[recall] = {
                        'GPU': {'QPS': None, 'QPSW': None},
                        'DPU': {'QPS': None, 'QPSW': None}
                    }
                
                # 
                for part in parts[1:]:
                    if 'DPU QPS:' in part:
                        temp_data[recall]['DPU']['QPS'] = float(part.split(':')[1])
                    elif 'GPU QPS:' in part:
                        temp_data[recall]['GPU']['QPS'] = float(part.split(':')[1])
                    elif 'DPU QPS per Watt:' in part:
                        temp_data[recall]['DPU']['QPSW'] = float(part.split(':')[1])
                    elif 'GPU QPS per Watt:' in part:
                        temp_data[recall]['GPU']['QPSW'] = float(part.split(':')[1])
        
        # recall
        sorted_recalls = sorted(temp_data.keys())
        data['recalls'] = sorted_recalls
        
        for recall in sorted_recalls:
            for tech in ['GPU', 'DPU']:
                data[tech]['QPS'].append(temp_data[recall][tech]['QPS'])
                data[tech]['QPSW'].append(temp_data[recall][tech]['QPSW'])
        
        return data

    def plot_figure16(data):
        """Figure 16"""
        # 
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.size': 10,
            'axes.grid': True,
            'grid.linestyle': '--',
            'grid.alpha': 0.4,
            'axes.facecolor': '#f5f5dc',
            'figure.facecolor': '#f5f5dc'
        })

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
        
        # 
        recalls = data['recalls']
        bar_width = 0.35
        x = np.arange(len(recalls))
        colors = {
            'GPU': '#FFA500',  # Faiss-GPU
            'DPU': '#808080'   # PIMANN
        }
        
        # ===== (a):  =====
        # y
        max_qps = max(max(data['GPU']['QPS']), max(data['DPU']['QPS']))
        y_upper = (int(max_qps / 5000) + 1) * 5000  # 10k
        
        ax1.bar(x - bar_width/2, data['GPU']['QPS'], width=bar_width,
                color=colors['GPU'], label='Faiss-GPU', edgecolor='black')
        ax1.bar(x + bar_width/2, data['DPU']['QPS'], width=bar_width,
                color=colors['DPU'], label='PIMANN', 
                hatch='////', edgecolor='black')
        
        ax1.set_title('(a) Throughput', y=-0.3)
        ax1.set_xticks(x)
        ax1.set_xticklabels(recalls)
        ax1.set_ylim(0, y_upper)
        ax1.set_yticks(np.linspace(0, y_upper, 5))  # 5
        ax1.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, _: f'{int(x/1000)}k' if x >= 1000 else str(int(x))))
        ax1.set_ylabel('QPS')
        ax1.set_xlabel('Recall@10')
        ax1.legend()
        
        # ===== (b):  =====
        # y
        max_qpsw = max(max(data['GPU']['QPSW']), max(data['DPU']['QPSW']))
        y_upper_qpsw = (int(max_qpsw / 10) + 1) * 10  # 50
        
        ax2.bar(x - bar_width/2, data['GPU']['QPSW'], width=bar_width,
                color=colors['GPU'], label='Faiss-GPU', edgecolor='black')
        ax2.bar(x + bar_width/2, data['DPU']['QPSW'], width=bar_width,
                color=colors['DPU'], label='PIMANN',
                hatch='////', edgecolor='black')
        
        ax2.set_title('(b) Power Efficiency', y=-0.3)
        ax2.set_xticks(x)
        ax2.set_xticklabels(recalls)
        ax2.set_ylim(0, y_upper_qpsw)
        ax2.set_yticks(np.linspace(0, y_upper_qpsw, 6))  # 6
        ax2.set_ylabel('QPS per Watt')
        ax2.set_xlabel('Recall@10')
        ax2.legend()
        
        # 
        fig.suptitle('Figure 16: (Exp #7) Comparison with GPU-based system\n'
                    '(a) Throughput, (b) Power Efficiency',
                    y=1.05, fontsize=12)

        plt.tight_layout()
        plt.savefig('figure16_gpu_comparison.png', dpi=300, bbox_inches='tight')
        plt.show()

    data = load_exp7_data('../exp7.txt')
    
    
    # 
    plot_figure16(data)

def exp8():
    """Execute code for EXP8"""
    print("Running EXP8...")
    # Your EXP8 code here
    
    

    def load_exp8_data(filename):
        data = {
            'Faiss-CPU': {'Price': 1500, 'QPS': None, 'QPS/$': None},
            'Faiss-GPU': {'Price': 9685, 'QPS': None, 'QPS/$': None},
            'PIMANN': {'Price': 5473, 'QPS': None, 'QPS/$': None}
        }
        
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                    
                if 'dpu_qps:' in line:
                    parts = [p.strip() for p in line.split(',')]
                    data['PIMANN']['QPS'] = float(parts[0].split(':')[1])
                    data['PIMANN']['QPS/$'] = float(parts[1].split(':')[1])
                elif 'cpu_qps:' in line:
                    parts = [p.strip() for p in line.split(',')]
                    data['Faiss-CPU']['QPS'] = float(parts[0].split(':')[1])
                    data['Faiss-CPU']['QPS/$'] = float(parts[1].split(':')[1])
                elif 'gpu_qps:' in line:
                    parts = [p.strip() for p in line.split(',')]
                    data['Faiss-GPU']['QPS'] = float(parts[0].split(':')[1])
                    data['Faiss-GPU']['QPS/$'] = float(parts[1].split(':')[1])
        
        return data

    def plot_table1(data):
        """Table 1"""
        # 
        plt.style.use('default')
        plt.rcParams.update({
            'font.family': 'sans-serif',
            'font.size': 10,
            'axes.facecolor': '#f5f5dc',
            'figure.facecolor': '#f5f5dc'
        })

        fig = plt.figure(figsize=(8, 3))
        ax = fig.add_subplot(111, frame_on=False)
        ax.axis('off')
        
        # 
        solutions = ['Faiss-CPU', 'Faiss-GPU', 'PIMANN']
        columns = ['Solution', 'Price ($)', 'QPS', 'QPS/$']
        cell_text = [
            [solutions[0], f"{data[solutions[0]]['Price']:,}", 
            f"{data[solutions[0]]['QPS']:,.2f}", 
            f"{data[solutions[0]]['QPS/$']:,.3f}"],
            [solutions[1], f"{data[solutions[1]]['Price']:,}", 
            f"{data[solutions[1]]['QPS']:,.2f}", 
            f"{data[solutions[1]]['QPS/$']:,.3f}"],
            [solutions[2], f"{data[solutions[2]]['Price']:,}", 
            f"{data[solutions[2]]['QPS']:,.2f}", 
            f"{data[solutions[2]]['QPS/$']:,.3f}"]
        ]
        
        # 
        table = ax.table(cellText=cell_text,
                        colLabels=columns,
                        loc='center',
                        cellLoc='center',
                        colColours=['#f5f5dc']*4)
        
        # 
        table.auto_set_font_size(False)
        table.set_fontsize(10)
        table.scale(1, 1.5)
        
        # 
        plt.suptitle('Table 1: (Exp #8): Cost efficiency comparison of different solutions',
                    y=0.9, fontsize=12)
        
        plt.tight_layout()
        plt.savefig('table1_cost_efficiency.png', dpi=300, bbox_inches='tight')
        plt.show()

    # 
    data = load_exp8_data('../exp8.txt')
    
    # 
    plot_table1(data)

def show_help():
    """Display help information"""
    print("""
Usage:
    python3 plot.py [exp1] [exp2] ... [exp8]
    python3 plot.py help

Available arguments:
    exp1    
    exp2    
    exp3
    exp4
    exp5
    exp6
    exp7
    exp8    
    help    

Examples:
    python3 plot.py exp1 exp3    # Run experiments 1 and 3
    python3 plot.py exp5         # Run only experiment 5
    python3 plot.py help          # Show help message
""")

def main(args: List[str]):
    """Main function to handle command-line arguments"""
    
    # Mapping of argument names to functions
    exp_functions: Dict[str, Callable] = {
        'exp1': exp1,
        'exp2': exp2,
        'exp3': exp3,
        'exp4': exp4,
        'exp5': exp5,
        'exp6': exp6,
        'exp7': exp7,
        'exp8': exp8,
        'help': show_help
    }
    
    if not args or 'help' in args:
        show_help()
        if not args:  # No arguments - run all
            print("\nRunning all experiments...")
            for exp in ['exp1', 'exp2', 'exp3', 'exp4', 'exp5', 'exp6', 'exp7', 'exp8']:
                exp_functions[exp]()
        return
    
    valid_args = []
    invalid_args = []
    
    # Validate arguments
    for arg in args:
        if arg in exp_functions:
            valid_args.append(arg)
        else:
            invalid_args.append(arg)
    
    if invalid_args:
        print(f"Warning: Ignoring invalid arguments: {', '.join(invalid_args)}")
        print("Use 'python3 plot.py help' to see available arguments.")
    
    if not valid_args:
        print("No valid arguments provided.")
        show_help()
        return
    
    # Execute the requested functions
    for arg in valid_args:
        if arg != 'help':  # help is already handled above
            exp_functions[arg]()

if __name__ == "__main__":
    main(sys.argv[1:])