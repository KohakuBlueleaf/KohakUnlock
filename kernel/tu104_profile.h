/* SPDX-License-Identifier: GPL-2.0-only */
/* TU104 RTX 2060 (10de:1e89 / 1458:3fc1, 6 GiB, rev a1, BDF 0000:01:00.0)
 * profile for the KohakUnlock TU104 port. TEMPLATE/PLACEHOLDER state:
 * every value marked OBSERVE must be replaced by a pre-driver observation
 * from kernel/tu104_diag.ko and a rom_analyze.py run on THIS card's own
 * full 1 MiB PROM capture, then regenerated with tools/profile_gen.py.
 * Placeholder values are guaranteed to fail closed: asset verification,
 * the PROM descriptor guard and the mapper guard all refuse before any
 * register write. Never hand-edit hashes; regenerate instead.
 * Empirical provenance of family-shared values: docs/TU104-2060.md.
 */
#ifndef TU104_PROFILE_H
#define TU104_PROFILE_H

#define KOH_PROFILE_NAME "tu104-2060-6g-unreviewed"

/* Card identity (from tools/inventory.py; OBSERVE subsystem on your host). */
#define KOH_PCI_DEVICE 0x1e89u
#define KOH_PCI_SUBSYSTEM_VENDOR 0x1458u
#define KOH_PCI_SUBSYSTEM_DEVICE 0x3fc1u
#define KOH_TARGET_BUS 0x01
#define KOH_TARGET_BDF "0000:01:00.0"

/* Framebuffer. Placeholder decode target: 6 GiB. OBSERVE 0x100ce0 pre-driver
 * and let tools/profile_gen.py derive this from the observed register. */
#define KOH_FB_SIZE 0x180000000ULL

/* PMC BOOT0: TU104 chip id 0x164, revision a1. OBSERVE (offset 0x0). */
#define KOH_BOOT0_EXPECT 0x164000a1u

/* OBSERVE pre-driver: workspace 0x625f04, WPR2 0x1fa824/0x1fa828,
 * selectors 0x409664/0x40966c, feature 0x409660, readout 0x409668,
 * GFW 0x118234. These Leadtek TU102 values are UNPROVEN on TU104. */
#define KOH_WORKSPACE_REG 0x1u
#define KOH_WPR2_LOW_INIT 0x1ffffe00u
#define KOH_WPR2_HIGH_INIT 0x0u
#define KOH_SELECTOR0_INIT 0x0u
#define KOH_SELECTOR1_INIT 0x0u
#define KOH_FEATURE_WORD 0x2f3u
#define KOH_READOUT_WORD 0x00400080u
#define KOH_GFW_WORD 0x3ffu

/* Native FWSEC, from THIS card's PROM via tools/rom_analyze.py.
 * Placeholder: nothing matches a real PROM, so execution refuses. */
#define KOH_FWSEC_SIZE 0x0u
#define KOH_FWSEC_SHA256 \
    "0000000000000000000000000000000000000000000000000000000000000000"
#define KOH_PROM_DESC_OFF 0x0u
#define KOH_PROM_FWSEC_OFF 0x0u
#define KOH_DESC_WORDS                                       \
    {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u,    \
     0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u}

/* FWSEC DMEM mapper interface, from the same parsed image. */
#define KOH_IFACE_WORD 0x0u /* packed v1 header, e.g. 0x02080401 (v1,4,8,count2) */
#define KOH_MAPPER_OFF 0x0u /* e.g. 0x390 */
#define KOH_CMD_OFF 0x0u    /* e.g. 0x3e0 */
#define KOH_CMD_SIZE 0x0u   /* e.g. 0x40 */

/* FRTS = FB - 2 MiB (empirically identical for TU102 22 GiB and TU106 8 GiB;
 * profile_gen.py derives and asserts the relation). */
#define KOH_FRTS (KOH_FB_SIZE - 0x200000ULL)

/* GSP-RM firmware heap: TU10X base 8 MiB + ceil(96 KiB/GiB) + 96 MiB clients
 * (open-gpu-kernel-modules gsp_fw_heap.h). profile_gen.py computes. */
#define KOH_HEAP_MIB 0u

/* Pinned external blobs. Same blobs served the CMP50HX (TU102) and CMP40HX
 * (TU106) flows; TU104 compatibility is NOT established — treat as research.
 * Sizes and sha256s are the published CMP-origin pins; do not hand-edit. */
#define KOH_GENERIC_SIZE 0x300u
#define KOH_BOOTER_SIZE 0xe700u
#define KOH_SIGNATURE_SIZE 0xfa00u
#define KOH_BOOT_CONTAINER_SIZE 0x20000u
#define KOH_BOOT_SIZE 0x6000u
#define KOH_ELF_SIZE 0x1bfadc8u

#define KOH_GENERIC_SHA256 \
    "f21f1cfbb8fffa8067685314367fc4c50f3f84dd99eda7c19db4292d9269b9d9"
#define KOH_BOOTER_SHA256 \
    "e0f0fc93da097b5b68e5af3adb58b2235ed1fad5fce0b23592d3895af44cb743"
#define KOH_SIGNATURE_SHA256 \
    "de7288bcfb80c2d4b259a59e823c926ba0a9adcab594219cc6802c94257ce271"
#define KOH_BOOT_SHA256 \
    "fa43239bcee7b97ca62f007cc68487560a39e19f74f3dde7486db3f98df8e471"

#endif
