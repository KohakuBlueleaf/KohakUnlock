// SPDX-License-Identifier: GPL-2.0-only
#ifndef TU102_DMA_HOST_TEST
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#endif
#include "tu102_dma.h"
#include "tu102_identity.h"

#ifndef TU102_DMA_HOST_TEST
MODULE_INFO(kohak_dma_profile, TX_IDENTITY_PROFILE);
#endif

/* Caller holds device lock; state is private and zero-initialized. */
int tu102_dma_prepare(struct tu102_dma *state, struct pci_dev *pdev)
{
    int ret;
    if (!state || !pdev || state->pdev || pdev->driver || !pdev->dev.dma_mask)
        return -EINVAL;
    if (pdev->vendor != 0x10de || pdev->device != TX_DEVICE_ID ||
        pdev->subsystem_vendor != TX_SUBVENDOR_ID || pdev->subsystem_device != TX_SUBDEVICE_ID)
        return -ENODEV;
    state->pdev = pci_dev_get(pdev);
    state->old_streaming = *pdev->dev.dma_mask;
    state->old_coherent = pdev->dev.coherent_dma_mask;
    state->masks_changed = true;
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
    if (ret)
        return ret;
    state->size = TU102_DMA_BLOCKS * TU102_DMA_BLOCK_SIZE;
    state->blocks = kcalloc(TU102_DMA_BLOCKS, sizeof(*state->blocks), GFP_KERNEL);
    if (!state->blocks)
        return -ENOMEM;
    while (state->allocated < TU102_DMA_BLOCKS) {
        struct tu102_dma_block *block = &state->blocks[state->allocated];
        block->cpu = dma_alloc_coherent(&pdev->dev, TU102_DMA_BLOCK_SIZE, &block->address, GFP_KERNEL);
        if (!block->cpu)
            return -ENOMEM;
        state->allocated++;
        if (block->address < (1ULL << 32) || (block->address & 4095) ||
            block->address > (1ULL << 40) - TU102_DMA_BLOCK_SIZE)
            return -ERANGE;
        memset(block->cpu, 0, TU102_DMA_BLOCK_SIZE);
    }
    /* cpu/address alias block zero, not a contiguous 32 MiB allocation. */
    state->cpu = state->blocks[0].cpu;
    state->address = state->blocks[0].address;
    dma_wmb();
    return 0;
}

/* Failed preparation also requires release; uncertain firmware retains ownership. */
int tu102_dma_release(struct tu102_dma *state)
{
    struct pci_dev *pdev;
    int a, b;
    if (!state || !state->pdev)
        return -EINVAL;
    if (state->mutated && !state->quiescent)
        return -EBUSY;
    pdev = state->pdev;
    if (state->masks_changed) {
        a = dma_set_mask(&pdev->dev, state->old_streaming);
        b = dma_set_coherent_mask(&pdev->dev, state->old_coherent);
        if (a || b)
            return -EIO;
    }
    while (state->allocated) {
        struct tu102_dma_block *block = &state->blocks[--state->allocated];
        dma_free_coherent(&pdev->dev, TU102_DMA_BLOCK_SIZE, block->cpu, block->address);
    }
    kfree(state->blocks);
    state->blocks = NULL;
    state->cpu = NULL;
    pci_dev_put(pdev);
    memset(state, 0, sizeof(*state));
    return 0;
}
