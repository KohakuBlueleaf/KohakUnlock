# Engineering handoff for agents and maintainers

## Non-negotiable scope

A user request to investigate is not permission to reset, unbind, install boot hooks, execute firmware, reboot, flash or publish private dumps. State the next action's boundary. Never call preparation success an unlock. Separate observed results from calculations and simulated assertions.

This project changed volatile state, not VBIOS or fuses. Reversibility is a design intent, not a guarantee. On uncertain post-write failure, retained DMA plus blocked driver matching and pre-udev stop are mandatory. Do not turn a safety refusal into success by dropping a check.

## Ada and later Turing work

For Windows/RTX4090, start with [ADA-AGENT-GUIDE.md](ADA-AGENT-GUIDE.md):
local ROM extraction, NVGI/PCI/BIT/FWSEC parsing, pinned asset generation,
profile adaptation, UEFI ownership, builds/tests, USB handoff and signing.
[ADA-WINDOWS.md](ADA-WINDOWS.md) records the successful manual boot and
same-binary randomized-mmapeak results. [DUAL-TURING.md](DUAL-TURING.md)
records the later two-card unlock and NCCL NV2 check. Historical single-card
sections below describe the original Turing bring-up; the public TU102 executor
still uses that original single-target profile.

## Repository map

- kernel/tu102_core_port.h: adaptation of upstream injected-I/O transaction core. Native Leadtek descriptor and layout pinned; reset/load/start/verify/cleanup sequence.
- kernel/tu102_execute_main.c: exact target, firmware hashes, device lock/driver_override, BAR ownership, CPU shadow, coherent buffers, bounded callbacks, lifetime/handoff.
- kernel/tu102_identity.h: shared exact PCI/subsystem identity for executor and DMA helper; propagate target flags to both translation units and check both module profile tags before packaging.
- kernel/tu102_dma.c: bounded coherent chunks, masks and restoration; blocks retained if mutation is not quiescent.
- kernel/tu102_diag.c: read-only unbound-device snapshot. No firmware execution. BDF allowlist remains deliberately narrow.
- kernel/tu102_pack.c: preparation-stage metadata/radix implementation and verification; tested during development, NOT linked into the final executor. Keep its distinction from the execution core explicit.
- kernel/tu102_flow.h: tested sequencing policy with mock callbacks, NOT the actual runtime executor. The executor uses tx_run and its own cleanup logic.
- tests/test_core_port.c: adapted protocol/register-bank model, private assets required.
- tests/test_dma_host.c, test_pack_host.c, test_flow_host.c: firmware-free injected API/model checks.
- tools: read-only OS inventory, file-only ROM parsing, input hash audit, boot templates.

## Investigate in this order

1. Query OS metadata; record exact PCI/subsystem IDs, VBIOS, capacity, kernel/driver and display topology. Record whether a snapshot is post-driver, pre-driver after warm reboot, or after full power-off. Check IOMMU group membership, not just target BDF.
2. Acquire native firmware with explicit hardware-access scope. PCI ROM export may stop at the standard image chain; private NVIDIA extension images can be beyond it. Full PROM reads and option-ROM exports are different. Handle partial reads. Do not trust marketing name/version equality as byte identity.
3. Parse ROM structure and firmware descriptor from bytes: image signatures/PCIR/NPDE, BIT checksums/token sizes, ucode table and native FWSEC. Bound every offset/length. Reject malformed/truncated images. Preserve original bytes and hashes.
4. Follow NVIDIA HAL dispatch for the actual architecture, not names found in a neighboring implementation. TU102's usable-FB path dispatches to GP102 and reads 0x100ce0. Ampere's 0x1183a4 returned BADF here. The TU102 reference MMU-lock implementation returns invalid/not-applicable; reading GA100-related registers was unhelpful.
5. Build an offline layout using actual capacity/workspace, declared image sizes, reservations, alignment and firmware ABI. 22 GiB is NOT a global replacement for an 11 GiB constant. Published ASUS FWSEC was 39440 bytes; our Leadtek FWSEC was 39968 with different DMEM/interface/mapper offsets.
6. Test allocation and cleanup without execution. A single 32 MiB coherent allocation exceeded this kernel's direct allocator order limit. We used 512 x 64 KiB blocks instead. DMA address != CPU physical address. Validate every mapping. The >4 GiB lower bound inherited from the published profile is conservative and may refuse otherwise valid IOMMU mappings; do not bypass it without evidence.
7. Construct firmware inputs in a CPU shadow; each hardware-consumed pointer must use the block's device DMA address. FWSEC/Booter/signature/boot image each fit within their own chunk; radix data points across independent chunks. Never do base+logical_offset for the whole pool.
8. Verify copies/metadata/radix before publishing any pointer or starting an engine. Preparation-stage zero-filled signature/data regions are not executable packages. The real executor checks and stages the actual pinned assets.
9. Simulate phase/I/O failures, alias CPU start, guard mutations, timeouts, command/mask restoration and partial allocation. Tests must target actual implementation code, not a separately reimplemented happy path. Tests do not prove hardware authentication or DMA quiescence.
10. Review independently; build the exact kernel image; use one-shot target-specific boot. Verify image hashes and hook order. Do not reset the whole bus when GPUs share a group/bridge.
11. After boot, check logs AND performance AND correctness. Do not infer unlock from an innocuous return code. Preserve counterfactual baseline and raw inputs/settings privately. Record whether comparison is same-card or different-card.

