/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU102_DMA_H
#define TU102_DMA_H
#ifndef TU102_DMA_HOST_TEST
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#endif
#define TU102_DMA_BLOCK_SIZE (64 * 1024)
#define TU102_DMA_BLOCKS 512
struct tu102_dma_block { void *cpu; dma_addr_t address; };
struct tu102_dma {
    struct pci_dev *pdev;
    void *cpu;
    dma_addr_t address;
    size_t size;
    struct tu102_dma_block *blocks;
    unsigned int allocated;
    u64 old_streaming, old_coherent;
    bool masks_changed, mutated, quiescent;
};
int tu102_dma_prepare(struct tu102_dma *state, struct pci_dev *pdev);
int tu102_dma_release(struct tu102_dma *state);
#endif
