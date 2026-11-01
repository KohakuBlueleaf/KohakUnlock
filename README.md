# KohakUnlock

**Source-first research on volatile NVIDIA tensor-compute unlocks. Not a universal GPU unlocker.**

**RTX 2080 Ti Linux/initramfs and RTX 4090 Windows/UEFI unlocks**, with RTX 3090 upstream deployment experience. Includes source, offline tests, firmware/ROM analysis guidance, boot workflow documentation and measured results.

- **2080 Ti:** physically VRAM-modded Leadtek cards, **11 GB → 22 GB**.
- **4090:** AD102 USB EFI transaction, stock Windows NVIDIA driver handoff, runtime-randomized instruction benchmarks.
- **3090:** four-card deployment using the upstream Ampere implementation.

Firmware, ROM dumps, signing keys and compiled kernel/EFI binaries stay local.

**Risk:** experimental privileged firmware execution can hang a GPU or host, prevent boot, corrupt data, or damage hardware. There is no guarantee of recovery. Have physical/BMC console and power control. Never experiment on an active workload. Editing IDs or removing guards does not validate a new card.

**The 22 GB cards are physically VRAM-modded, not factory 22 GB models.** Both the unlocked card and locked comparison card already had that modification. This software unlock does not add VRAM, and the repository does not provide a memory-soldering guide.

## Results

| Test | Locked | Unlocked | Comparison |
|---|---:|---:|---|
| 2080 Ti FP16→FP32 `mmapeak`, 32×8×16 | 59.9 TFLOPS | 111.0 TFLOPS | Same card before/after; +85.3% |
| 2080 Ti FP16→FP16 control | 119.5 TFLOPS | 118.5 TFLOPS | Same card; essentially unchanged |
| 2080 Ti best MAMF median, 125-shape sweep | 57.71 TFLOPS | 88.24 TFLOPS | Two same-model cards; +52.9%, not same-card A/B |
| 4090 FP16→FP32 randomized `mmapeak` | 178.9 TFLOPS | 331.5 TFLOPS | Same card/binary; +85.3% |
| 4090 BF16→FP32 randomized `mmapeak` | 179.0 TFLOPS | 330.9 TFLOPS | Same card/binary; +84.9% |
| 4090 FP16 MAMF Finder, best median of 125 shapes | ~160 TFLOPS | 243.9 TFLOPS (measured) | WSL2; 4096x5120x2048 |
| One 3090, FP16→FP32 8192³ PyTorch GEMM | 73.03 TFLOPS | 122.76 TFLOPS | Same card; +68.1%* |

*3090 power limits: 370 W stock / 360 W unlocked. [Measurement details](docs/RESULTS.md#ampere).

See [results and limitations](docs/RESULTS.md). A successful kernel log alone is not proof of higher throughput. BF16/TF32 are not added to Turing. This is not a claim of a 2× application/game speedup.

## Start here

1. [Safety and scope](docs/SAFETY.md)
2. [Agent engineering guide](docs/AGENT-GUIDE.md): investigation, porting, failure lessons
3. [2080 Ti reproduction](docs/TURING.md): inputs, build, one-shot boot, verification, recovery
4. [3090 experience](docs/AMPERE.md): upstream and known-working revision
5. [RTX 2060 (TU104) port](docs/TU104-2060.md): source-level port, unvalidated; per-card profile generation
6. [Licensing/provenance](NOTICE.md)
5. [4090 Windows/UEFI source, results and references](docs/ADA-WINDOWS.md)
6. [Agent-oriented Ada porting guide](docs/ADA-AGENT-GUIDE.md): inventory, ROM extraction, FWSEC parsing, profile adaptation, ownership, tests and signing
7. [Licensing/provenance](NOTICE.md)

### Benchmark implementation

4090 results use [our mmapeak fork](https://github.com/KohakuBlueleaf/mmapeak).
The original compile-time all-zero fragments allowed the compiler to collapse
identical tile calculations while the host still counted full-tile FLOPs in inspected
builds. Our fork uses runtime-randomized inputs. See the [analysis and pinned revision](docs/ADA-WINDOWS.md#original-mmapeak-zero-input-issue).

### Offline entry points

```sh
# Linux, Python 3.10+, C compiler; no GPU or sudo needed
make test
# Protocol simulation with privately supplied firmware; no GPU access
make test-private ASSETS=/absolute/path/to/verified/assets
# Compile only against matching installed kernel headers
make module
```

`make test` also runs the offline TU104 RTX 2060 host suite (synthetic
firmware image; no assets required). `make module` builds both the validated
TU102 pair and the **unvalidated** TU104 port (`tu104_execute.ko`,
`tu104_diag.ko`) as hardware-execution-disabled modules by default. A reviewed
exact-profile experiment can opt into compilation with
`make module ENABLE_EXPERIMENT=1`; runtime parameters are still required. The
TU104 port additionally requires a generated per-card profile via
`tools/profile_gen.py`; see [docs/TU104-2060.md](docs/TU104-2060.md). No
target installs/loads modules, edits boot configuration, flashes anything, or
runs external installers.

```sh
python3 tools/inventory.py --bdf 0000:42:00.0 --output local/inventory.json
python3 tools/rom_analyze.py local/card-prom.bin --extract local/native-fwsec.bin
python3 tools/check_assets.py --assets local/assets
```

Inventory uses OS metadata and `nvidia-smi`, not BAR mmap or resets. Output is private and gitignored. ROM analysis is file-only; obtaining a full dump is a separate hardware operation covered in the guide.

## Tested boundary

- Two Leadtek TU102 rev a1 cards: `10de:1e04 / 107d:1e04`, VBIOS `90.02.33.00.3F`, physically VRAM-modded to 22 GiB, 68 SMs; kernel 6.8.0-147 and NVIDIA open 595.99.02. Both unlocked in the local sequential implementation. The public executor remains single-target.
- Two-card NCCL all-reduce passed from 1 KiB to 64 MiB with NV2 topology, bidirectional peer access and NCCL direct P2P. At 64 MiB: **36.77 GB/s** algorithm bandwidth.
- GIGABYTE MZ32-AR0 / AMD server, separate ASPEED display, Secure Boot disabled. Shared IOMMU group observed: no bus-wide resets.
- Turing public reproduction: one-shot entry and mandatory initramfs gate; local combined 3090+2080Ti persistent setup was separately validated. [Dual-card notes](docs/DUAL-TURING.md).
- Ampere: external [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock); not vendored here.
- The TU104 RTX 2060 port in this fork is **source-only and unvalidated**: offline host tests with a synthetic profile pass, but no hardware run has occurred; it requires its own PROM-derived profile (see docs/TU104-2060.md).
- Other boards, 11 GiB variants, Windows, Ada and Blackwell: **not validated here**.
- Ada: one RTX 4090 Windows/UEFI manual USB unlock on ASUS Pro WS W890E-SAGE SE, NVIDIA 621.01, 450 W. Signed EFI, successful trial with Secure Boot off. [Source](windows/ad102) and [results](docs/ADA-WINDOWS.md).
- Other board profiles and Blackwell remain research targets.

Public source derives from the tested source with a default-off compile gate and publication packaging changes. It is not byte-identical to the historical test binary. No public-source hardware rerun is claimed.

## Attribution

Based on [haohervchb/Turing-unlock](https://github.com/haohervchb/Turing-unlock), [xrip/cmp50hx-unlock](https://github.com/xrip/cmp50hx-unlock), CMP40HX work, Jon Pry's V67 research, and NVIDIA driver interfaces. We do not claim discovery of the exploit. See [NOTICE.md](NOTICE.md).
