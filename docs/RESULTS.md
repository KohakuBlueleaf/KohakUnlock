# Observed results and limits

Historical measurements: 2026-10-25, one physically VRAM-modified Leadtek RTX 2080 Ti (stock 11 GB → 22 GB, reporting 22 GiB) unlocked by a one-shot Linux/initramfs experiment. The other same-model, also 22 GB VRAM-modded card remained compute-locked. The memory modifications predate this experiment; the software adds no VRAM. Kernel 6.8.0-147, NVIDIA open 595.99.02; power limit 250 W on both. Original record retained privately; public tables are summarized measurements, not complete raw logs.

## Same-card mmapeak A/B

Short instruction tests, target 0.3 s each. Same physical GPU selected by verified UUID before/after. Clocks not locked or normalized, so ratios are measured throughput, not exact per-clock rates.

| Instruction | Locked | Unlocked | Unit |
|---|---:|---:|---|
| FP16→FP32 16×16×16 | 59.9 | 110.9 | TFLOPS |
| FP16→FP32 32×8×16 | 59.9 | 111.0 | TFLOPS |
| FP16→FP16 16×16×16 | 119.5 | 118.5 | TFLOPS |
| FP16→FP16 32×8×16 | 119.5 | 118.5 | TFLOPS |
| INT8→INT32 16×16×16 | 238.6 | 236.5 | TOPS |
| INT8→INT32 32×8×16 | 236.8 | 236.6 | TOPS |

Approximately +85% on the targeted FP32-accumulating path; controls essentially flat. Native BF16/TF32 remained unsupported.

## Turing TU102-300A, kernel 7.0.0-30

Second, separate one-shot result: 22 GiB RTX 2080 Ti TU102-300A, PCI `10de:1e07`,
subsystem `10de:12a4`, VBIOS `90.02.0B.00.0E`, physically VRAM-modified to
22 GiB. Kernel `7.0.0-30-generic`, NVIDIA 595.91.07. The initial selector pair
was `0x20500060 / 5`; the transaction reached stage 11 with `unlocked=1`,
`quiescent=1` and safe handoff, then NVIDIA bound the device normally. The
second exact pair was owner-authorized after five read-only samples; bit 29 is
not decoded.

Same-method short test: `2048³` cuBLAS GEMM, 3 warmups, 20 timed iterations,
structured ±1 inputs, complete output checked.

| Mode | Locked | Unlocked, first post-boot run | Change |
|---|---:|---:|---:|
| FP32→FP32 | 10.799 TFLOPS | 10.798 TFLOPS | flat |
| FP16→FP32 | **40.777 TFLOPS** | **71.514 TFLOPS** | **+75.4%** |
| FP16→FP16 | 76.089 TFLOPS | 76.098 TFLOPS | flat |
| INT8→INT32 NN | 37.139 TOPS | 37.028 TOPS | flat |

Sustained runs used `8192³`, 10 warmups, batches of 32 GEMMs, at least 10 GPU
seconds per mode; transfers and validation are excluded.

| Mode | Throughput | GPU time | Final output |
|---|---:|---:|---|
| FP16→FP32 | 107.057 TFLOP/s | 10.188155 s | 67,108,864 passed |
| FP16→FP16 | 112.972 TFLOP/s | 10.277614 s | 67,108,864 passed |
| INT8→INT32 NN | 44.301 TOPS | 10.324857 s | 67,108,864 passed |
| INT8→INT32 TN | 169.935 TOPS | 10.145256 s | 67,108,864 passed |

The locked baseline has no 8192/10-second run, so 107.057/40.777,
112.972/76.089 and 169.935/37.139 must not be reported as matched unlock
ratios. The FP16→FP16 control was already near 76 TFLOPS while locked, and the
sustained value is close to NVIDIA's 107.6–113.8 TFLOPS FP16-accumulate
specification for this SKU. The clearest same-method recovery is therefore the
FP16-input, FP32-accumulate short test. No locked 8192 INT8 Tensor baseline or
locked telemetered run exists.

The INT8 NN first kernel executed no IMMA instructions; the TN kernel executed
536,870,912. TN/NN is a labeled layout change inside the unlocked boot, not an
unlock speedup. A later short rerun after sustained load also raised FP32, pure
FP16 and NN INT8 by roughly 36–37%, showing a warmup/state confound; those
changes are not attributed to the unlock.

