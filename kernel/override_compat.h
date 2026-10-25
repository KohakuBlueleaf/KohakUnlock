/* SPDX-License-Identifier: GPL-2.0-only */
/* driver_override compatibility: struct device.driver_override became private
 * in 6.15+ behind device_set_driver_override()/device_has_driver_override();
 * older kernels expose the field and the driver_set_override() helper. Same
 * semantics: set replaces, empty string clears, device lock must be held. */
#ifndef KOH_OVERRIDE_COMPAT_H
#define KOH_OVERRIDE_COMPAT_H
#ifdef __KERNEL__
#include <linux/device.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
#define KOH_OVERRIDE_HAS(dev) device_has_driver_override(dev)
#define KOH_OVERRIDE_SET(dev, s) device_set_driver_override(dev, s)
#else
static inline bool koh_override_has(struct device *dev) { return dev->driver_override != NULL; }
static inline int koh_override_set(struct device *dev, const char *s)
{
    return driver_set_override(dev, &dev->driver_override, s, s ? strlen(s) : 0);
}
#define KOH_OVERRIDE_HAS(dev) koh_override_has(dev)
#define KOH_OVERRIDE_SET(dev, s) koh_override_set(dev, s)
#endif
#endif
#endif
