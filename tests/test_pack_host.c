#define main dma_test_main
#include "test_dma_host.c"
#undef main
#include "kernel/tu102_pack.c"
int main(void)
{
    u64 mask=0xffffffff;
    struct pci_dev p=profile_device(&mask);
    struct tu102_dma s={0};
    struct tu102_pack_result out;
    assert(dma_test_main()==0);
    mode=refs=frees=calls=0;
    assert(tu102_dma_prepare(&s,&p)==0);
    for(unsigned i=0;i<s.allocated;i++)
        s.blocks[i].address=0x400000000ULL+(u64)(s.allocated-i)*0x30000;
    assert(tu102_pack(&s,22ULL<<30,1,&out)==0);
    assert(out.frts==0x57fe00000ULL);
    assert(tu102_pack_verify(&s,&out)==0);
    unsigned char *r=cpu_at(&s,TU102_RADIX_OFF);
    for(unsigned i=0;i<out.image_pages;i++) {
        u64 expected=s.blocks[(TU102_DATA_OFF+i*4096)/65536].address+(TU102_DATA_OFF+i*4096)%65536;
        assert(get64(r+8192+i*8)==expected);
    }
    assert(get64(cpu_at(&s,TU102_META_OFF)+16*8)==107ULL*1024*1024);
    assert(tu102_pack(&s,11ULL<<30,1,&out)==-EINVAL);
    assert(tu102_pack(&s,22ULL<<30,8,&out)==-ERANGE);
    u64 address=s.blocks[10].address;
    s.blocks[10].address=s.blocks[11].address;
    assert(tu102_pack(&s,22ULL<<30,1,&out)==-EINVAL);
    s.blocks[10].address=address+1;
    assert(tu102_pack(&s,22ULL<<30,1,&out)==-ERANGE);
    s.blocks[10].address=address;
    assert(tu102_pack(&s,22ULL<<30,1,&out)==0);
    for(unsigned at=0;at<65536;at+=4096) {
        r[at]^=1;
        assert(tu102_pack_verify(&s,&out)!=0);
        r[at]^=1;
    }
    assert(tu102_pack_verify(&s,&out)==0);
    unsigned char *m=cpu_at(&s,TU102_META_OFF);
    for(unsigned at=0;at<256;at+=8) {
        m[at]^=1;
        assert(tu102_pack_verify(&s,&out)!=0);
        m[at]^=1;
    }
    assert(tu102_dma_release(&s)==0 && refs==0);
    puts("PASS metadata layout, noncontiguous DMA radix traversal, invalid inputs and corruption rejection");
    return 0;
}
