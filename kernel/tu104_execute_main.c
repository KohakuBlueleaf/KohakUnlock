// SPDX-License-Identifier: GPL-2.0-only
#include <crypto/hash.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/hex.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include "override_compat.h"
#include "tu104_dma.h"
#define TX_EXPERIMENT_V4
#include "tu104_core_port.h"

static char *target;
static bool execute;
static int result = -EINPROGRESS;
static bool handoff_safe = true;
module_param(target, charp, 0400);
module_param(execute, bool, 0400);
module_param(result, int, 0400);
module_param(handoff_safe, bool, 0400);
MODULE_PARM_DESC(execute, "Explicitly authorize one TU104 firmware transaction");
static struct {
    struct pci_dev *pdev;
    void __iomem *bar;
    resource_size_t bar_len;
    struct tu104_dma dma;
    Tx tx;
    const struct firmware *fw[5];
    u16 command;
    bool region, command_saved, override_set;
    ktime_t deadline;
} run;
static const struct {
    const char *name, *hash;
    size_t size;
} assets[] = {
    {"tu104-lab/tu104-fwsec.bin", KOH_FWSEC_SHA256, KOH_FWSEC_SIZE},
    {"tu104-lab/bl_gsp_tu102.bin", KOH_GENERIC_SHA256, KOH_GENERIC_SIZE},
    {"tu104-lab/booter_ucode_prod.bin", KOH_BOOTER_SHA256, KOH_BOOTER_SIZE},
    {"tu104-lab/v67_payload.bin", KOH_SIGNATURE_SHA256, KOH_SIGNATURE_SIZE},
    {"tu104-lab/gsp_rm_boot_dbg.bin", KOH_BOOT_SHA256, KOH_BOOT_CONTAINER_SIZE}
};
static int verify_asset(unsigned int index)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    u8 digest[32], expected[32];
    int ret;
    if (!assets[index].size ||
        run.fw[index]->size != assets[index].size || hex2bin(expected, assets[index].hash, 32))
        return -EBADMSG;
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) return PTR_ERR(tfm);
    desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) { crypto_free_shash(tfm); return -ENOMEM; }
    desc->tfm = tfm;
    ret = crypto_shash_digest(desc, run.fw[index]->data, run.fw[index]->size, digest);
    if (!ret && memcmp(digest, expected, 32)) ret = -EBADMSG;
    kfree(desc);
    crypto_free_shash(tfm);
    return ret;
}
static int access_ok_at(u32 off)
{
    return run.bar && !(off & 3) && off <= run.bar_len - 4 &&
           ktime_compare(ktime_get(), run.deadline) < 0;
}
static int rd(void *unused, u32 off, u32 *value)
{
    if (!access_ok_at(off)) return 1;
    *value = readl(run.bar + off);
    return 0;
}
static int wr(void *unused, u32 off, u32 value)
{
    if (!access_ok_at(off) || !tx_write_allowed(off)) return 1;
    handoff_safe = false;
    run.dma.mutated = true;
    writel(value, run.bar + off);
    return 0;
}
static int delay_us(void *unused, u32 us)
{
    if (ktime_compare(ktime_get(), run.deadline) >= 0) return 1;
    if (us >= 20000) msleep(DIV_ROUND_UP(us, 1000));
    else usleep_range(us, us + 50);
    return 0;
}
static int memory_barrier(void *unused) { dma_wmb(); return 0; }
static int event(void *unused, unsigned int stage, u32 a, u32 b)
{
    u32 identity;
    dev_info(&run.pdev->dev, "tu104_execute stage=%u a=%#x b=%#x mutated=%d quiescent=%d\n",
             stage, a, b, run.tx.mutated, run.tx.quiescent);
    if (pci_read_config_dword(run.pdev, PCI_VENDOR_ID, &identity) ||
        identity != ((KOH_PCI_DEVICE << 16) | 0x10de))
        return 1;
    return 0;
}
static int guard_note(void *unused, u32 off, u32 expected, u32 actual, u32 mask)
{
    if ((expected & mask) != (actual & mask))
        dev_err(&run.pdev->dev, "tu104_execute guard off=%#x expected=%#x actual=%#x mask=%#x\n", off, expected, actual, mask);
    return 0;
}
static u64 address(void *unused, u32 off)
{
    unsigned int index = off / TU104_DMA_BLOCK_SIZE;
    if (index >= run.dma.allocated) return 0;
    return run.dma.blocks[index].address + off % TU104_DMA_BLOCK_SIZE;
}
static int restore_command(void)
{
    u16 now, expected;
    if (!run.command_saved) return 0;
    if (pci_read_config_word(run.pdev, PCI_COMMAND, &now) || now == 0xffff) return -EIO;
    expected = (now & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) |
               (run.command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER));
    if (pci_write_config_word(run.pdev, PCI_COMMAND, expected) ||
        pci_read_config_word(run.pdev, PCI_COMMAND, &now) || now != expected) return -EIO;
    return 0;
}
static int __init tu104_execute_init(void)
{
    unsigned int i;
    u16 command;
    u32 capacity;
    u64 fb;
    int ret, core;
#ifndef KOH_UNLOCK_REVIEWED_PROFILE
    pr_err("tu104_execute: public default build disables hardware execution; read docs/TU104-2060.md\n");
    return -EOPNOTSUPP;
#endif
    if (!execute || !target || strcmp(target, KOH_TARGET_BDF)) return -EINVAL;
    run.pdev = pci_get_domain_bus_and_slot(0, KOH_TARGET_BUS, PCI_DEVFN(0, 0));
    if (!run.pdev) return -ENODEV;
    device_lock(&run.pdev->dev);
    if (run.pdev->driver) { ret = -EBUSY; goto out; }
    if (run.pdev->vendor != 0x10de || run.pdev->device != KOH_PCI_DEVICE ||
        run.pdev->subsystem_vendor != KOH_PCI_SUBSYSTEM_VENDOR ||
        run.pdev->subsystem_device != KOH_PCI_SUBSYSTEM_DEVICE) {
        ret = -ENODEV; goto out;
    }
    if (KOH_OVERRIDE_HAS(&run.pdev->dev)) { ret = -EBUSY; goto out; }
#if KOH_OVERRIDE_SET_NEEDS_UNLOCK
    device_unlock(&run.pdev->dev);
    if (KOH_OVERRIDE_SET(&run.pdev->dev, "tu104-experiment-hold")) { device_lock(&run.pdev->dev); ret = -ENOMEM; goto out; }
    device_lock(&run.pdev->dev);
#else
    if (KOH_OVERRIDE_SET(&run.pdev->dev, "tu104-experiment-hold")) { ret = -ENOMEM; goto out; }
#endif
    run.override_set = true;
    ret = pci_read_config_word(run.pdev, PCI_COMMAND, &run.command);
    if (ret || run.command == 0xffff || !(run.command & PCI_COMMAND_MEMORY)) { ret = -EACCES; goto out; }
    run.command_saved = true;
    run.bar_len = pci_resource_len(run.pdev, 0);
    if (run.bar_len < 0x1000000) { ret = -EINVAL; goto out; }
    ret = pci_request_region(run.pdev, 0, "tu104_execute");
    if (ret) goto out;
    run.region = true;
    run.bar = pci_iomap(run.pdev, 0, 0);
    if (!run.bar) { ret = -ENOMEM; goto out; }
    run.deadline = ktime_add_ms(ktime_get(), 90000);
    if (rd(NULL, 0x100ce0, &capacity) || capacity == ~0u || (capacity & 0xffff0000) == 0xbadf0000) { ret = -ENODEV; goto out; }
    fb = (u64)((capacity >> 4) & 63) << ((capacity & 15) + 20);
    if (capacity & BIT(30)) fb = fb / 16 * 15;
    if (fb != KOH_FB_SIZE) { ret = -ERANGE; goto out; }
    for (i = 0; i < ARRAY_SIZE(assets); i++) {
        ret = request_firmware(&run.fw[i], assets[i].name, NULL);
        if (ret) goto out;
        ret = verify_asset(i);
        if (ret) goto out;
    }
    run.tx = (Tx){ .read=rd, .write=wr, .delay=delay_us, .barrier=memory_barrier,
        .event=event, .guard_note=guard_note, .address=address,
        .native_fwsec=run.fw[0]->data, .generic_bl=run.fw[1]->data };
    core = tx_guards(&run.tx);
    if (core) { ret = -EPERM; goto out; }
    pci_clear_master(run.pdev);
    if (pci_read_config_word(run.pdev, PCI_COMMAND, &command) || (command & PCI_COMMAND_MASTER)) { ret = -EIO; goto out; }
    ret = tu104_dma_prepare(&run.dma, run.pdev);
    if (ret) goto out;
    run.tx.pool = vzalloc(TX_POOL_SIZE);
    if (!run.tx.pool) { ret = -ENOMEM; goto out; }
    run.tx.device_address = run.dma.address;
    core = tx_prepare(&run.tx, run.fw[2]->data, run.fw[3]->data, run.fw[4]->data);
    if (core) { ret = -EBADMSG; goto out; }
    for (i = 0; i < run.dma.allocated; i++) {
        memcpy(run.dma.blocks[i].cpu, run.tx.pool + i * TU104_DMA_BLOCK_SIZE, TU104_DMA_BLOCK_SIZE);
        if (memcmp(run.dma.blocks[i].cpu, run.tx.pool + i * TU104_DMA_BLOCK_SIZE, TU104_DMA_BLOCK_SIZE)) { ret = -EIO; goto out; }
    }
    dma_wmb();
    pci_set_master(run.pdev);
    if (pci_read_config_word(run.pdev, PCI_COMMAND, &command) || !(command & PCI_COMMAND_MASTER)) { ret = -EIO; goto out; }
    core = tx_run(&run.tx);
    dev_info(&run.pdev->dev, "tu104_execute core=%d stage=%u mutated=%d unlocked=%d quiescent=%d\n",
             core, run.tx.stage, run.tx.mutated, run.tx.unlocked, run.tx.quiescent);
    ret = core ? -EIO : 0;
    if (run.tx.mutated && (core || !run.tx.quiescent || !run.tx.unlocked)) {
        handoff_safe = false;
        goto retain;
    }
    run.dma.quiescent = run.tx.quiescent;
    handoff_safe = true;
out:
    if (restore_command()) { ret = -EIO; handoff_safe = false; goto retain; }
    if (run.dma.pdev) {
        int cleanup = tu104_dma_release(&run.dma);
        if (cleanup) { ret = cleanup; handoff_safe = false; goto retain; }
    }
    if (run.bar) { pci_iounmap(run.pdev, run.bar); run.bar = NULL; }
    if (run.region) { pci_release_region(run.pdev, 0); run.region = false; }
    vfree(run.tx.pool); run.tx.pool = NULL;
    for (i = 0; i < ARRAY_SIZE(assets); i++) { release_firmware(run.fw[i]); run.fw[i] = NULL; }
retain:
    if (handoff_safe && run.override_set) {
        if (KOH_OVERRIDE_SET(&run.pdev->dev, "")) {
            ret = -EIO;
            handoff_safe = false;
            goto retain;
        }
        run.override_set = false;
    }
    result = ret;
    dev_info(&run.pdev->dev, "tu104_execute result=%d handoff_safe=%d; %s\n", result, handoff_safe,
             handoff_safe ? "resources released" : "STOP: resources retained; FULL POWER OFF REQUIRED");
    device_unlock(&run.pdev->dev);
    if (handoff_safe) { pci_dev_put(run.pdev); run.pdev = NULL; }
    return 0;
}
module_init(tu104_execute_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental TU104 RTX 2060 single-card unlock; explicit opt-in, unvalidated");
MODULE_FIRMWARE("tu104-lab/tu104-fwsec.bin");
MODULE_FIRMWARE("tu104-lab/bl_gsp_tu102.bin");
MODULE_FIRMWARE("tu104-lab/booter_ucode_prod.bin");
MODULE_FIRMWARE("tu104-lab/v67_payload.bin");
MODULE_FIRMWARE("tu104-lab/gsp_rm_boot_dbg.bin");
