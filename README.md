# KohakUnlock

**Source-first research on volatile NVIDIA tensor-compute unlocks. Not a universal GPU unlocker.**

This repository documents a successful **Linux/initramfs port for one VRAM-modified Leadtek RTX 2080 Ti (11 GB → 22 GB; 22 GiB reported)** and running the upstream Ampere unlock on four RTX 3090s. It includes Turing source, offline tests, ROM analysis, inventory tooling, and a fail-closed boot-hook template. It does **not** include firmware blobs, ROM dumps, prebuilt kernel/EFI binaries, a flash utility, or an automatic installer.

**Risk:** experimental privileged firmware execution can hang a GPU or host, prevent boot, corrupt data, or damage hardware. There is no guarantee of recovery. Have physical/BMC console and power control. Never experiment on an active workload. Editing IDs or removing guards does not validate a new card.

**The 22 GB cards are physically VRAM-modded, not factory 22 GB models.** Both the unlocked card and locked comparison card already had that modification. This software unlock does not add VRAM, and the repository does not provide a memory-soldering guide.

## Results

| Test | Locked | Unlocked | Comparison |
|---|---:|---:|---|
| 2080 Ti FP16→FP32 `mmapeak`, 32×8×16 | 59.9 TFLOPS | 111.0 TFLOPS | Same card before/after; +85.3% |
| 2080 Ti FP16→FP16 control | 119.5 TFLOPS | 118.5 TFLOPS | Same card; essentially unchanged |
| 2080 Ti best MAMF median, 125-shape sweep | 57.71 TFLOPS | 88.24 TFLOPS | Two same-model cards; +52.9%, not same-card A/B |
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

- One Leadtek TU102 rev a1: `10de:1e04 / 107d:1e04`, VBIOS `90.02.33.00.3F`, VRAM-modded to 22 GiB, 68 SMs; kernel 6.8.0-147 and NVIDIA open 595.99.02. Second same-model card was the locked control, **not a second validated unlock**.
- GIGABYTE MZ32-AR0 / AMD server, separate ASPEED display, Secure Boot disabled. Shared IOMMU group observed: no bus-wide resets.
- Turing: **one-shot only**, no module unload path, mandatory fail-closed initramfs gate. Do not make it the permanent default.
- Ampere: external [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock); not vendored here.
- The TU104 RTX 2060 port in this fork is **source-only and unvalidated**: offline host tests with a synthetic profile pass, but no hardware run has occurred; it requires its own PROM-derived profile (see docs/TU104-2060.md).
- Other boards, 11 GiB variants, Windows, Ada and Blackwell: **not validated here**.

Public source derives from the tested source with a default-off compile gate and publication packaging changes. It is not byte-identical to the historical test binary. No public-source hardware rerun is claimed.

## Attribution

Based on [haohervchb/Turing-unlock](https://github.com/haohervchb/Turing-unlock), [xrip/cmp50hx-unlock](https://github.com/xrip/cmp50hx-unlock), CMP40HX work, Jon Pry's V67 research, and NVIDIA driver interfaces. We do not claim discovery of the exploit. See [NOTICE.md](NOTICE.md).