Limits: one successful boot, one card, structured inputs, no random-data or
all-22-GiB verification, no long endurance run, no cross-boot repeatability and
no end-to-end LLM, image-generation or Real-ESRGAN measurement. Clocks were not
locked and power/thermal history was not normalized.

## Original MAMF engine, two-card comparison

FP16, PyTorch 2.8.0+cu129, 30 s initial warmup, 50 warmup + 100 measured iterations per shape. Original transposed-B layout, cache/destination-reset behavior, timing function retained; terminal UI changed only. No claim of strict same-card baseline. Silicon/cooling/clock differences between cards remain possible.

Manual sweep: M/N/K independently 2048..6144 inclusive, step 1024, 125 shapes. Best locked median 57.71 TFLOPS at 2048×2048×3072; best unlocked median 88.24 at 2048×2048×4096, +52.9% across best candidates.

| Shape | Locked median | Unlocked median | Unit |
|---|---:|---:|---|
| 2048×2048×2048 | 57.21 | 87.83 | TFLOPS |
| 2048×2048×3072 | 57.71 | 86.98 | TFLOPS |
| 2048×2048×4096 | 57.66 | 88.24 | TFLOPS |
| 2048×3072×2048 | 57.00 | 86.52 | TFLOPS |
| 2048×3072×3072 | 56.80 | 85.40 | TFLOPS |
| 2048×3072×4096 | 56.70 | 85.89 | TFLOPS |
| 2048×2048×6144 | 57.07 | 84.52 | TFLOPS |

Separate single-shape runs: 4096³ locked/unlocked median 54.34/76.99; 8192³ 55.33/76.15. Do not mix them with sweep scores as if conditions/order were identical.

## Ampere

Four 3090s, sequential PyTorch 8192³ tests at 360 W: FP16 122.76/122.96/127.40/115.21; BF16 126.57/127.45/130.39/118.39 TFLOPS. Numerical sanity checks passed in that run.

### Recovered stock baseline for one card

The early `unlock-stock-82.json` capture was labeled for `82:00.0`, but the PCI-address CUDA selector actually selected `01:00.0`. Its per-window telemetry shows that card boosting while the intended target remained idle; a later explicit CUDA properties query using the same selection confirmed `01:00.0`. The later four-card run selected this card by UUID. The baseline is therefore useful for **that one physical card**, not the originally intended card or the entire four-card set.

| 8192³ GEMM mode | Stock TFLOPS | Unlocked TFLOPS | Observed change |
|---|---:|---:|---:|
| FP16→FP32 accumulation | 73.03 | 122.76 | +68.1% |
| BF16→FP32 accumulation | 73.82 | 126.57 | +71.5% |
| TF32 | 38.48 | 65.42 | +70.0% |
| Ordinary FP32 | 27.60 | 25.59 | −7.3% |

Both saved reports have benchmark script SHA256 `64b4ed9fed4c50da812c8c3529aa9cc535d5f6cda5cbf1c4c473fa8a147aefe8`, PyTorch 2.13.0+cu130, five timing windows and ten iterations/window. Numerical-check output hashes match for each mode; these are the script's bounded correctness cases, not whole large-GEMM parity tests.

**Not a controlled A/B:** stock power cap 370 W, unlocked cap 360 W; cooling, clocks and thermal history differ. Stock swept 4096 and 8192, while the unlocked comparison ran 8192 only. Percentages are calculated from unrounded medians. A separate recovered-stock run also measured approximately 73.40 FP16 / 73.60 BF16 / 38.37 TF32 / 25.52 FP32 TFLOPS, illustrating variation, particularly in the FP32 control. No individual stock baselines were recovered for the remaining three cards.

## What is NOT established

The recorded Turing runs include complete final-output checks for structured ±1 inputs, healthy-driver checks and sustained GEMM throughput. They are not independent numerical certification: random inputs, per-iteration checking, all-memory testing and long-duration thermal/endurance verification remain required. Upstream's own correctness results apply to its hardware/profile, not automatically to either port.

No deliberate post-launch hardware failure recovery test, long-duration stability certification, application-level (LLM/image-generation/Real-ESRGAN) speedup measurement, or successful second physical copy of the `1e07:12a4` profile is claimed. Host simulation, matching hashes and successful boots are valuable but narrower evidence.
