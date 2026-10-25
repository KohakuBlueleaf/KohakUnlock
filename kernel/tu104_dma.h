/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU104_DMA_H
#define TU104_DMA_H
#ifndef TU104_DMA_HOST_TEST
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#endif
#define TU104_DMA_BLOCK_SIZE (64 * 1024)
#define TU104_DMA_BLOCKS 512
struct tu104_dma_block { void *cpu; dma_addr_t address; };
struct tu104_dma {
    struct pci_dev *pdev;
    void *cpu;
    dma_addr_t address;
    size_t size;
    struct tu104_dma_block *blocks;
    unsigned int allocated;
    u64 old_streaming, old_coherent;
    bool masks_changed, mutated, quiescent;
};
int tu104_dma_prepare(struct tu104_dma *state, struct pci_dev *pdev);
int tu104_dma_release(struct tu104_dma *state);
#endif
