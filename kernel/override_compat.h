/* SPDX-License-Identifier: GPL-2.0-only */
/* driver_override compatibility across kernel versions */
#ifndef KOH_OVERRIDE_COMPAT_H
#define KOH_OVERRIDE_COMPAT_H
#ifdef __KERNEL__
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
#define KOH_OVERRIDE_HAS(dev) device_has_driver_override(dev)
#define KOH_OVERRIDE_SET(dev, s) device_set_driver_override(dev, s)
#define KOH_OVERRIDE_SET_NEEDS_UNLOCK 0
#elif defined(driver_set_override)
static inline bool koh_override_has(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	if (!pdev)
		return false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	return pdev->driver_override != NULL;
#else
	return false;
#endif
}

static inline int koh_override_set(struct device *dev, const char *s)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	if (!pdev)
		return -EINVAL;
	return driver_set_override(dev, &pdev->driver_override, s, s ? strlen(s) : 0);
}
#define KOH_OVERRIDE_HAS(dev) koh_override_has(dev)
#define KOH_OVERRIDE_SET(dev, s) koh_override_set(dev, s)
#define KOH_OVERRIDE_SET_NEEDS_UNLOCK 1
#else
static inline bool koh_override_has(struct device *dev) { return false; }
static inline int koh_override_set(struct device *dev, const char *s) { (void)dev; (void)s; return -ENOSYS; }
#define KOH_OVERRIDE_HAS(dev) koh_override_has(dev)
#define KOH_OVERRIDE_SET(dev, s) koh_override_set(dev, s)
#define KOH_OVERRIDE_SET_NEEDS_UNLOCK 0
#endif
#endif
#endif
