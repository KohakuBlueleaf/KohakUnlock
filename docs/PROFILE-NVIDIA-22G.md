# NVIDIA 1e07/12a4 22 GiB offline profile

`profiles/nvidia-tu102-300a-22g.json` describes the 22 GiB RTX 2080 Ti
TU102-300A candidate, PCI `10de:1e07`, subsystem `10de:12a4`, VBIOS
`90.02.0B.00.0E`. The core is owner-identified; it is not decoded from the PCI
ID. The physical 22 GiB memory modification is owner-confirmed. The historical
card also had 22 GiB physical memory; software does not add memory.

## Evidence and limits

A read-only pre-udev boot captured an unbound device,
a 1 MiB PROM aperture snapshot and 13 register readouts. The earlier standard
option-ROM export matches its prefix. The full image chain and native FWSEC
parse with bounded offsets. Cold versus warm power state is still unestablished.
PROM aperture size does not establish physical flash capacity.

Native FWSEC is 39440 bytes, with descriptor at `0x3f200`, image at `0x3f23c`,
interface `0xdc`, DMEM `0x9600`, mapper `0x380`, command `0x3d0`. Secure IMEM
starts at `0x300` and spans `0x9300` bytes. The command buffer is `0x40` bytes.
The original observed selector pair was `0x500060 / 5`; a later normal-reboot
pre-driver capture returned `0x20500060 / 5` in all five samples. Both exact
tuples are accepted only by the candidate profile. Neither is decoded as a bin
or fuse field, and neither is added to the historical runtime allowlist.

Capacity `0x2c9` decodes to 22 GiB using the existing TU102/GP102 path; workspace
and WPR2 initial values match the historical snapshot. FRTS `0x57fe00000`, the
107 MiB heap, post-FWSEC WPR and post-Booter selector assertions remain inherited
model assumptions. They have not been observed after firmware execution here.
Equal memory capacity alone does not establish firmware/selector compatibility.

Static dispatch/ABI review used NVIDIA release `580.178.04`, commit
`c8e699821c23e4335bf23330a54a23d15cfb95e9`:

