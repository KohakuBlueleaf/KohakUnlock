# Reproducing the 2080 Ti experiment

This is a porting/reproduction guide, not a universal installation recipe. Read SAFETY.md first. No command here flashes firmware. Do not execute an unreviewed port.

**The test cards were physically VRAM-modded from stock 11 GB to 22 GB.** This modification existed before the compute unlock. The 22 GiB firmware/layout profile is not a factory 2080 Ti configuration and must not be applied to an unmodified 11 GB card.

## 1. Inventory and native inputs

Use `tools/inventory.py --bdf DOMAIN:BUS:DEVICE.FUNCTION --output local/inventory.json` on the target Linux host. Record GPU PCI/subsystem IDs, VBIOS, memory, driver/kernel, IOMMU grouping, parent bridges, and separate display/recovery access. Resolve runtime benchmark selection by UUID; PCI BDF strings are not reliable CUDA_VISIBLE_DEVICES selectors.

Obtain your own complete native PROM dump through a reviewed method. In our run, sysfs `rom` export needed a temporary enable and returned only 130560 bytes; a one-call read initially returned 4096 bytes. Neither was sufficient for the NVIDIA extensions. A separately authorized read-only 1 MiB PROM-aperture capture at BAR0+0x300000 was parsed. This is a hardware access operation, not included in the automatic inventory tool. Keep private captures out of git. Never turn a truncated file into a valid input by padding it.

`python3 tools/rom_analyze.py local/prom.bin --extract local/assets/leadtek-fwsec.bin` follows the ROM chain, NPDE/BIT and ucode entry rather than applying ASUS offsets. It writes only native extracted bytes to a new file; no patching or hardware operations. Successful parsing does not validate executable behavior.

The exact tested profile expects FWSEC size 39968 and hash in profiles/leadtek-tu102-22g.json. Mismatch means stop and investigate a new profile, not edit the hash blindly.

## 2. External assets

The profile lists five input names/sizes/hashes. Four inputs were sourced privately from https://github.com/xrip/cmp50hx-unlock at `df2fe11b370bb52cc314576d0f2f5ae47ce908f8`, under `efi-unlock/blobs/`. No binary is redistributed here. Check the applicable terms yourself and obtain them separately; no external installation scripts are needed or endorsed. Copy the exact inputs you are authorized to use into a private directory alongside native `leadtek-fwsec.bin`.

```sh
python3 tools/check_assets.py --assets local/assets
make test
make test-private ASSETS="$PWD/local/assets"
```

Private protocol tests use the actual native image in a simulated register model; they never access a GPU. The successful source run had 445 such cases. The source model does not independently validate firmware authentication or its internal execution.

## 3. Probe before mutation

Build `tu102_diag.ko` with matching Linux headers, inspect its source, and place it in a **separate** diagnostic initramfs before the udev coldplug script. Explicitly select the target; keep normal boot as default. No GPU firmware is executed by that diagnostic.

Required pre-driver observations include BOOT0, TU102 capacity register 0x100ce0, workspace 0x625f04, WPR2 boundaries, selector/permission state, halted GSP/SEC2 and full native PROM. Current diagnostic covers the principal registers; the executor additionally checks GFW/readout registers itself before mutation. Log checks must identify the correct card and boot. A warm-reboot pre-driver snapshot is not automatically a power-on-cold snapshot.

Our observed capacity=0x2c9 decodes as `((reg >> 4) & 63) << ((reg & 15)+20)`, with ECC adjustment if bit30 is set: 22 GiB. Workspace=1 selects the fallback 1 MiB top reservation. Candidate FRTS=0x57fe00000 was later confirmed by successful FWSEC execution. This is not a rule for every TU102 board.

## 4. Build the reviewed experiment

Default `make module` cannot execute. After matching/reviewing the complete profile:

```sh
make module ENABLE_EXPERIMENT=1 KDIR=/lib/modules/$(uname -r)/build
```

This does not install/load anything. Compiler and kernel must match. Host-only tests require a C compiler; default ASan/UBSan can be disabled with SANITIZE= only for a justified environment limitation. Private tests require actual external inputs; the public default tests do not.

The executor uses 512 separate 64 KiB coherent blocks, a CPU shadow and per-block DMA pointers. It must not assume a contiguous 32 MiB allocation. External files are loaded through request_firmware and authenticated against pinned SHA256 before mutation.

