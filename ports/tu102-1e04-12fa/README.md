# TU102 port — RTX 2080 Ti `10de:1e04` / subsystem `10de:12fa`

Experimental reproduction of the volatile FP16→FP32-accumulate tensor-compute
unlock on a **different** 22 GiB VRAM-modded 2080 Ti board than the ones already
in the tree. Read [docs/SAFETY.md](../../docs/SAFETY.md) and
[docs/TURING.md](../../docs/TURING.md) first. No binary firmware, ROM dump, GPU
UUID or host data is included here — only source, a profile with hashes, and a
benchmark helper.

Related work: PR #4 (ChenYFan) refactors the tree to be profile-driven and adds
the `10de:1e07` / `12a4` TU102-300A board. This port is an **independent**
bring-up of the `10de:1e04` / `12fa` board, done against the pre-refactor
single-profile `kernel/` on kernel 7.0.0-38. If #4's profile framework lands,
this board collapses to a single profile JSON; until then these are the exact
source deltas that made it work.

## This board vs the in-tree Leadtek profile

Same descriptor family (magic `0x3c0201`), different native FWSEC size and
internal layout. Everything below was parsed from our own PROM with
`tools/rom_analyze.py`; the identity was read pre-driver with the diagnostic.

| field | Leadtek (`107d:1e04`, in-tree) | this board (`10de:1e04` / `12fa`) |
| --- | --- | --- |
| native FWSEC size | `0x9c20` (39968) | `0x9a10` (39440), sha256 `9e93d134…` |
| interface offset (in DMEM) | `0xe0` | `0xdc` |
| IMEM / DMEM offset | `0x9800` | `0x9600` |
| secure-IMEM base / size | `0x400` / `0x9400` | `0x300` / `0x9300` |
| DMEM size | `0x420` | `0x410` |
| mapper / command (in DMEM) | `0x390` / `0x3e0` | `0x380` / `0x3d0` |
| PROM descriptor / image | `0x33ed98` / `0x33edd4` | `0x33f024` / `0x33f060` |
| min IMEM blocks | `0x9a` | `0x98` |
| VBIOS | `90.02.33.00.3F` | `90.02.17.00.DB` |

All other guard registers matched the in-tree V4 profile exactly: BOOT0
`0x162000a1`, capacity `0x100ce0 = 0x2c9` (22 GiB), workspace `0x625f04 = 1`,
WPR2 `0x1ffffe00/0`, FEAT PLM `0xffffff8f`, both engine CPUCTLs halted (`0x10`).

### Selector baseline varies per cold boot

`SS0/SS1` (`0x409664` / `0x40966c`) are **not** stable across cold boots on this
board — observed `0x0/0x0`, `0x20/0x4`, `0x4000/0x0`, and `0x4000000/0x4`, all
clearly pre-unlock (the full-rate target is `0x88888888/0x8`). Enumerating exact
pairs is a losing game here (a fresh value shows up and that card silently stays
locked for the boot), so `tx_observed_pair` accepts **any** pre-unlock state and
refuses only the already-unlocked `0x88888888/0x8` target. The other guards
(BOOT0, capacity, workspace, WPR2, PLM, both CPUCTLs halted, full PROM descriptor
+ FWSEC byte-match) already establish the cold pre-boot state, so this stays
safe. Worth noting for the profile-driven design — a pinned selector pair is not
viable for this board.

## Source deltas vs `kernel/`

Pure constant/identity swap from the parsed descriptor, plus kernel-7.0 API
fixes. See [kernel/](kernel/):

- `tu102_core_port.h`: `tx_desc[15]`, `TX_FWSEC_SIZE`, the mapper/interface guard
  offsets, command-injection offsets, the FWSEC bootloader descriptor fields in
  `tx_run`, IMEM-block minimum, and the PROM descriptor/image addresses — all
  swapped to the values above; `tx_observed_pair` set to the locked family.
- `tu102_execute_main.c` and `tu102_dma.c`: subsystem identity `107d:1e04` →
  `10de:12fa` (**both** files carry the check — the second one in `tu102_dma.c`
  is easy to miss and returns `-ENODEV` after guards pass).
- Kernel 7.0.0 build fixes: `driver_override` moved off `struct pci_dev` →
  `device_set_driver_override()` / `device_has_driver_override()`; add
  `#include <linux/hex.h>` (hex2bin) and `#include <linux/sprintf.h>` (sscanf);
  the target BDF is parsed from the module param instead of hardcoded; a no-op
  `module_exit` is added so a boot-time gate can `rmmod` between cards.

## Bring-up / capture on a dracut system

This box uses **dracut** (`update-initramfs` is a dracut shim); `initramfs-tools`
hooks are ignored. The runtime capture tricks in the docs do not apply as-is:

- `modprobe.blacklist=nvidia` does **not** stop the load — NVIDIA's udev rule
  calls `nvidia-modprobe`, which `insmod`s directly and bypasses the blacklist.
- A runtime `nvidia` unbind **hangs** (the headless framebuffer holds the device
  with a non-zero usage count and wedges the card until reboot).

So both the read-only diagnostic and the executor must run **pre-driver**. The
[dracut/](dracut/) module (`99tu102`) installs the `.ko`s + firmware and runs a
`pre-trigger` hook gated on a kernel cmdline flag, so the default boot is
unaffected:

- `tu102_go=diag:N` — read-only snapshot of card on bus `N` (1 or 2)
- `tu102_go=diag:all` — snapshot both
- `tu102_go=exec:N` — one mutating unlock attempt on card `N`
- `tu102_go=exec:all` — unlock both (insmod → rmmod → insmod)

The gate here intentionally does **not** drop to the SAFETY recovery shell on an
unsafe handoff; it relies on the retained `driver_override` to keep NVIDIA off a
mutated card while the rest of the box (and SSH) comes up for inspection. That is
a deliberate trade for a remote, headless box — review `docs/SAFETY.md` and
restore the halting gate if you do not have that trade's assumptions.

## Results (cuBLAS `GemmEx`, 8192³, [tools/gemmbench.cu](tools/gemmbench.cu))

Same physical card, one session, FP16→FP32 accumulate vs an FP16→FP16 control:

| condition | FP16→FP32acc | FP16→FP16 control |
| --- | --- | --- |
| locked, 200 W | 60.4 | 100.2 |
| locked, 280 W | 61.3 | 112.1 |
| unlocked, 200 W | 94.8 | 102.1 |
| unlocked, 280 W (max) | 106.5 | 114.5 |

The control is flat across lock state; the FP16→FP32 cap is removed (from ~½ the
control to ~parity). The cap is architectural, not power/thermal: locked stays
~61 TFLOPS at both 200 W and 280 W while the control scales with power. Same-power
A/B at 280 W is 61.3 → 106.5 (+74%). `GemmEx` includes layout/memory overhead, so
these sit below an instruction-peak (`mmapeak`) number. Clocks not locked; 280 W
is the card's own max power limit (not persisted). Both cards on the host unlock
and pass a two-GPU workload.

## Not established

Same caveats as the rest of the repo: long-duration stability, deliberate
post-write failure recovery, numerical certification beyond healthy-driver GEMM
checks, and persistence design. The volatile unlock is re-applied every boot by
the gate; a guard miss on a given boot simply leaves that card locked.
