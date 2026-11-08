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

## Factory full-rate control: TITAN RTX (no unlock applied)

A control from an unmodified TITAN RTX. **No module, firmware or boot change was applied.** It tests whether a TU102 card ships with the half-rate FP16→FP32 limit. On the cards below, it does not, so this project has nothing to unlock there.

Cards: two hosts, one card each. TU102 rev a1, PCI ID `10de:1e02`, subsystem `10de:12a3`, VBIOS `90.02.42.00.0F`, stock 24 GB, 72 SMs, 280 W limit.

- Host 1: kernel 5.15.0-139, NVIDIA 550.78.
- Host 2: kernel 6.8.0-64, NVIDIA 570.169.

Benchmark: [mmapeak](https://github.com/KohakuBlueleaf/mmapeak) at `cb43e95423be799eeb038cce0dfca1ba1c5572a8`, built with CUDA 12.1 for `sm_75`, run with `-t 1 -r 3`, seed 12345 (randomized inputs). Clocks were not locked.

| Instruction | Host 1 | Host 2 | Unit |
|---|---:|---:|---|
| FP16→FP16 16×16×16 | 140.1 | 140.9 | TFLOPS |
| FP16→FP16 32×8×16 | 139.7 | 140.7 | TFLOPS |
| FP16→FP32 16×16×16 | 136.5 | 137.5 | TFLOPS |
| FP16→FP32 32×8×16 | 136.8 | 137.9 | TFLOPS |
| INT8→INT32 16×16×16 | 260.9 | 262.2 | TOPS |
| INT8→INT32 32×8×16 | 259.4 | 260.6 | TOPS |
| Scalar FP32 FMA | 17.5 | 17.5 | TFLOPS |

FP16→FP32 runs at about 0.97–0.98× FP16→FP16. A locked 2080 Ti runs at about 0.50× (59.9 vs 119.5 above). This matches TITAN RTX being marketed with full-rate FP32 accumulation. It is only an observation for these two cards and this VBIOS, not a claim about every TU102 SKU.

### Related observations, not unlock results

- **Pascal GP104 has nothing to unlock.** GTX 1080 (`sm_61`) has no tensor-core MMA. Its packed FP16 (`HFMA2`) rate is a silicon property: we measured 0.15 TFLOPS against 7.4 TFLOPS FP32 FMA and 36 TOPS INT8 `DP4A`, with a separate randomized chained-accumulator probe. KohakUnlock does not apply.
- **Quantized LLM decode probably will not speed up.** Low-bit GGUF inference in llama.cpp, for example IQ1_M decode through `mmvq`/`vec_dot` with `__dp4a`, runs on INT8 dot-product paths. The INT8 rows in the 2080 Ti A/B above are flat before and after the unlock. So quantized-decode speedups should not be expected from this unlock alone. FP16 GEMM-heavy workloads (training, FP16 prefill) are the plausible beneficiaries. This is a source-path observation; no end-to-end LLM A/B was run.

## What is NOT established

Turing numerical correctness has not been independently certified by the local public-repo measurement record; the recorded runs here are throughput tests and healthy-driver checks. Upstream's own correctness results apply to its hardware/profile, not automatically this port. A local validation script was drafted during development but its successful execution was not recorded; do not turn that into a pass claim. Independent numerical, memory-integrity and sustained thermal/endurance testing remains required.

No deliberate post-launch hardware failure recovery test, long-duration stability certification, game/LLM speedup measurement, or successful second-card TU102 unlock is claimed. Host simulation, matching hashes and successful boot are valuable but narrower evidence.
