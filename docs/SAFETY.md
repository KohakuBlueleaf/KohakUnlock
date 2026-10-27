# Safety and explicit boundaries

This project executes privileged GPU firmware in an experimental sequence. No warranty or guarantee of reversibility is made. Firmware bugs, DMA errors and reset failures can affect the whole host, not just the target card. Test with backups, no valuable active jobs, local/BMC console and full power control.

## Defaults

- Public kernel execution build is disabled unless compiled with ENABLE_EXPERIMENT=1.
- The loader further requires execute=1 and the exact historical target BDF.
- Identity, capacity, native firmware and external assets are checked, not guessed.
- Existing device binding causes refusal; never unbind a working display GPU to bypass this.
- No VBIOS flashing, fuse programming, bus-wide reset, power-limit modification or overclock is implemented.
- No module unload path exists for the executor. Reboot is the lifecycle boundary.
- No automatic persistence installer exists. First attempt must be a one-time boot with a separate known-good default.

## Failure model

Before mutation, release owned resources and leave the device available if restoration succeeds. Once writes may have reached the GPU, an unverified result retains DMA backing/BAR ownership and a driver_override string that prevents NVIDIA matching this device. The boot hook MUST check handoff_safe and halt before udev/NVIDIA if it is not Y.

Do not delete the driver_override, unload the module, free memory, force-bind NVIDIA or continue from the failure shell. Capture the console, then fully power off/on into the known-good entry. SSH cannot recover a host stopped before networking starts. A warm reboot may not reset all GPU state. Do not automatically retry an uncertain hardware mutation.

Timeouts bound software polling but cannot guarantee recovery from an unresponsive PCI/MMIO transaction. A successful host simulation is not evidence of hardware quiescence. A successful unlock boot is not long-term stability certification.

## Profiles

The shipped code still pins BDF 42:00.0, Leadtek subsystem IDs, exact native firmware hash/descriptor, 22 GiB capacity, workspace=1, and observed selectors 0/0. Same marketing name or VBIOS version is insufficient. The other 22 GiB card was a control, not independently unlock-tested.

The read-only diagnostic also has a narrow target allowlist (42:00.0 or 43:00.0), refuses driver-bound devices and will not turn on PCI memory decoding. MMIO reads can still have platform-specific risks; review register definitions before expanding them.

An explicitly selected NVIDIA 1e07/12a4 trial profile is now available separately
from that historical default. It pins 01:00.0, the measured native FWSEC and exact
selector pairs, requires ENABLE_EXPERIMENT=1 and a controlled pre-udev gate, and
has one successful boot with sustained GEMM checks. See PROFILE-NVIDIA-22G.md.
This does not establish repeatability or long-term stability. Generic/offline
profile checks never grant permission to execute it. The trial's phase journal
is fsynced to a private log directory on an existing FAT volume; early mounting
must not bypass normal root-filesystem checks. Failure stops before udev.

## Security settings

Do not copy the original host's unrelated iomem/ACS/IOMMU flags as prerequisites. They are not established requirements. Do not disable IOMMU or Secure Boot casually. An unsigned module needs appropriate local signing or an explicitly accepted Secure Boot configuration. Changing boot/security settings may trigger disk-encryption recovery; retain recovery keys first.
