# Licensing and provenance

The combined source work is **GPL-2.0-only** (LICENSE), except retained upstream license texts. New documentation is GPL-2.0-only too. External firmware retains its own terms; no license to proprietary firmware is implied.

## Turing lineage

kernel/tu102_core_port.h and tests/test_core_port.c adapt haohervchb/Turing-unlock at `a188b24a392414f29f931ea886329227cfbc2f98` (copyleft-next 0.3.1). Full license: LICENSES/copyleft-next-0.3.1.txt. Section 3 permits a derived work incorporating GPL material to be licensed under the GPL. This combined port incorporates original GPL-2.0-only Linux adapter/DMA code. This does not recharacterize the upstream's original license; its notices remain retained.

Upstream attribution:

> Ported from unlock40x_v70.c of the CMP40HX-Unlock project
> (https://github.com/PZH1gdmu/CMP40HX-Unlock, MIT License, Copyright (c) 2026),
> which itself descends from the CMP 90HX (GA102) unlock work and the V67
> canary exploit published by Jon Pry, "A Canary in the Crypto Mine",
> DOI 10.5281/zenodo.20916112. Their MIT license and this notice carry over.

Full MIT texts: LICENSES/CMP40HX-MIT.txt and LICENSES/NVIDIA-MIT.txt.
Firmware input reference: xrip/cmp50hx-unlock `df2fe11b370bb52cc314576d0f2f5ae47ce908f8`. Root source licenses and individual firmware licenses are not interchangeable. Public availability is not by itself redistribution permission.

## Adaptation

The board-specific UEFI wrapper was replaced with Linux early-boot PCI/MMIO/DMA handling. Native firmware descriptors, mapper offsets and hashes were adapted to the measured Leadtek 22 GiB profile. Framebuffer layout changed from 11 to 22 GiB. Per-64-KiB device addresses and a CPU shadow replace contiguous physical DMA assumptions. Linux cleanup, driver override and fail-closed boot gating were added. Tests cover the adapted inputs, noncontiguous addressing, IMEM bounds and failure cleanup.

The TU104 RTX 2060 port (kernel/tu104_*, tools/profile_gen.py) re-parameterizes the same core from a generated per-card profile; it shares the upstream lineage above and performs no hardware run. `kernel/override_compat.h` adapts the Linux driver_override API (private since 6.15); driver semantics unchanged.

Original source: https://github.com/haohervchb/Turing-unlock/tree/a188b24a392414f29f931ea886329227cfbc2f98 . No upstream endorsement implied.
Loader/layout interfaces were compared with NVIDIA/open-gpu-kernel-modules 580.178.04 (`c8e699821c23e4335bf23330a54a23d15cfb95e9`). Driver licenses vary; its repository is not bundled.

## Ampere and measurement references

Ampere implementation: Zagitta/gpu-unlock `a9b0c75bb79b0c9fac6be68feade78d0b8715395`, linked rather than vendored. MAMF: stas00/ml-engineering, with EleutherAI, Imtiaz Sajwani, Xiaoyu Zhang and Oren Leung credited by the original script. Benchmark programs are not bundled. Published measurements are summaries, not complete raw benchmark archives.

## Publication boundary

Excluded: ROM/PROM dumps, native firmware/payload binaries, complete decrypted firmware, motherboard serials, GPU UUIDs, private IPs, usernames/home paths, root-filesystem UUIDs, credentials, raw inventories, kernel/EFI binaries and automatic installers.
Included: source-level register constants, input hashes/lengths and the small signature constant retained from upstream source. No complete NVIDIA firmware or standalone payload is embedded. Obtain inputs independently under applicable terms. The source license does not override those terms; no blanket legal assurance is made.
Hardware IDs, VBIOS version, motherboard model and historical PCI addresses are intentionally documented for reproducibility. Do not remove profile guards to run an unsupported card.
