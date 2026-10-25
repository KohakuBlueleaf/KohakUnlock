/* SPDX-License-Identifier: GPL-2.0-only */
/* SYNTHETIC profile for offline host tests ONLY. No card matches this
 * descriptor; it exercises profile-driven code paths with values that
 * differ from the Leadtek TU102 constants on purpose (mapper 0x2f0,
 * iface 0x30 word, IMEM 0x9000, min blocks 0x92, heap 105 MiB,
 * FRTS 0x17fe00000). Simulation only; never hardware evidence. */
#ifndef TU104_PROFILE_H
#define TU104_PROFILE_H

#define KOH_PROFILE_NAME "tu104-2060-6g-synthetic-test"
#define KOH_PCI_DEVICE 0x1e89u
#define KOH_PCI_SUBSYSTEM_VENDOR 0x1458u
#define KOH_PCI_SUBSYSTEM_DEVICE 0x3fc1u
#define KOH_TARGET_BUS 0x01
#define KOH_TARGET_BDF "0000:01:00.0"

#define KOH_FB_SIZE 0x180000000ULL
#define KOH_BOOT0_EXPECT 0x164000a1u

#define KOH_WORKSPACE_REG 0x1u
#define KOH_WPR2_LOW_INIT 0x1ffffe00u
#define KOH_WPR2_HIGH_INIT 0x0u
#define KOH_SELECTOR0_INIT 0x0u
#define KOH_SELECTOR1_INIT 0x0u
#define KOH_FEATURE_WORD 0x2f3u
#define KOH_READOUT_WORD 0x00400080u
#define KOH_GFW_WORD 0x3ffu

#define KOH_FWSEC_SIZE 0x9420u
#define KOH_FWSEC_SHA256 \
    "0000000000000000000000000000000000000000000000000000000000000000"
#define KOH_PROM_DESC_OFF 0x3f024u
#define KOH_PROM_FWSEC_OFF 0x3f060u
#define KOH_DESC_WORDS                                       \
    {0x3c0201u, 0x9420u, 0x9420u, 0x0u, 0xd8u, 0x0u, 0x9000u, \
     0x0u, 0x300u, 0x8d00u, 0x9000u, 0x0u, 0x420u, 0x9000u, 0x6600u}

#define KOH_IFACE_WORD 0x03080401u
#define KOH_MAPPER_OFF 0x2f0u
#define KOH_CMD_OFF 0x350u
#define KOH_CMD_SIZE 0x40u

#define KOH_FRTS (KOH_FB_SIZE - 0x200000ULL)
#define KOH_HEAP_MIB 105u

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
