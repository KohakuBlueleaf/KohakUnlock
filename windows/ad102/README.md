# AD102 Windows UEFI source

GPL-2.0-only application derived from TOTORONG/AD103_win_unlocker revision
51df9c17ec89eb8486df52cc9079dde4bd1f9974. Component licenses are retained in
source/LICENSES and source/deps/edk2/License.txt; upstream notices accompany it.

Target: RTX4090 10de:2684 / 10de:167c, VBIOS 95.02.18.80.EF, 24GiB,
ASUS W890E-SAGE SE BIOS0402 with two Arc Pro B50 display devices.
Adaptations cover workstation enumeration/BARs, inactive GOP children, full
preflight logging, strict selectors and effective readout, and Secure Boot policy.

## Build

Python3, clang, lld-link:

```
python build.py --clang /path/to/clang --linker /path/to/lld-link
```

Default EFI returns EFI_UNSUPPORTED before hardware access. For an execution build,
supply the reviewed private Windows ESP GUID initializer in
source/unlock/local_profile.h, review the exact board profile, and add
--enable-experiment. Output: source/build/BOOTX64.EFI. Signing/deployment are
separate manual steps. Firmware hashes and names are pinned in app_io.c.

Public source differs from the tested local binary by the execution gate and
private-GUID abstraction; provenance.json records imported source hashes.
CPU-only private-fixture runner:

```
python test_host.py --prom /private/normalized-prom.bin --assets /private/assets --pci-config /private/pci-config.bin
```

Retained source/unlock/tests uses simulated EFI/PCI/MMIO/DMA with privately supplied
firmware, ROM-derived PROM and PCI fixtures. Historical validation and fixture
requirements are detailed in the agent handoff.

[Results](../../docs/ADA-WINDOWS.md) · [Agent handoff](../../docs/ADA-AGENT-GUIDE.md) ·
[Provenance](../../NOTICE.md)