### Exact `10de:1e07 / 10de:12a4` profile on kernel 7.0.0-30

The NVIDIA candidate is a separate profile, not a change to the historical
Leadtek identity. It requires the explicit target trial build and keeps
`ENABLE_EXPERIMENT=1` as the public build gate:

```sh
make test-private-nvidia ASSETS="$PWD/local/profile-model-inputs"
make module ENABLE_EXPERIMENT=1 DIAG_PROFILE=nvidia-1e07-12a4 \
  EXEC_PROFILE=nvidia-1e07-12a4-trial
```

The profile pins `0000:01:00.0`, `10de:1e07 / 10de:12a4`, native FWSEC size
39440 and its SHA256, the `0x9600` DMEM layout, and only the two exact initial
selector pairs `0x00500060/5` or `0x20500060/5`. The selected pair is pinned
across preparation; switching pairs still refuses. Bit 29 is not decoded.
All identity, native-image, capacity, post-Booter `0x88888888/8`, quiescence
and safe-handoff checks remain. The candidate build succeeded once on this
profile. Treat that as one boot, not repeatability.

For the candidate gate, use the reviewed kernel-7 templates
`tools/initramfs-trial-{hook,gate}.example` and verify the exact module and five
input hashes, init-top order, target identity and kernel before rebooting. The
log path is private and machine-specific; never copy another host's filesystem
UUID. The public examples remain one-shot and do not install a default entry.

## 5. One-shot initramfs integration (manual, machine-specific)

There is deliberately no `make install`.

1. Preserve your current kernel/initramfs/GRUB configuration and verify a known-good boot entry.
2. Install the reviewed module only for the tested kernel; place verified assets under `/usr/lib/firmware/tu102-lab/` with their exact expected names. Run depmod for that kernel.
3. Copy the existing initramfs configuration into a private build directory. Do NOT replace the stock initramfs. Include the module and all five firmware files in a separately named image.
4. Adapt `tools/initramfs-hook.example` and `tools/initramfs-gate.example`. The gate MUST execute before init-top/udev and before NVIDIA. Existing Ampere unlock, if used, may run first with explicit separate card targets.
5. Inspect the generated image with lsinitramfs/unmkinitramfs. Verify exact module hash, five asset hashes and init-top/ORDER. Do not trust a successful build exit alone.
6. Add a separate GRUB menu entry copying the machine's existing kernel/root arguments, changing only the initramfs and adding `tu102_execute.target=0000:42:00.0 tu102_execute.execute=1` for the exact historical profile. Other targets require a reviewed port. Never copy someone else's filesystem UUID.
7. Validate generated GRUB syntax; select with `grub-reboot ENTRY_ID`, not GRUB_DEFAULT. Confirm next_entry and retain a visible menu.
8. Ensure all jobs are stopped and recovery console is available. Reboot once. Never loop automatic retries.

Our permanent 3090 default was preserved; this Turing experiment was NOT made persistent. Default-off source and examples are publication changes; rebuild/review rather than expecting the historical binary hash.

## 6. Interpretation and recovery

Success requires all of:
- `core=0 stage=11 mutated=1 unlocked=1 quiescent=1`;
- `result=0 handoff_safe=1`, resources released, NVIDIA binds normally;
- all expected GPUs healthy, no new Xid/PCI/driver errors;
- increased FP16→FP32 instruction throughput and separate numerical validation.

If safe is N/missing, stop before NVIDIA: keep buffers and override, capture console, fully power off/on into normal boot. Do not remove the override or force-bind the card. A safe pre-write refusal may continue normal boot but is NOT success.

Normal next boot bypasses Turing. Removing the experiment means deleting its custom GRUB entry, regenerating GRUB, and retiring its separate image/module/assets only when no longer needed. Do not regenerate the stock image blindly. A full power cycle clears volatile state more reliably than a warm reboot.

## Not established

Long-duration stability, repeatability across different board firmware, two-card TU102 unlock, native Windows support, arbitrary driver/kernel upgrades, and automatic persistence. IMEM/loading and post-write quiescence checks are source-derived guards, not exhaustive hardware certification. Other profiles need new fixtures and an independently reviewed recovery path.
