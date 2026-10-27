/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU102_PROFILE_H
#define TU102_PROFILE_H
#include "tu102_identity.h"

/* The model flag alone cannot enable a kernel trial. */
#ifdef TX_PROFILE_NVIDIA_1E07_12A4
#define TX_NATIVE_SHA256 "1516eb686e3b4a379f9c5739720b4ebf938700c2b5bf65686006ccd3999e2227"
#ifndef TX_EXPERIMENT_V4
#error "The NVIDIA candidate requires the reviewed V4 guard path"
#endif
#define TX_FWSEC_SIZE 0x9a10u
#define TX_NATIVE_NAME "native-fwsec.bin"
#define TX_NATIVE_DESC_OFF 0x3f200u
#define TX_NATIVE_IMAGE_OFF 0x3f23cu
#define TX_NATIVE_INTERFACE_OFF 0xdcu
#define TX_NATIVE_DMEM_OFF 0x9600u
#define TX_NATIVE_MAPPER_OFF 0x380u
#define TX_NATIVE_COMMAND_OFF 0x3d0u
#define TX_INITIAL_SS0 0x00500060u
#define TX_INITIAL_SS1 5u
/* Owner-authorized second exact tuple, observed in five pre-driver samples.
 * Bit 29 is not decoded; this is not a wildcard or a hardware success claim. */
#define TX_INITIAL_SS0_ALT 0x20500060u
#define TX_SELECTOR_PROFILE "nvidia-1e07-12a4-observed-pairs-v2"
#define TX_NATIVE_DESC_WORDS {0x3c0201,0x9a10,0x9a10,0,0xdc,0,0x9600,0, \
                             0x300,0x9300,0x9600,0,0x410,0x9600,0x6618}
#else
#define TX_NATIVE_SHA256 "c6fa28fa2458f7bce2aeb422a2e57cce3885ff65c779e1077f682c23d097a635"
#define TX_FWSEC_SIZE 0x9c20u
#define TX_NATIVE_NAME "leadtek-fwsec.bin"
#define TX_NATIVE_DESC_OFF 0x3ed98u
#define TX_NATIVE_IMAGE_OFF 0x3edd4u
#define TX_NATIVE_INTERFACE_OFF 0xe0u
#define TX_NATIVE_DMEM_OFF 0x9800u
#define TX_NATIVE_MAPPER_OFF 0x390u
#define TX_NATIVE_COMMAND_OFF 0x3e0u
#define TX_INITIAL_SS0 0u
#define TX_INITIAL_SS1 0u
#define TX_SELECTOR_PROFILE "leadtek-observed-pair-v1"
#define TX_NATIVE_DESC_WORDS {0x3c0201,0x9c20,0x9c20,0,0xe0,0,0x9800,0, \
                             0x400,0x9400,0x9800,0,0x420,0x9800,0x6638}
#endif

/* The two-block generic loader must not overlap the native IMEM image. */
#define TX_MIN_IMEM_BLOCKS ((TX_NATIVE_DMEM_OFF + 0x200u + 0xffu) >> 8)
#endif
