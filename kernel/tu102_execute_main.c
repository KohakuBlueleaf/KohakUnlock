// SPDX-License-Identifier: GPL-2.0-only
#include <crypto/hash.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/hex.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "tu102_dma.h"
#define TX_EXPERIMENT_V4
#include "tu102_core_port.h"
MODULE_INFO(kohak_selector_profile, TX_SELECTOR_PROFILE);

static char *target;
static bool execute;
static bool pre_udev;
static int result = -EINPROGRESS;
static bool handoff_safe = true;
module_param(target, charp, 0400);
module_param(execute, bool, 0400);
module_param(pre_udev, bool, 0400);
module_param(result, int, 0400);
module_param(handoff_safe, bool, 0400);
MODULE_PARM_DESC(execute, "Explicitly authorize one TU102 firmware transaction");
MODULE_PARM_DESC(pre_udev, "Private initramfs gate: udev/drivers absent, halt on unsafe handoff");
static struct {
    struct pci_dev *pdev;
    void __iomem *bar;
    resource_size_t bar_len;
    struct tu102_dma dma;
    Tx tx;
    const struct firmware *fw[5];
    u16 command;
    bool region, command_saved, override_set;
    ktime_t deadline;
    struct file *journal;
    u32 last_read_off, last_read_value, last_write_off, last_write_value;
} run;
static const struct {
    const char *name, *hash;
    size_t size;
} assets[] = {
    {"tu102-lab/" TX_NATIVE_NAME, TX_NATIVE_SHA256, TX_FWSEC_SIZE},
    {"tu102-lab/bl_gsp_tu102.bin", "f21f1cfbb8fffa8067685314367fc4c50f3f84dd99eda7c19db4292d9269b9d9", 768},
    {"tu102-lab/booter_ucode_prod.bin", "e0f0fc93da097b5b68e5af3adb58b2235ed1fad5fce0b23592d3895af44cb743", 59136},
    {"tu102-lab/v67_payload.bin", "de7288bcfb80c2d4b259a59e823c926ba0a9adcab594219cc6802c94257ce271", 64000},
    {"tu102-lab/gsp_rm_boot_dbg.bin", "fa43239bcee7b97ca62f007cc68487560a39e19f74f3dde7486db3f98df8e471", 131072}
};
/* The private boot gate places this fixed path on its dedicated log directory.
 * Stage records are fsynced before the next engine action. No file I/O in MMIO
 * callbacks; never use a GPU reset as a way to recover the logger. */
