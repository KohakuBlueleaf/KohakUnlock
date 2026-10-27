# Small CUDA compute baseline

`tools/compute_smoke.cu` runs ordinary cuBLAS workloads through the installed
NVIDIA driver. It does not load this project's modules or execute its firmware.
Build for the actual GPU architecture (TU102 is `sm_75`):

```sh
nvcc -O2 -std=c++17 -arch=sm_75 tools/compute_smoke.cu -lcublas -o build/compute_smoke
./build/compute_smoke 0000:01:00.0
```

Select the target by its actual PCI BDF. The program resolves it with the CUDA
PCI API and prints the resolved BDF and GPU properties. Do not put a PCI address
in `CUDA_VISIBLE_DEVICES`; that environment variable has different semantics.
Run on an idle GPU and keep results privately under `local/`. Record driver,
toolkit, power/clock conditions and the source revision for any before/after
comparison. The program prints CUDA and cuBLAS versions.

Four square 2048-element GEMMs use FP32 inputs with FP32 accumulation, FP16
inputs with FP32 or FP16 accumulation, and INT8 inputs with INT32 accumulation.
FP32 uses pedantic computation to exclude reduced-precision substitution. The
other cases allow the library's default algorithm selection; they do not prove
which machine instructions ran. Each case warms up three times, then measures
20 GEMMs with CUDA events, excluding allocation, transfers and validation.
FMA counts as two operations; integer throughput is TOPS, floating point is
TFLOP/s. FP16-to-FP16 is a separate control for FP16-to-FP32.

Inputs alternate +1/-1 by row and column. Every output is checked against the
exact expected +2048/-2048 value. This is a basic arithmetic smoke test, not
general numerical validation, sustained performance, memory capacity testing,
instruction peak measurement or evidence that an unlock succeeded. Keep this
methodology fixed when comparing its results.

## Sustained GEMM

`tools/compute_sustained.cu` is the separate long-run benchmark used for the
kernel 7 / `1e07:12a4` profile. It keeps the same cuBLAS GEMM and correctness
checks but runs each selected mode for a requested GPU-event duration:

```sh
nvcc -O2 -std=c++17 -arch=sm_75 tools/compute_sustained.cu -lcublas -o build/compute_sustained
./build/compute_sustained 0000:01:00.0 10 8192 all
```

Modes are `fp16` (FP16 input, FP32 accumulation/output), `pure-fp16`,
`int8` (NN), `int8-tn` (transposed-A Tensor layout) and `all`. The program
rejects non-SM 7.5 devices, requires a multiple of 256 in `2048..16384`, and
checks the complete final output. Do not treat a slower NN INT8 result as a
Turing Tensor peak: ordinary column-major IMMA uses TN. Keep raw runs private;
the summarized measurements and their limits are in RESULTS.md.

The kernel compatibility patch is separate from this benchmark. On kernels
where PCI uses the new device override API, `ENABLE_EXPERIMENT=1` alone remains
insufficient: the executor requires the reviewed pre-udev gate and its explicit
assertion. The device mutex still serializes binding, while override writers use
a separate lock. See PROFILE-NVIDIA-22G.md for the controlled early-boot trial;
this benchmark neither enables it nor establishes its safety.
