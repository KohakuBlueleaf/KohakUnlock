# RTX 4090 Windows / UEFI

A principal implementation alongside the physically VRAM-modded 2080 Ti Linux
port. RTX 3090 remains an upstream deployment reference.

## Hardware result

One RTX 4090 AD102 24 GiB, 10de:2684 / subsystem 10de:167c, VBIOS
95.02.18.80.EF; ASUS Pro WS W890E-SAGE SE BIOS 0402, with two Intel Arc Pro B50s.
Windows NVIDIA 621.01, native CUDA 13.2, 450 W unchanged before/after.
RUN03 completed one FWSEC/SEC2 transaction, required permission gates, selector
writes SS0=0x88888888 / SS1=8, one FLR, cleanup, GOP reconnection and Windows
handoff. Effective speed readout remained zero after FLR and GOP reconnection.
FBPA was the known nonessential mismatch; all required gates passed.
No VBIOS flash, NVIDIA driver replacement or internal boot modification.

The EFI was locally signed, but this successful hardware boot had **Secure Boot
off**. Enabled Secure Boot is supported by policy/tests, not hardware-validated here.

## Before/after instruction rates

Used [KohakuBlueleaf/mmapeak](https://github.com/KohakuBlueleaf/mmapeak) revision
`cb43e95423be799eeb038cce0dfca1ba1c5572a8`, same Windows executable SHA-256,
`-t 1 -r 3 -v`, seed 12345. Largest tile median per format; three repeats.
Both unlocked runs are retained, rather than discarding the first as proven pollution.

### Floating-point Tensor Cores (TFLOPS)

| Input | Accumulator | Locked | First unlocked | Unlocked rerun | Rerun change |
|---|---|---:|---:|---:|---:|
| FP8 E4M3 | FP16 | 707.3 | 637.9 | 643.4 | -9.0% |
| FP8 E4M3 | FP32 | 357.0 | 644.0 | 642.4 | +79.9% |
| FP16 | FP16 | 357.0 | 324.5 | 331.2 | -7.2% |
| FP16 | FP32 | 178.9 | 322.1 | 331.5 | +85.3% |
| BF16 | FP32 | 179.0 | 324.0 | 330.9 | +84.9% |
| TF32 | FP32 | 89.4 | 161.4 | 163.6 | +83.0% |

### Integer Tensor Cores (TOPS)

| Input | Accumulator | Locked | First unlocked | Unlocked rerun |
|---|---|---:|---:|---:|
| INT4 | INT32 | 1342.3 | 1209.3 | 1258.8 |
| INT8 | INT32 | 695.7 | 642.9 | 651.8 |

### Scalar FMA (TFLOPS)

| Input | Accumulator | Locked | First unlocked | Unlocked rerun |
|---|---|---:|---:|---:|
| FP32 | FP32 | 80.5 | 75.8 | 76.7 |
| FP64 | FP64 | 1.4 | 1.3 | 1.3 |

Control paths declined too. Clock/thermal/background conditions were not matched
closely enough to attribute those declines. These are dense instruction rates,
not end-to-end GEMM/game/inference gains. Finite outputs were checked; exhaustive
numerical correctness, endurance and sleep/resume are not established.

## MAMF Finder application GEMM

Unlocked RTX4090 FP16 MAMF Finder: **243.9 TFLOPS best median** at
4096x5120x2048; best peak250.4 TFLOPS, best mean224.2 TFLOPS.
Original measurement loop:50 warmup +100 measured iterations,125 shapes,
M/N/K each2048..6144 step1024. WSL2, PyTorch2.14.1+cu130.
| Before | After (best median) |
|---:|---:|
| ~160 TFLOPS | 243.9 TFLOPS |

## Original mmapeak zero-input issue

[ReinForce-II/mmapeak](https://github.com/ReinForce-II/mmapeak) uses compile-time
all-zero fragments. In inspected CUDA 13.2 / SM120a SASS, identical FP16/BF16 tile
computations were collapsed while FLOP accounting still counted the full tile.
That inflated results. This is a compiler/counting problem, not proof that zero
data intrinsically doubles hardware throughput, nor that every kernel/compiler
behaves identically.

Our fork uses runtime-randomized bounded inputs and nonzero scales, correct packed
formats, unique output storage and explicit scalar FMA chains. It also removes
redundant post-MMA barriers and uses an SM-scaled launch. These are intentional
methodology changes. The 4090 A/B uses the SAME improved binary. Historical Turing
original-mmapeak results should not be silently relabeled as randomized results.
The executable prints per-kernel dtype sections; tables here are summaries.

## Source and deployment model

[windows/ad102](../windows/ad102) contains source, tests and retained notices.
Public build defaults to no execution; the private Windows ESP GUID is an inert
placeholder. This public packaging is not byte-identical to the tested EFI.

UEFI USB -> identify/verify ROM and ownership -> preload Windows -> manual U ->
release target GOP -> firmware transaction -> selectors -> FLR -> cleanup ->
GOP reconnect -> Windows directly, without a system reboot between stages.

The workstation exposed 317 PCI handles and four target display children with
only one active framebuffer. The port scans the complete UEFI handle buffer,
validates inactive-child ancestry/ownership without requiring a framebuffer,
checks all children and collects independent preflight results. Independent Intel
GOP identities/state remain checked. Actual integrity/ownership/execution faults
still block mutation or handoff; an inactive display alone does not.

FAT32 USB layout: `EFI/BOOT/BOOTX64.EFI` and `RTX4090-UNLOCK/ASSETS/`.
Keep stock Windows Boot Manager as default. Cold-start, select USB manually, U
once at READY. Failure recovery: full power-off and boot Windows without USB.
No automatic installer, boot-order editing or signing keys are distributed.

A USB-less layout is also supported: the EFI on the Windows ESP with the
work tree on a separate FAT32 payload partition. Identify that partition by
its GPT GUID, never by its index: partition numbers are not stable. On the
tested box `diskpart` renumbered the payload from the planned partition 5 to
partition 4 after the C: shrink, and a run pinned to the stale number failed
(measured 2026-10-26). The GUID is unique and authoritative; the index is a
wildcard.

For Secure Boot enabled, sign a reviewed local EFI and append its public cert to
firmware db (not shim MOK), preserving existing keys. The app does not enroll keys
or override authentication. Cryptographic signing is not proof of firmware trust.

See [agent porting guide](ADA-AGENT-GUIDE.md) for extraction, adaptation and tests.

## References and licenses

- [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker), `51df9c17ec89eb8486df52cc9079dde4bd1f9974`: immediate UEFI source and Ada payload research.
- [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada), `4e7fefcc4923f9e083d5324dc75a7d2f906af77f`: AD102 evidence and pinned firmware generators.
- [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock): underlying transaction lineage.
- [impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK): UEFI/Windows handoff reference.
- [Goldlionren/gpu-unlock-4080s](https://github.com/Goldlionren/gpu-unlock-4080s): local signing/db enrollment reference.
- [NVIDIA open GPU kernel modules](https://github.com/NVIDIA/open-gpu-kernel-modules): firmware/interfaces.

GPL-derived UEFI code: GPL-2.0-only. EDK2 and per-file NVIDIA/MIT notices retain
their terms. Firmware is separately licensed and excluded. No upstream TOTORONG
CC-BY-NC narrative documents or disassembly excerpts are copied; these guides
are independently written. See the retained LICENSE and third-party notices.
