/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU102_IDENTITY_H
#define TU102_IDENTITY_H

/* Shared by the executor and DMA translation units. Never combine allowlists. */
#ifdef TX_PROFILE_NVIDIA_1E07_12A4
#if defined(__KERNEL__) && (!defined(KOH_TARGET_NVIDIA_TRIAL) || !defined(KOH_UNLOCK_REVIEWED_PROFILE))
#error "The NVIDIA candidate requires an explicit target trial build"
#endif
#define TX_IDENTITY_PROFILE "nvidia-1e07-12a4-trial"
#define TX_TARGET "0000:01:00.0"
#define TX_TARGET_BUS 0x01
#define TX_DEVICE_ID 0x1e07
#define TX_SUBVENDOR_ID 0x10de
#define TX_SUBDEVICE_ID 0x12a4
#else
#define TX_IDENTITY_PROFILE "historical"
#define TX_TARGET "0000:42:00.0"
#define TX_TARGET_BUS 0x42
#define TX_DEVICE_ID 0x1e04
#define TX_SUBVENDOR_ID 0x107d
#define TX_SUBDEVICE_ID 0x1e04
#endif

#endif
