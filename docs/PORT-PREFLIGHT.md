# Preparing a different TU102 card

The shipped executor and default diagnostic support the historical Leadtek profile.
A new PCI ID is a port, not an installation option. The diagnostic's allowlist
also excludes other cards. Do not edit the historical identity or firmware hash
just to pass those checks.

## File-only assessment

Use an existing inventory and keep the report private:

```sh
python3 tools/assess_profile.py --inventory local/inventory.json \
  --bdf 0000:01:00.0 --output local/port-assessment.json
```

If a complete native PROM has already been obtained with separately authorized
hardware access, add `--rom local/prom.bin`. Add `--assets local/assets` to check
existing private inputs against the historical asset manifest. The tool never
acquires firmware, accesses hardware, downloads assets or emits an executable
profile. Its `execution_allowed` and `hardware_validated` fields remain false
even when all file comparisons match. Successful CLI exit means a report was
written, not that the card is ready. Existing output files are not overwritten.

The ROM analysis records the native image hashes and descriptor words, FWSEC
interface/mapper/command offsets and sizes. Compare them with `tx_desc`,
the native staging checks in `tx_prepare()` and the per-profile constants in
`kernel/tu102_profile.h`. Compare the
profile's FWSEC hash with both the loaded input and native PROM guard; one must
not be substituted for the other.

## Evidence needed before preparing an executable port

| Evidence | Why it is needed |
| --- | --- |
| Native complete PROM, capture provenance and offline parser report | Establish actual FWSEC bytes and ABI, including extension images |
| Reviewed architecture dispatch | Confirm the capacity/layout path for the actual GPU |
| Pre-driver register snapshot, identified as cold or warm | Establish capacity, workspace, WPR2, engine, selector and guard assumptions |
| Independent console and full power control | Recover if firmware execution hangs the host or the early boot gate stops |
| Target-kernel override exclusion review | Preserve the transaction and retained-resource failure policy |
| Private fixtures and actual-core model failures | Check layout and cleanup without treating simulation as hardware evidence |

`tu102_diag` itself captures PROM through BAR0. It is not an ordinary metadata
query and its default target allowlist does not cover a new board. Expanding or
running it requires review of the new profile, explicit hardware-access scope
and verified recovery. Do not unbind a running display card to collect evidence.

The new device override implementation uses a spinlock rather than taking the
device mutex. Device binding still uses the device mutex, but an override writer
need not take it. The executor therefore requires the controlled pre-udev gate
on that branch, not simply a successful build or a live `execute=1` invocation.
The private gate must be first in init-top, exclude udev/GPU module loading, and
halt on failed or unverified handoff. `pre_udev=1` asserts this reviewed environment;
it is not a runtime check that can make an arbitrary live invocation safe.
Holding a spinlock across allocation or firmware execution is not a solution.
See PROFILE-NVIDIA-22G.md for the separately authorized target-specific trial.

The current `TX_EXPERIMENT_V4` guard evidence must cover the following. These
are historical expectations to review, not values to write into the new card:

| BAR0 offset | Historical expectation |
| --- | --- |
| `0x000000` | `0x162000a1` |
| `0x100ce0` | Capacity decodes to `TX_FB_SIZE` (22 GiB); historical raw value `0x2c9` |
| `0x409650` | `0xffffff8f` |
| `0x409664`, `0x40966c` | Selector pair exactly `0/0`, stable across repeated guards |
| `0x625f04` | `1` |
| `0x1fa824`, `0x1fa828` | `0x1ffffe00`, `0` |
| `0x118234` | Low 10 bits equal `0x3ff` |
| `0x409660`, `0x409668` | `0x2f3`, `0x00400080` |
| `0x110100`, `0x840100` | `0x10`, `0x10` |
| `0x33ed98` through next 15 words | Native descriptor exactly matches `tx_desc` |
| `0x33edd4` for `TX_FWSEC_SIZE` bytes | Native image exactly matches the authenticated input |

The historical diagnostic omits some executor readouts (`0x118234`,
`0x409660`, `0x409668`). Its log alone is not the full guard evidence. Native
PROM descriptor and image positions also must be derived from the new capture,
not assumed from the offsets in this table.

A separate diagnostic-only candidate can be compiled with:

```sh
make module DIAG_PROFILE=nvidia-1e07-12a4
```

This selects only `0000:01:00.0`, PCI `10de:1e07`, subsystem `10de:12a4` in
`tu102_diag`, and includes the three additional readouts above. The flag applies
only to the diagnostic object. The executor remains the historical profile,
with hardware execution disabled. The default `DIAG_PROFILE=historical` retains
the original diagnostic identities. Unknown profile names fail compilation.
This diagnostic has also produced one read-only pre-driver capture on its
intended owner-reported TU102-300A board; cold/warm provenance remains unknown. It
refuses driver-bound devices and does not enable PCI memory decoding. Do not
run it against a live bound GPU. Compilation does not install or load it.

For a separately authorized diagnostic boot, `initramfs-diag-hook.example`
packages only the reviewed diagnostic module, while
`initramfs-diag-capture.example` requires the exact
`kohak.diag=read-only-1e07-12a4` boot token. Use a private initramfs config and
make udev explicitly depend on the capture script; inspect the generated
`init-top/ORDER` rather than relying on file names. The script checks the exact
identity and unbound state, verifies the packaged module hash, captures the PROM
and register logs into `/run/kohak-tu102-capture`, then permits ordinary boot.
It performs no unlock. Capture failure is recorded and does not claim success.
The `/run` output must be saved privately before another reboot. Initial power
state still needs a user-supplied cold/warm provenance note.

The resulting [22 GiB offline candidate](PROFILE-NVIDIA-22G.md) has its own native
FWSEC layout and exact observed selector tuple. It is host-model-only and is
rejected in kernel builds. It does not select a new executor profile.

## Final application boundary

After those prerequisites, review a target-specific diagnostic and executor,
run private model tests, compile for the exact kernel, and prepare a separate
one-shot initramfs. Inspect its module/asset hashes and pre-udev hook order.
Keep the normal image and default boot intact. Stage a target-specific boot
entry only with separately authorized boot-setting changes and recovery access.
Do not copy the historical entry's BDF or another host's root arguments.

At the final review, distinguish the source diff, compiled module, installed
module, selected boot image and executed firmware. These are separate states.
SSH is useful while Linux and networking are healthy; it cannot recover a host
halted before networking or blocked in PCI/MMIO. The project performs a volatile
firmware transaction, not a VBIOS flash, but that does not guarantee recovery.
