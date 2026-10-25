#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
struct device { u64 *dma_mask; u64 coherent_dma_mask; };
struct pci_dev { struct device dev; void *driver; unsigned vendor, device, subsystem_vendor, subsystem_device; };
static int refs, frees, mode, calls;
#define kcalloc(n,s,f) calloc(n,s)
#define kfree(p) free(p)
#define GFP_KERNEL 0
#define DMA_BIT_MASK(n) ((1ULL<<(n))-1)
static struct pci_dev *pci_dev_get(struct pci_dev *p) { refs++; return p; }
static void pci_dev_put(struct pci_dev *p) { (void)p; refs--; }
static int dma_set_mask(struct device *d,u64 m) { if(mode==4) return -EIO; *d->dma_mask=m; return 0; }
static int dma_set_coherent_mask(struct device *d,u64 m) { d->coherent_dma_mask=m; return 0; }
static int dma_set_mask_and_coherent(struct device *d,u64 m) { *d->dma_mask=m; if(mode==1) return -EIO; d->coherent_dma_mask=m; return 0; }
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *a,int flags) { (void)d;(void)flags; assert(n <= 65536); calls++; *a=mode==3?0x1000:0x200000000ULL+(uint64_t)calls*0x10000; return (mode==2 || (mode==5 && calls==4))?NULL:malloc(n); }
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t a) { (void)d;(void)n;(void)a; frees++;free(p); }
static void dma_wmb(void) {}
#define TU104_DMA_HOST_TEST
#include "kernel/tu104_dma.c"
int main(void) {
    u64 mask;
    struct pci_dev p;
    struct tu104_dma s;
    struct pci_dev wrong;
    for(int test=0;test<8;test++) {
        memset(&s,0,sizeof(s)); mask=0xffffffff;
        p=(struct pci_dev){.dev={&mask,0xffffffff},.vendor=0x10de,.device=0x1e89,.subsystem_vendor=0x1458,.subsystem_device=0x3fc1};
        wrong=(struct pci_dev){.dev={&mask,0xffffffff},.vendor=0x10de,.device=0x1e04,.subsystem_vendor=0x107d,.subsystem_device=0x1e04};
        refs=frees=calls=0;mode=test<=3?test:(test==6?5:0);
        int ret=(test==7)?tu104_dma_prepare(&s,&wrong):tu104_dma_prepare(&s,&p);
        if(test==1) assert(ret==-EIO);
        else if(test==2 || test==6) assert(ret==-ENOMEM);
        else if(test==3) assert(ret==-ERANGE);
        else if(test==7) assert(ret==-ENODEV && refs==0);
        else assert(ret==0);
        if(test==4) {
            s.mutated=true;
            assert(tu104_dma_release(&s)==-EBUSY && frees==0 && refs==1);
            s.quiescent=true;
        }
        if(test==5) {
            mode=4;
            assert(tu104_dma_release(&s)==-EIO && frees==0 && refs==1);
            mode=0;
        }
        if(test!=7) assert(tu104_dma_release(&s)==0);
        assert(refs==0 && mask==0xffffffff && p.dev.coherent_dma_mask==0xffffffff);
        assert(frees==((test==1 || test==2 || test==7)?0:(test==3?1:(test==6?3:512))));
    }
    puts("PASS: seven real DMA-helper cleanup scenarios plus wrong-card refusal, no GPU access");
    return 0;
}
