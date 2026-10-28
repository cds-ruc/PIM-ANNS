# TOS single-PU microbenchmarks

These experiments compare PQ distance kernels and measure sensitivity to PQ subspace count, Top-K, transfer size, LUT segment size, pruning checks and precomputation size. The `checksum/` directory keeps its SDK example name, but its host program computes CPU reference PQ distances and runs a DPU search kernel; it is not a checksum demo.

## Requirements

Use a configured UPMEM machine with the DPU SDK on `PATH`, a C compiler and GNU Make. Plotting requires Python 3, NumPy, pandas and Matplotlib. The default build uses one DPU and 16 tasklets. There is one C host implementation; no Python host or `test_python` target is provided.

## Inputs

Place headerless binary files in `experiments/single-task/input/`. For each PQ subspace count `M` required by a sweep, supply:

| File | Type and shape |
|---|---|
| `data_M<M>.bin` | `uint8`, `100000 × M` PQ codes in row-major order |
| `id_M<M>.bin` | `int64`, `100000` vector IDs |
| `lut_M<M>.bin` | `int32`, `M × 256` lookup-table entries in row-major order |

Use the native little-endian representation expected by the host. Input data is not bundled. The M sweeps use `20, 40, 60, 80, 100`; the Top-K sweep uses `M=20`. Input files must agree with `SLOT_L`, `MY_PQ_M` and `MY_PQ_CLUSTER` in `checksum/common/common.h`.

`inspect_data.py` inspects existing expanded inputs and compares the first M=40 vector with the original M=20 vector. It additionally expects `input/data.bin` with shape `100000 × 20` and uses `input/lut.bin` for the original LUT size report. It does not generate inputs or establish that every vector is correct.

```bash
python3 experiments/single-task/inspect_data.py
```

## Build and run one configuration

From the repository root:

```bash
make -C experiments/single-task clean
make -C experiments/single-task test
```

Set the macros in `checksum/common/common.h` and select `checksum/dpu/dpu.c` before running. The checked-in defaults reference the M=100 files through `../input/`, relative to the `checksum/` working directory used by Make. Direct execution of `build/checksum_host` must also use that working directory. The host prints DPU time and compares CPU/DPU Top-K IDs; its current PASS criterion is at least five matching IDs, not exact equality.

## Parameter sweeps

Run the desired script with Bash from any directory. Paths are resolved relative to the script location.

| Run script | Results directory | Plot script |
|---|---|---|
| `run_tech_m.sh` | `TECH-M/` | `print_tech_m.sh` |
| `run_tech_topk.sh` | `TECH-TOPK/` | `print_tech_topk.sh` |
| `run_dsize.sh` | `SEN-DSIZE/` | `print_dsize.sh` |
| `run_lut_size.sh` | `SEN-LUT_SIZE/` | `print_lut_size.sh` |
| `run_pre_size.sh` | `SEN-PRE_COMPUTE/` | `print_pre_size.sh` |
| `run_check_size.sh` | `SEN-CHECK_SIZE/` | `print_check_size.sh` |

For example, from the repository root:

```bash
bash experiments/single-task/run_tech_m.sh
bash experiments/single-task/print_tech_m.sh
```

Sweeps rewrite macros, input paths and kernel source files, then rebuild for each configuration. Run them sequentially and keep any local source edits separate. Some scripts restore headers and kernels only on normal completion; inspect your working tree after an interrupted run. The `normal.sh` and `run_exit.sh` wrappers select their respective kernel and run the current configuration.

The CHECK_SIZE sweep uses the shared pruning kernel `checksum/dpu/dpu-pimann.txt` and recompiles it for `CHECK_SIZE=1,2,4,5,10,20` with `LUT_SIZE=20`. Each CHECK_SIZE must be a positive divisor of LUT_SIZE, and each M must be a positive multiple of LUT_SIZE. The script checks these constraints and input-file readability before modifying sources. It restores the original header and kernel on normal exit, command failure, SIGINT or SIGTERM; SIGKILL and machine failure cannot trigger cleanup. This is a parameterized sweep of the retained kernel, not a recovery of the missing historical per-value templates; hardware equivalence to historical results has not been verified. The LUT_SIZE sweep still uses its six included per-value templates. Stable kernel templates are not modified by either sweep.

Plot scripts extract the first `DPU time:` value from each log. Check the full logs and host comparison result before interpreting those plots as successful runs. The `.txt` files inside `checksum/dpu/` are C source templates, not experiment logs.