static int journal(const char *phase, int code, u32 a, u32 b)
{
    char line[512];
    int len;
    ssize_t written;
    if (!run.journal) return pre_udev ? -EIO : 0;
    len = scnprintf(line, sizeof(line),
        "phase=%s code=%d stage=%u a=%#x b=%#x mutated=%d unlocked=%d quiescent=%d safe=%d read=%#x:%#x write=%#x:%#x\n",
        phase, code, run.tx.stage, a, b, run.tx.mutated, run.tx.unlocked,
        run.tx.quiescent, handoff_safe, run.last_read_off, run.last_read_value,
        run.last_write_off, run.last_write_value);
    written = kernel_write(run.journal, line, len, &run.journal->f_pos);
    if (written != len) return written < 0 ? (int)written : -EIO;
    return vfs_fsync(run.journal, 0);
}
/* All callers hold device_lock across the entire transaction. */
static bool has_driver_override(struct pci_dev *pdev)
{
#ifdef KOH_DEVICE_DRIVER_OVERRIDE
    return device_has_driver_override(&pdev->dev);
#else
    return pdev->driver_override != NULL;
#endif
}
static int set_driver_override(struct pci_dev *pdev, const char *name)
{
#ifdef KOH_DEVICE_DRIVER_OVERRIDE
    /* This API uses its own spinlock; it does not retake device_lock. */
    return device_set_driver_override(&pdev->dev, name);
#else
    const char *old = pdev->driver_override;
    const char *new = NULL;
    /* driver_set_override() would recursively acquire device_lock here. */
    if (*name) {
        new = kstrdup(name, GFP_KERNEL);
        if (!new) return -ENOMEM;
    }
    pdev->driver_override = new;
    kfree(old);
    return 0;
#endif
}
static int verify_asset(unsigned int index)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    u8 digest[32], expected[32];
    int ret;
    if (run.fw[index]->size != assets[index].size || hex2bin(expected, assets[index].hash, 32))
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
    run.last_read_off = off;
    run.last_read_value = *value;
    return 0;
}
static int wr(void *unused, u32 off, u32 value)
{
    if (!access_ok_at(off) || !tx_write_allowed(off)) return 1;
    handoff_safe = false;
    run.dma.mutated = true;
    run.last_write_off = off;
    run.last_write_value = value;
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
    dev_info(&run.pdev->dev, "tu102_execute stage=%u a=%#x b=%#x mutated=%d quiescent=%d\n",
             stage, a, b, run.tx.mutated, run.tx.quiescent);
    if (pci_read_config_dword(run.pdev, PCI_VENDOR_ID, &identity) ||
        identity != ((u32)TX_DEVICE_ID << 16 | 0x10de))
        return 1;
    return journal("stage", stage, a, b) ? 1 : 0;
}
static int guard_note(void *unused, u32 off, u32 expected, u32 actual, u32 mask)
{
    if ((expected & mask) != (actual & mask)) {
        dev_err(&run.pdev->dev, "tu102_execute guard off=%#x expected=%#x actual=%#x mask=%#x\n", off, expected, actual, mask);
        return journal("guard-mismatch", off, expected, actual) ? 1 : 0;
    }
    if ((off == 0x409664 || off == 0x40966c) && mask == 0xffffffffu)
        return journal("selector-observed", off, expected, actual) ? 1 : 0;
    return 0;
}
static u64 address(void *unused, u32 off)
{
    unsigned int index = off / TU102_DMA_BLOCK_SIZE;
    if (index >= run.dma.allocated) return 0;
    return run.dma.blocks[index].address + off % TU102_DMA_BLOCK_SIZE;
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
static int __init tu102_execute_init(void)
{
    unsigned int i;
    u16 command;
    u32 capacity;
    u64 fb;
    int ret, core;
#ifndef KOH_UNLOCK_REVIEWED_PROFILE
    pr_err("tu102_execute: public default build disables hardware execution; read docs/TURING.md\n");
    return -EOPNOTSUPP;
#endif
#ifdef KOH_DEVICE_DRIVER_OVERRIDE
    /* device_lock still serializes binding, but not new override sysfs writes.
     * Support only the reviewed early-boot gate, with no udev/driver writers.
     * pre_udev is an assertion by that private gate, not a general live API.
     */
    if (!pre_udev) return -EOPNOTSUPP;
#endif
#ifdef KOH_TARGET_NVIDIA_TRIAL
    if (!pre_udev) return -EOPNOTSUPP;
#endif
    if (!execute || !target || strcmp(target, TX_TARGET)) return -EINVAL;
    if (pre_udev) {
        run.journal = filp_open("/run/kohak-tu102-trial/events.log",
                               O_WRONLY | O_APPEND | O_NOFOLLOW, 0);
        if (IS_ERR(run.journal)) { ret = PTR_ERR(run.journal); run.journal = NULL; return ret; }
        if (!S_ISREG(file_inode(run.journal)->i_mode) ||
            file_inode(run.journal)->i_sb->s_magic != MSDOS_SUPER_MAGIC) {
            filp_close(run.journal, NULL);
            run.journal = NULL;
            return -EINVAL;
        }
        ret = journal("admission", 0, TX_DEVICE_ID, TX_SUBDEVICE_ID);
        if (ret) goto close_journal;
    }
    run.pdev = pci_get_domain_bus_and_slot(0, TX_TARGET_BUS, PCI_DEVFN(0, 0));
    if (!run.pdev) { ret = -ENODEV; goto close_journal; }
    device_lock(&run.pdev->dev);
    if (run.pdev->driver) { ret = -EBUSY; goto out; }
    if (run.pdev->vendor != 0x10de || run.pdev->device != TX_DEVICE_ID ||
        run.pdev->subsystem_vendor != TX_SUBVENDOR_ID || run.pdev->subsystem_device != TX_SUBDEVICE_ID) {
        ret = -ENODEV; goto out;
    }
    if (has_driver_override(run.pdev)) { ret = -EBUSY; goto out; }
    ret = set_driver_override(run.pdev, "tu102-experiment-hold");
    if (ret) goto out;
    run.override_set = true;
    ret = journal("device-held", 0, 0, 0);
    if (ret) goto out;
    ret = pci_read_config_word(run.pdev, PCI_COMMAND, &run.command);
    if (ret || run.command == 0xffff || !(run.command & PCI_COMMAND_MEMORY)) { ret = -EACCES; goto out; }
    run.command_saved = true;
    run.bar_len = pci_resource_len(run.pdev, 0);
    if (run.bar_len < 0x1000000) { ret = -EINVAL; goto out; }
    ret = pci_request_region(run.pdev, 0, "tu102_execute");
    if (ret) goto out;
    run.region = true;
    run.bar = pci_iomap(run.pdev, 0, 0);
    if (!run.bar) { ret = -ENOMEM; goto out; }
    run.deadline = ktime_add_ms(ktime_get(), 90000);
    if (rd(NULL, 0x100ce0, &capacity) || capacity == ~0u || (capacity & 0xffff0000) == 0xbadf0000) { ret = -ENODEV; goto out; }
    fb = (u64)((capacity >> 4) & 63) << ((capacity & 15) + 20);
    if (capacity & BIT(30)) fb = fb / 16 * 15;
    if (fb != TX_FB_SIZE) { ret = -ERANGE; goto out; }
    for (i = 0; i < ARRAY_SIZE(assets); i++) {
        ret = request_firmware(&run.fw[i], assets[i].name, NULL);
        if (ret) goto out;
        ret = verify_asset(i);
        if (ret) goto out;
        ret = journal("asset-verified", i, run.fw[i]->size, 0);
        if (ret) goto out;
    }
    run.tx = (Tx){ .read=rd, .write=wr, .delay=delay_us, .barrier=memory_barrier,
        .event=event, .guard_note=guard_note, .address=address,
        .native_fwsec=run.fw[0]->data, .generic_bl=run.fw[1]->data };
    core = tx_guards(&run.tx);
    if (core) { ret = -EPERM; goto out; }
    ret = journal("guards-passed", 0, run.tx.initial_ss0, run.tx.initial_ss1);
    if (ret) goto out;
    pci_clear_master(run.pdev);
    if (pci_read_config_word(run.pdev, PCI_COMMAND, &command) || (command & PCI_COMMAND_MASTER)) { ret = -EIO; goto out; }
    ret = journal("dma-prepare-begin", 0, 40, TU102_DMA_BLOCKS);
    if (ret) goto out;
    ret = tu102_dma_prepare(&run.dma, run.pdev);
    core = journal("dma-prepare-return", ret, run.dma.allocated, 0);
    if (run.dma.allocated) {
        u64 last = run.dma.blocks[run.dma.allocated - 1].address;
        int logged = journal("dma-last-address", ret, upper_32_bits(last), lower_32_bits(last));
        if (!core) core = logged;
    }
    if (ret) goto out;
    if (core) { ret = core; goto out; }
    ret = journal("dma-prepared", 0, run.dma.allocated, 0);
    if (ret) goto out;
    run.tx.pool = vzalloc(TX_POOL_SIZE);
    if (!run.tx.pool) { ret = -ENOMEM; goto out; }
    run.tx.device_address = run.dma.address;
    core = tx_prepare(&run.tx, run.fw[2]->data, run.fw[3]->data, run.fw[4]->data);
    if (core) { ret = -EBADMSG; goto out; }
    for (i = 0; i < run.dma.allocated; i++) {
        memcpy(run.dma.blocks[i].cpu, run.tx.pool + i * TU102_DMA_BLOCK_SIZE, TU102_DMA_BLOCK_SIZE);
        if (memcmp(run.dma.blocks[i].cpu, run.tx.pool + i * TU102_DMA_BLOCK_SIZE, TU102_DMA_BLOCK_SIZE)) { ret = -EIO; goto out; }
    }
    dma_wmb();
    ret = journal("inputs-verified", 0, 0, 0);
    if (ret) goto out;
    pci_set_master(run.pdev);
    if (pci_read_config_word(run.pdev, PCI_COMMAND, &command) || !(command & PCI_COMMAND_MASTER)) { ret = -EIO; goto out; }
    core = tx_run(&run.tx);
    dev_info(&run.pdev->dev, "tu102_execute core=%d stage=%u mutated=%d unlocked=%d quiescent=%d\n",
             core, run.tx.stage, run.tx.mutated, run.tx.unlocked, run.tx.quiescent);
    ret = core ? -EIO : 0;
    journal("core-return", core, 0, 0);
    if (run.tx.mutated && (core || !run.tx.quiescent || !run.tx.unlocked)) {
        handoff_safe = false;
        goto retain;
    }
    run.dma.quiescent = run.tx.quiescent;
    handoff_safe = true;
out:
    if (restore_command()) { ret = -EIO; handoff_safe = false; goto retain; }
    if (run.dma.pdev) {
        int cleanup = tu102_dma_release(&run.dma);
        if (cleanup) { ret = cleanup; handoff_safe = false; goto retain; }
    }
    if (run.bar) { pci_iounmap(run.pdev, run.bar); run.bar = NULL; }
    if (run.region) { pci_release_region(run.pdev, 0); run.region = false; }
    vfree(run.tx.pool); run.tx.pool = NULL;
    for (i = 0; i < ARRAY_SIZE(assets); i++) { release_firmware(run.fw[i]); run.fw[i] = NULL; }
retain:
    if (handoff_safe && run.override_set) {
        int cleanup = set_driver_override(run.pdev, "");
        if (cleanup) { ret = cleanup; handoff_safe = false; }
        else run.override_set = false;
    }
    result = ret;
    dev_info(&run.pdev->dev, "tu102_execute result=%d handoff_safe=%d; %s\n", result, handoff_safe,
             handoff_safe ? "resources released" : "STOP: resources retained; FULL POWER OFF REQUIRED");
    device_unlock(&run.pdev->dev);
    if (handoff_safe) { pci_dev_put(run.pdev); run.pdev = NULL; }
    journal("handoff", ret, 0, 0);
    if (run.journal) { filp_close(run.journal, NULL); run.journal = NULL; }
    return 0;
close_journal:
    journal("init-refused", ret, 0, 0);
    if (run.journal) { filp_close(run.journal, NULL); run.journal = NULL; }
    return ret;
}
module_init(tu102_execute_init);
MODULE_LICENSE("GPL");
MODULE_INFO(kohak_executor_profile, TX_IDENTITY_PROFILE);
MODULE_DESCRIPTION("Experimental TU102 22GiB exact target " TX_TARGET "; explicit opt-in");
MODULE_FIRMWARE("tu102-lab/" TX_NATIVE_NAME);
MODULE_FIRMWARE("tu102-lab/bl_gsp_tu102.bin");
MODULE_FIRMWARE("tu102-lab/booter_ucode_prod.bin");
MODULE_FIRMWARE("tu102-lab/v67_payload.bin");
MODULE_FIRMWARE("tu102-lab/gsp_rm_boot_dbg.bin");