## Actual changes from upstream Turing sample

Native descriptor: 0x3c0201; stored/uncompressed 0x9c20; interface 0xe0; IMEM 0x9800; secure IMEM base/size 0x400/0x9400; DMEM offset/size 0x9800/0x420. Native descriptor/image positions 0x3ed98/0x3edd4 in PROM. Mapper/command offsets 0x390/0x3e0; init command field mapper+44. These describe this captured image, not all TU102 firmware.

Observed profile: BOOT0 0x162000a1; capacity register 0x2c9; workspace 1; WPR2 low/high 0x1ffffe00/0; GSP/SEC2 HALTED; selectors 0/0. The executor also checks GFW/readouts against its profile before mutation. Successful native FWSEC produced WPR low/high 0x57fe000/0x57fee00. These are empirical evidence, not general constants.

The larger native image needs at least 0x9a IMEM blocks to fit the two-block generic loader without overlap. A review caught the inherited 0x98 minimum. Added failing tests before fixing it.

The Linux wrapper holds the device lock, rejects existing bindings/overrides, installs a temporary unmatched driver_override and owns BAR0. On uncertain failure it retains override/backing and stops boot. Restores PCI command and DMA masks BEFORE releasing backing. No unloading callback, no retry/reset fallback, no live display unbind. The native firmware hash is checked both on disk and through PROM guards. No flash is performed.

## Failure-path evidence versus proof

Host suite covers 462 historical and 528 candidate protocol cases and 104 sequencing-policy combinations; policy is not wired as the executor. The real DMA helper is tested under both exact identity profiles, each with 7 failure/lifetime scenarios and 5 identity rejection cases before side effects. A mixed-profile compiled module must be rejected before packaging. Diagnostic/preparation boots verified kernel allocation, identity, real noncontiguous addresses and cleanup after injected pre-execution abort on the historical board. Actual unlock succeeded once on that board. The first NVIDIA-profile attempt passed initial guards but stopped at a stale DMA identity check before allocation or firmware launch; the second refused the initial selector pair before DMA. Neither is unlock evidence. Rejected-pair tests verify both raw words are reported, including a matching word and callback failure, before any write. Post-launch failure recovery has not been deliberately induced on hardware. Document this limitation prominently.

The candidate accepts exactly two observed initial pairs, 0x00500060/5 and
0x20500060/5. The selected pair is pinned across preparation, so a transition
between them still refuses. Tests independently enumerate both pairs and one-bit
mutations. This is a bounded allowlist, not a bit-29 decode or repeatability
claim.

## Benchmark mistakes to avoid

- CUDA_VISIBLE_DEVICES with a PCI address string selected the wrong GPU in an early Ampere measurement. Use full GPU UUID and assert PyTorch's actual UUID; keep UUIDs private when publishing.
- mmapeak tests issue throughput; PyTorch GEMM includes layout/memory/power bottlenecks. Compare the same instruction/accumulation mode.
- Turing has no native BF16/TF32 tensor path. FP16→FP32 is the relevant target; FP16→FP16 is the control.
- Do not silently replace an original benchmark with long calibrated samples, per-shape GC, correctness copies and telemetry subprocesses, then call numbers comparable. Presentation changes and methodology changes are separate tasks.
- Our earlier Ampere baseline targeted another GPU, but telemetry and a subsequent identity check recovered its attribution to 01:00.0. A later UUID-targeted measurement gives a same-card comparison with different power/cooling conditions; see RESULTS.md. Do not discard recoverable measurements or hide their confounders. MAMF Turing baseline uses the untouched same-model card: distinguish it from same-card mmapeak A/B.

## Publishing

Never upload private ROMs/PROMs, firmware/payload binaries, decrypted images, source credentials, serials, GPU UUIDs, filesystem UUIDs, host/IP/user inventory or compiled modules. Retain upstream notices and exact source revisions. Public source availability does not settle firmware redistribution rights. Report summaries as summaries; do not invent missing raw logs or numerical validation. Update license provenance when copying/adapting code. Default public build must not execute hardware.

## Next work

Independent repeatability/correctness/endurance testing, a second TU102 card only after its own pre-driver profile validation, stronger hardware quiescence evidence, generic profile generation with signed-off gates, and a nonrecursive persistent boot design. Do not enable persistence by merely choosing the experimental entry as default.