- [Generated memory-system dispatch](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/c8e699821c23e4335bf23330a54a23d15cfb95e9/src/nvidia/generated/g_kern_mem_sys_nvoc.c#L349)
  sends TU102 to `kmemsysReadUsableFbSize_GP102`. Its GP102 implementation reads
  `0x100ce0`; `0x2c9` yields `44 << 29`, or 23622320128 bytes (22 GiB).
- [Generated MMU-lock dispatch](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/c8e699821c23e4335bf23330a54a23d15cfb95e9/src/nvidia/generated/g_mem_mgr_nvoc.c#L1220)
  uses the invalid/not-applicable stub for TU102; the GA100 register path does
  not apply.
- [FWSEC V2 interpretation](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/c8e699821c23e4335bf23330a54a23d15cfb95e9/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_fwsec.c#L747)
  gives nonsecure size `IMEMLoadSize - IMEMSecSize` (`0x300` here), secure base
  `IMEMSecBase - IMEMVirtBase + IMEMPhysBase` (`0x300` here), secure size `0x9300`
  and the descriptor's DMEM offset/size (`0x9600 / 0x410`).
- [TU102 mapper and FRTS command ABI](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/c8e699821c23e4335bf23330a54a23d15cfb95e9/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c#L80)
  places mapper `init_cmd` at byte 44 and uses command `0x15`, a version-1
  24-byte VBIOS read descriptor followed by the 20-byte FRTS region descriptor.
  The region is FB media type 2, offset in 4 KiB units, size `0x100` units.
- [TU102 generic-loader preparation](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/c8e699821c23e4335bf23330a54a23d15cfb95e9/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_falcon_tu102.c#L232)
  uses those native code/data fields and places the block-aligned loader at
  the top of IMEM.

These are TU102-family source paths; the review does not decode a TU102-300/300A
bin distinction. The installed driver is a different version. The source
review supports the offline layout and cannot establish hardware authentication.

## Shared implementation and offline checks

`kernel/tu102_profile.h` supplies compile-time native layout constants to the
same `tu102_core_port.h` used by the historical executor. Its default is the
historical profile. `TX_PROFILE_NVIDIA_1E07_12A4` alone selects the CPU candidate;
kernel selection additionally requires the explicit target trial flag and
`ENABLE_EXPERIMENT=1`. Public/default builds remain disabled. A separately
authorized trial uses `EXEC_PROFILE=nvidia-1e07-12a4-trial`, exact BDF/PCI/subsystem
IDs and native hash, rather than editing the historical allowlist.

The native staging code, PROM guards, generic-loader descriptor and IMEM capacity
minimum now use those profile constants. The generic loader occupies two blocks
above the native IMEM image: minimum `0x98` blocks for the candidate, `0x9a` for
the historical image. Selector acceptance is a finite set of exact per-profile
tuples, with cross-profile tuples rejected. The candidate accepts only
`(0x00500060,5)` or `(0x20500060,5)` and pins the complete initial pair across
allocation/preparation: switching even between those two refuses before writes.
The model's independently transcribed layout
checks the resulting accesses, patched bytes and loader descriptor.

`make test` runs both profiles with explicitly synthetic firmware-free fixtures.
They exercise actual core code but cannot demonstrate firmware authentication,
DMA quiescence or hardware support. No binary fixture is redistributed.

For privately obtained, hash-pinned candidate inputs:

```sh
make test-private-nvidia ASSETS=local/profile-model-inputs
python3 tools/check_profile.py \
  --capture local/diag-capture-20261025T153521249631Z \
  --assets local/profile-model-inputs --output local/new-candidate-evidence.json
```

The private target checks all five input sizes and SHA256 hashes before running
the same actual-core model. Four external inputs retain the historical pinned
CMP revision, attribution and hashes; their compatibility is modeled only.
`check_profile.py` checks saved capture identity, unbound/pre-udev log markers,
PROM hash, native layout, exact register tuple and decoded capacity. It rejects
ambiguous repeated snapshots and refuses to overwrite an output. These are
file-level consistency checks, not attestation of a live device or authorization
to execute firmware. The output always has `execution_allowed=false`.

## Controlled hardware trial

The candidate pins kernel `7.0.0-30-generic` and NVIDIA 595.91.07. On kernels
with the newer PCI override API, `device_lock` still serializes binding but the
override setter does not, so an override writer need not take the mutex. The
executor therefore requires the reviewed pre-udev gate as the first init-top
script, before udev and module coldplug. The gate checks for existing udev/GPU
modules and binding, asserts `pre_udev=1`, and never continues on a missing or
unsafe result. That parameter does not make a live-system invocation supported.

`tools/initramfs-trial-{hook,gate}.example` and `tools/trial-handoff.sh` describe
the target-specific integration. The gate mounts a private FAT log volume,
creates a per-image attempt directory and flushes it before loading the
executor. Existing attempt directories refuse retries. The journal records
stage boundaries with last read/write values and is fsynced before the next
action; firmware inputs are verified before GPU mutation. Successful handoff
keeps a RAM mirror and releases the temporary mount. Failure retains resources,
saves logs and stops before udev. A hard hang can still prevent final records
from being saved.

The separately compiled DMA helper shares `tu102_identity.h` and profile flags
with the executor; a stale identity check previously stopped before allocation.
A rejected selector pair now records both read words and fails before writes.
These are software integration checks, not firmware authentication evidence.

## Single-boot hardware result

The two-pair profile booted on kernel `7.0.0-30-generic` with NVIDIA 595.91.07.
The initial pair was `0x20500060/5`; the native FWSEC and Booter stages
completed, final selectors were `0x88888888/8`, and stage 11 reported unlocked
and quiescent with safe handoff. NVIDIA subsequently owned the device and
accepted ordinary CUDA workloads. This is one successful boot, not repeatability
across boots or long-term stability.

`tools/compute_sustained.cu` used 8192-cubed cuBLAS GEMM for at least 10 GPU
seconds per accumulation mode. FP16 inputs with FP32 output and accumulation
achieved 107.057 TFLOP/s over 10.188155 seconds; FP16 output and accumulation
achieved 112.972 TFLOP/s over 10.277614 seconds. Each final 67,108,864-element
structured-input output passed. Raw logs, exact commands/hashes and telemetry
remain private; no NVIDIA Xid or GPU falloff appeared after testing.

This supports successful unlock for that boot. It is not a measurement of
instruction peak, long-term stability, all-VRAM correctness or cross-boot
repeatability. The original 2048-cubed short-test baseline was preserved; there
is no locked 8192/10-second baseline, so those methods must not be used to claim
a matched performance ratio.

Subsequent 8192-cubed, 10-second INT8/INT32 measurements found 44.301 TOPS with
NN and 169.935 TOPS with TN. First-kernel hardware counters found no IMMA
instruction in NN and 536,870,912 in TN. Each final output passed correctness.
TN is a labeled layout change; the locked NN short-test baseline does not
establish a Tensor-path unlock speedup. Pure FP16 and NN INT8 were essentially
unchanged in the first post-boot short run; a later post-load short run also
accelerated FP32, showing a state/warmup confound. Raw records remain private.

Remaining questions:

- Establish boot power provenance, selector repeatability and repeated success
  across normal boots.
- Independently review the source-backed native loader/command ABI and the
  pinned inputs; the TU102 capacity/MMU-lock dispatch has been checked statically.
- Independently review retained-resource failure handling; host simulation does
  not prove post-write hardware recovery.
- Independently review actual-core model assumptions, including shared 22 GiB
  workspace/FRTS/heap and expected post-firmware states.
- Add random-input, all-VRAM and long-duration validation; the recorded
  structured GEMM outputs and 10-second runs passed but are narrower.
- Measure end-to-end LLM, image-generation and Real-ESRGAN workloads rather than
  assuming the FP16/FP32-accumulate GEMM result transfers unchanged.

No executable candidate, firmware installation, boot selection or GPU mutation
is performed by the offline profile tools or model targets.
