# Agent handoff: Ada Windows UEFI porting

Read [ADA-WINDOWS.md](ADA-WINDOWS.md), windows/ad102/source and provenance.json.
This reference is not authorization to erase disks, change keys, reboot or execute
firmware. Separate offline work, preflight, actual firmware execution and verification.

## Inventory FIRST

Record device/subsystem IDs, revision, VBIOS, VRAM, motherboard BIOS, display GPUs,
UEFI mode and Windows ESP identity. Use nvidia-smi, Win32_BaseBoard, Win32_BIOS,
Win32_VideoController and Get-Partition. Keep UUIDs/GUIDs and paths private.
UEFI returned 317 PCI handles on our workstation: enumerate its allocated buffer,
not a desktop-sized fixed count. Windows and UEFI device counts need not match.

## Extract and parse the local ROM

Windows GPU-Z Save BIOS or NVFlash `--save card.rom` can save the ROM. No flashing,
protectoff, reset or driver unbinding is part of this step. WSL is for offline tools,
not ordinary direct access to the physical PCI ROM. Hash and preserve original bytes.

Our 2,048,000-byte dump begins NVGI, not 55aa. Expansion views at 0x9400 and
0xe9400 were identical over 0xd6c00 bytes. Legacy+UEFI PCI image length was 0x24a00.
Use bounds-checked PCIR/NPDS/NPDE chains and BIT parsing; BIT pointers are relative
to the normalized ROM with extension adjustment. Finding a signature alone is not
an executable image parser.

File-only parser: source/unlock/vendor/vbios_610.c. Our FWSEC geometry:
image0x44168 size0x10000, IMEM0xf200, DMEM0xd80, PKC0xb24, interface0x1c,
engine0x400, ucode9, signatures2, versions3, signature offset0x43e68 size0x300.
These are this ROM's values, not universal Ada constants. Live BAR0+0x300000 must
match the normalized view hash before execution; a GPU-Z container hash is not a
live PROM hash. Label padded offline PROM fixtures SYNTHETIC, not captured.

## Firmware assets

Pin gpu-unlock-ada revision4e7fefcc4923f9e083d5324dc75a7d2f906af77f.
Use firmware/fetch_artifacts.py, generate_v67.py and extract_boot_assets.py --chip ad102.
Verify the pinned NVIDIA610.43.03 archives before generation. app_io.c lists the
five exact files, lengths and SHA256s: production booter, V67 four-PLM payload,
GSP-RM, GSP boot and descriptor. Do not mix versions or substitute newer firmware.
No firmware or ROM bytes are published here.

## Which source values need adaptation?

- port.h: exact GPU/subsystem/chip/BDF; verify each independently.
- prom_profile.h and app.c PromCheck: normalized live-view and PCI-image hashes.
- ColdCheck: GFW/WPR2 state, accessible selectors and measured VRAM. Layout uses
  exact capacity; never round away the firmware reservation.
- BAR/config: validate firmware-assigned descriptors and PCI capabilities, then
  snapshot/recheck. Do not copy another motherboard's physical BAR addresses.
- local_profile.h: private reviewed Windows ESP GUID initializer; never publish it.
- ownership.c: driver binding, child owner and parent PCI ancestry. Only active
  framebuffers need geometry. Inactive/no-GOP children still need valid ancestry.
- Independent display handling currently permits Arc Pro B50; another device
  requires review, not blanket removal of display checks.

Determine whether a refusal represents an actual prerequisite or a historical
observation. Log all children and all independent preflight failures in one boot.
Do not perform dependent MMIO after failed BAR validation. After firmware mutation,
unknown DMA/execution state cannot become safe just by logging and proceeding.

## Build/tests

`python windows/ad102/build.py --clang /path/to/clang --linker /path/to/lld-link`
builds an inert EFI by default. After reviewing the exact profile and supplying
local ESP GUID, --enable-experiment enables the transaction. Build never installs,
signs or executes anything. Public source is not byte-identical to tested EFI.

Retained source/unlock/tests uses fake EFI/PCI/MMIO/DMA. Private firmware and a
1MiB synthetic PROM window derived from the ROM are required. Application tests
also need a reviewed synthetic PCI config fixture. None of those binaries ship.
The tests use local_profile.h for the mocked ESP too, so zero is valid in simulation.
Historical local tests:43 core +50 application +7 policy/topology cases, including
317/4096 handles with target last, inactive children, all-child logging despite a
failure, Secure Boot mode checks, and RUN02 PCI settings through FLR/handoff.
These are simulations, not proof of firmware authentication or hardware quiescence.
PE build checks: AMD64 EFI application, relocations, no imports/TLS/CLR. Retain hashes.

## Manual trial

FAT32 USB: EFI/BOOT/BOOTX64.EFI and RTX4090-UNLOCK/ASSETS. Keep internal Windows
ESP/BCD/BootOrder unchanged. Cold boot USB; physical keyboard, target-GPU monitor;
manual U at READY. Success requires firmware marker/HALT, required PLMs, both
selectors, one FLR, zero effective readout, DMA cleanup, GOP recovery and actual
Windows startup. Cleanup alone is not unlock. FBPA is upstream-known nonessential;
WPR_CFG/WPR/FEAT and selectors remain mandatory. No retry after uncertain mutation.
Failure recovery: full power-off, bypass USB and select stock Windows.

Secure Boot off works. For enabled Secure Boot: locally sign, verify signature and
unchanged executable content, copy PUBLIC CER/ESL only, append cert to firmware db.
Do not replace/clear PK/KEK/dbx; shim MOK is not db. Our successful RUN03 used Secure
Boot OFF. Cryptographic signing is not proof of firmware trust or execution.

## Benchmark/publication

Use [our mmapeak fork](https://github.com/KohakuBlueleaf/mmapeak) at the revision in
ADA-WINDOWS.md. Original compile-time zeros allowed tile folding while full FLOPs
were counted in inspected builds. Runtime randomness fixes that observed issue.
Use the SAME executable hash/args for before/after, record identity/driver/power/
telemetry and keep controls/reruns. Do not relabel historical original-mmapeak
Turing values as randomized measurements. Instruction rate is not application speed.

Publish source, hashes, independent prose and sanitized measurements. Exclude
ROM/PROM, firmware/payload binaries, keys/certs, compiled EFI, disk GUIDs and raw
host logs. Preserve GPL and per-component notices. Upstream CC-BY-NC narrative
licensing does not relicense code or firmware. See windows/ad102/README.md.
