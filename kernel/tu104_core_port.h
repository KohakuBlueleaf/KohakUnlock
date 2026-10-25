/* SPDX-License-Identifier: GPL-2.0-only */
/* Experimental TU104 RTX 2060 (10de:1e89, 6 GiB) port of the KohakUnlock
 * TU102 core, which adapts haohervchb/Turing-unlock a188b24 (itself from
 * PZH1gdmu/CMP40HX-Unlock unlock40x_v70.c, TU106; V67 canary research by
 * Jon Pry). Upstream copyleft-next-0.3.1 and MIT attribution retained in
 * LICENSES/ and NOTICE.md. Turing register map family-verified against
 * nouveau (drivers/gpu/drm/nouveau/nvkm/subdev/fb) and NVIDIA
 * open-gpu-kernel-modules 580.178.04 sources; see docs/TU104-2060.md.
 * Per-card constants come from kernel/tu104_profile.h (template refuses
 * execution until regenerated from THIS card's own PROM diagnostics).
 * NO hardware run has been performed on TU104; this file is UNVALIDATED.
 * All platform access is injected, so failure paths can be tested off-device.
 */
#ifndef TU104_EXPERIMENT_CORE_H
#define TU104_EXPERIMENT_CORE_H
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

#ifdef KOH_PROFILE
#include KOH_PROFILE
#else
#include "tu104_profile.h"
#endif

#define TX_GSP 0x110000u
#define TX_SEC 0x840000u
#define TX_FWSEC_SIZE KOH_FWSEC_SIZE
#define TX_BOOTER_SIZE KOH_BOOTER_SIZE
#define TX_SIGNATURE_SIZE KOH_SIGNATURE_SIZE
#define TX_BOOT_SIZE KOH_BOOT_SIZE
#define TX_BOOT_CONTAINER_SIZE KOH_BOOT_CONTAINER_SIZE
#define TX_GENERIC_SIZE KOH_GENERIC_SIZE
#define TX_ELF_SIZE KOH_ELF_SIZE
#define TX_POOL_SIZE 0x2000000u
#define TX_FWSEC_OFF 0x00000u
#define TX_BOOTER_OFF 0x10000u
#define TX_SIGNATURE_OFF 0x20000u
#define TX_BOOT_OFF 0x30000u
#define TX_META_OFF 0x40000u
#define TX_RADIX_OFF 0x50000u
#define TX_DATA_OFF 0x60000u /* root + L1 + 14 L2 pages fit before this */
#define TX_FB_SIZE KOH_FB_SIZE
#define TX_FRTS KOH_FRTS

enum { TX_OK=0, TX_GUARD=1, TX_IO=2, TX_TIMEOUT=3, TX_VERIFY=4, TX_LOG=5 };
enum { TX_STAGE_GUARDS=1, TX_STAGE_FWSEC_RESET, TX_STAGE_FWSEC_LOAD,
       TX_STAGE_FWSEC_START, TX_STAGE_FWSEC_RESULT, TX_STAGE_BOOTER_RESET,
       TX_STAGE_BOOTER_LOAD, TX_STAGE_BOOTER_START, TX_STAGE_BOOTER_RESULT,
       TX_STAGE_CLEANUP, TX_STAGE_DONE };

typedef struct {
    void *opaque;
    int (*read)(void *,uint32_t,uint32_t *);
    int (*write)(void *,uint32_t,uint32_t);
    int (*delay)(void *,uint32_t);
    int (*barrier)(void *);
    int (*event)(void *,unsigned,uint32_t,uint32_t);
    int (*next_input)(void *); /* optional: remap immutable Booter inputs */
    uint8_t *pool;
    uint64_t device_address;
    uint64_t (*address)(void *, uint32_t);
    const uint8_t *native_fwsec, *generic_bl;
    int mutated, unlocked, quiescent;
    unsigned stage;
    /* Pin an observed baseline across allocation/preparation. These are NOT
     * decoded fuse fields; unknown selector tuples still refuse. */
    uint32_t initial_ss0,initial_ss1;
    int baseline_valid;
    int (*guard_note)(void *,uint32_t,uint32_t,uint32_t,uint32_t);
} Tx;

static void tx_zero(void *dst,size_t n) {
    uint8_t *p=dst; for(size_t i=0;i<n;i++) p[i]=0;
}
static void tx_copy(void *dst,const void *src,size_t n) {
    uint8_t *d=dst; const uint8_t *s=src; for(size_t i=0;i<n;i++) d[i]=s[i];
}
static uint32_t tx_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void tx_put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static void tx_put64(uint8_t *p,uint64_t v) {
    for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i));
}
static inline uint64_t tx_u64(const uint8_t *p) {
    return tx_u32(p)|((uint64_t)tx_u32(p+4)<<32);
}
static int tx_write_allowed(uint32_t off) {
    uint32_t base=off&0xffff0000u, r=off&0xffffu;
    if(base!=TX_GSP && base!=TX_SEC) return 0;
    switch(r) {
    case 0x040: case 0x044: case 0x084: case 0x100: case 0x104:
    case 0x10c: case 0x130: case 0x180: case 0x184: case 0x188:
    case 0x1c0: case 0x1c4: case 0x3c0: case 0x610: case 0x624:
        return 1;
    default: return 0;
    }
}
#define TX_TRY(call) do { int tx_status=(call); if(tx_status) return tx_status; } while(0)
static int tx_read(Tx *t,uint32_t off,uint32_t *v) {
    if(t->read(t->opaque,off,v)) return TX_IO;
    if(*v==0xffffffffu || (*v&0xffff0000u)==0xbadf0000u) return TX_VERIFY;
    return TX_OK;
}
static int tx_write(Tx *t,uint32_t off,uint32_t v) {
    if(!tx_write_allowed(off)) return TX_GUARD;
    t->mutated=1; /* A failed write may still have reached hardware. */
    return t->write(t->opaque,off,v) ? TX_IO:TX_OK;
}
static int tx_event(Tx *t,unsigned stage,uint32_t a,uint32_t b) {
    t->stage=stage;
    return t->event(t->opaque,stage,a,b) ? TX_LOG:TX_OK;
}
static int tx_delay(Tx *t,uint32_t us) {
    return t->delay(t->opaque,us) ? TX_IO:TX_OK;
}
static int tx_barrier(Tx *t) { return (t->barrier)(t->opaque) ? TX_IO:TX_OK; }

/* Native descriptor words, independently parsed from THIS card's PROM by
 * tools/rom_analyze.py and pinned in the profile. */
static const uint32_t tx_desc[15]=KOH_DESC_WORDS;
#define TX_DESC_IFACE tx_desc[4]       /* interface offset (Leadtek: 0xe0) */
#define TX_DESC_IMEM_LOAD tx_desc[6]   /* full IMEM load size */
#define TX_DESC_SEC_BASE tx_desc[8]
#define TX_DESC_SEC_SIZE tx_desc[9]
#define TX_DESC_DMEM_OFF tx_desc[10]   /* == IMEM load size for known images */
#define TX_DESC_DMEM_SIZE tx_desc[12]
/* GSP IMEM blocks needed to host FWSEC plus the two top blocks of the
 * generic loader: derived, not guessed. */
#define TX_MIN_IMEM_BLOCKS (((tx_desc[6]+0xffu)>>8)+2u)
static int tx_prepare(Tx *t,const uint8_t *booter,const uint8_t *signature,const uint8_t *boot) {
    if(!t->address || !t->pool || !t->native_fwsec || !t->generic_bl || !booter || !signature || !boot ||
       t->device_address<0x100000000ULL || (t->device_address&4095) ||
       t->device_address>0x10000000000ULL-TX_POOL_SIZE) return TX_GUARD;
    if(!TX_FWSEC_SIZE || TX_FWSEC_SIZE!=tx_desc[1] || tx_desc[1]!=tx_desc[2] || tx_desc[3] ||
       TX_FWSEC_SIZE>TX_BOOTER_OFF) return TX_GUARD;
    if(!tx_desc[6] || tx_desc[10]!=tx_desc[6] ||
       TX_DESC_IFACE>tx_desc[6] || TX_MIN_IMEM_BLOCKS>0x100u) return TX_GUARD;
    const uint8_t *d=t->native_fwsec+TX_DESC_IMEM_LOAD;
    /* This fixed-card profile rejects a different mapper, not merely a size. */
    if(TX_DESC_IFACE+0x10>tx_desc[6] ||
       tx_u32(d+TX_DESC_IFACE)!=KOH_IFACE_WORD || tx_u32(d+TX_DESC_IFACE+4)!=4 ||
       tx_u32(d+TX_DESC_IFACE+8)!=KOH_MAPPER_OFF ||
       tx_u32(d+KOH_MAPPER_OFF)!=0x50414d44 || tx_u32(d+KOH_MAPPER_OFF+4)!=0x00400003 ||
       tx_u32(d+KOH_MAPPER_OFF+8)!=KOH_CMD_OFF || tx_u32(d+KOH_MAPPER_OFF+12)!=KOH_CMD_SIZE)
        return TX_GUARD;
    tx_zero(t->pool,TX_POOL_SIZE);
    tx_copy(t->pool+TX_FWSEC_OFF,t->native_fwsec,TX_FWSEC_SIZE);
    tx_copy(t->pool+TX_BOOTER_OFF,booter,TX_BOOTER_SIZE);
    tx_copy(t->pool+TX_SIGNATURE_OFF,signature,TX_SIGNATURE_SIZE);
    tx_copy(t->pool+TX_BOOT_OFF,boot,TX_BOOT_SIZE);
    static const uint8_t sig[16]={0x32,0x60,0x43,0x67,0x98,0x89,0x7b,0x06,
                                  0xe8,0x5f,0x1a,0xa8,0x71,0x43,0x5d,0xcb};
    tx_copy(t->pool+TX_BOOTER_OFF+0x8700,sig,16);
    uint8_t *pd=t->pool+TX_FWSEC_OFF+TX_DESC_IMEM_LOAD;
    const uint32_t cmd[12]={1,24,0,0,0,2,1,20,(uint32_t)(TX_FRTS>>12),0x100,2,0};
    for(unsigned i=0;i<12;i++) tx_put32(pd+KOH_CMD_OFF+4*i,cmd[i]);
    tx_put32(pd+KOH_MAPPER_OFF+44,0x15);
    /* TU104 heap: 8 MiB TU10X base + ceil(96 KiB/GiB x FB GiB) + 96 MiB
     * clients (open-gpu-kernel-modules gsp_fw_heap.h), within the 256 MiB
     * pre-scrubbed top region for this image.
     * Dummy image/Booter combination is pinned to the upstream experiment;
     * it is NOT a replacement GSP-RM image and must never be started as one.
     */
    uint64_t m[32]={0}, mb=0x100000ULL;
    m[0]=0xdc3aae21371a60b3ULL; m[1]=1;
    m[2]=t->address(t->opaque,TX_RADIX_OFF); m[3]=TX_ELF_SIZE;
    m[4]=t->address(t->opaque,TX_BOOT_OFF); m[5]=TX_BOOT_SIZE;
    m[6]=0x1800; m[7]=0x800; m[8]=0;
    m[9]=t->address(t->opaque,TX_SIGNATURE_OFF); m[10]=TX_SIGNATURE_SIZE;
    m[22]=TX_FB_SIZE; m[23]=TX_FB_SIZE-0x100000; m[24]=0x100000;
    m[21]=m[23]; m[20]=mb; m[19]=m[21]-mb;
    m[18]=(m[19]-m[5])&~0xfffULL;
    m[17]=(m[18]-m[3])&~0xffffULL;
    m[16]=KOH_HEAP_MIB*mb; m[15]=(m[17]-m[16])&~(mb-1);
    m[14]=m[15]-mb; m[13]=mb; m[12]=m[14]-mb; m[11]=m[12];
    if(!KOH_HEAP_MIB || m[19]!=TX_FRTS || TX_FB_SIZE-m[11]>256*mb ||
       m[15]+m[16]>m[17]) return TX_GUARD;
    for(unsigned i=0;i<32;i++) tx_put64(t->pool+TX_META_OFF+8*i,m[i]);
    t->pool[TX_META_OFF+241]=1; /* flags: CLOCK_BOOST, same structure offset */
    uint64_t pages=(TX_ELF_SIZE+4095ULL)>>12, l2=(pages+511)>>9;
    if(TX_RADIX_OFF+(2+l2)*4096>TX_DATA_OFF || TX_DATA_OFF+pages*4096>TX_POOL_SIZE) return TX_GUARD;
    tx_put64(t->pool+TX_RADIX_OFF,t->address(t->opaque,TX_RADIX_OFF+0x1000));
    for(uint64_t i=0;i<l2;i++)
        tx_put64(t->pool+TX_RADIX_OFF+0x1000+8*i,t->address(t->opaque,TX_RADIX_OFF+0x2000+4096*i));
    for(uint64_t i=0;i<pages;i++)
        tx_put64(t->pool+TX_RADIX_OFF+0x2000+8*i,t->address(t->opaque,TX_DATA_OFF+4096*i));
    return TX_OK;
}
static int tx_reset(Tx *t,uint32_t base) {
    uint32_t v;
    /* Turing PreResetWait is a no-op, NOT Ampere's HWCFG2.RESET_READY poll.
     * Turing has no explicit Falcon/RISC-V core-select write either. */
    TX_TRY(tx_write(t,base+0x3c0,1));
    for(unsigned i=0;i<16;i++) { TX_TRY(tx_read(t,base+0x3c0,&v)); if(!(v&1)) return TX_VERIFY; }
    TX_TRY(tx_delay(t,50));
    TX_TRY(tx_write(t,base+0x3c0,0));
    TX_TRY(tx_read(t,base+0x3c0,&v)); if(v&1) return TX_VERIFY;
    for(unsigned i=0;i<3000;i++) {
        TX_TRY(tx_read(t,base+0x10c,&v));
        if(!(v&6)) { TX_TRY(tx_write(t,base+0x084,KOH_BOOT0_EXPECT)); return TX_OK; }
        TX_TRY(tx_delay(t,1000));
    }
    return TX_TIMEOUT;
}
static int tx_ctx(Tx *t,uint32_t base) {
    uint32_t v; TX_TRY(tx_read(t,base+0x624,&v));
    TX_TRY(tx_write(t,base+0x624,v|0x80));
    return tx_write(t,base+0x10c,0);
}
static int tx_dmem(Tx *t,uint32_t base,const uint8_t *src,uint32_t bytes) {
    if(bytes&3) return TX_GUARD;
    TX_TRY(tx_write(t,base+0x1c0,1u<<24));
    for(uint32_t i=0;i<bytes;i+=4) TX_TRY(tx_write(t,base+0x1c4,tx_u32(src+i)));
    return TX_OK;
}
static int tx_imem(Tx *t,uint32_t base,uint32_t dest,const uint8_t *src,
                   uint32_t bytes,uint32_t tag,int secure) {
    if((dest&255)||(bytes&3)||(tag&255)) return TX_GUARD;
    TX_TRY(tx_write(t,base+0x180,dest|(1u<<24)|(secure?(1u<<28):0)));
    for(uint32_t i=0;i<bytes;i+=4) {
        if(!(i&255)) TX_TRY(tx_write(t,base+0x188,(tag+i)>>8));
        TX_TRY(tx_write(t,base+0x184,tx_u32(src+i)));
    }
    return TX_OK;
}
static int tx_start(Tx *t,uint32_t base) {
    uint32_t v; TX_TRY(tx_read(t,base+0x100,&v));
    TX_TRY(tx_barrier(t));
    return tx_write(t,base+((v&0x40)?0x130:0x100),2);
}
static int tx_halt(Tx *t,uint32_t base) {
    uint32_t v;
    for(unsigned i=0;i<5000;i++) {
        TX_TRY(tx_read(t,base+0x100,&v));
        if(v&0x10) return TX_OK;
        TX_TRY(tx_delay(t,1000));
    }
    return TX_TIMEOUT;
}
static int tx_guard_note(Tx *t,uint32_t off,uint32_t want,uint32_t actual,uint32_t mask) {
    return t->guard_note && t->guard_note(t->opaque,off,want,actual,mask)?TX_LOG:TX_OK;
}
static int tx_observed_pair(uint32_t ss0,uint32_t ss1) {
    /* Pre-driver diagnostic observed tuples only; everything else refuses. */
    return ss0==KOH_SELECTOR0_INIT && ss1==KOH_SELECTOR1_INIT;
}
static int tx_guards(Tx *t) {
    static const uint32_t off[]={0,0x409650,0x409664,0x40966c,0x625f04,
        0x1fa824,0x1fa828,0x118234,0x409660,0x409668,0x110100,0x840100};
    static const uint32_t want[]={KOH_BOOT0_EXPECT,0xffffff8f,KOH_SELECTOR0_INIT,
        KOH_SELECTOR1_INIT,KOH_WORKSPACE_REG,KOH_WPR2_LOW_INIT,KOH_WPR2_HIGH_INIT,
        KOH_GFW_WORD,KOH_FEATURE_WORD,KOH_READOUT_WORD,0x10,0x10};
    uint32_t ss0=0,ss1=0,v=0;
    const uint32_t fwsec_words=TX_FWSEC_SIZE/4;
    if(!t->native_fwsec || t->mutated) return TX_GUARD;
    if(!KOH_PROM_DESC_OFF || !KOH_PROM_FWSEC_OFF || !fwsec_words || TX_FWSEC_SIZE&3) return TX_GUARD;
    TX_TRY(tx_event(t,TX_STAGE_GUARDS,0,0));
    for(unsigned i=0;i<sizeof(off)/sizeof(off[0]);i++) {
        v=0;
        int r=tx_read(t,off[i],&v);
        uint32_t mask=(i==2 || i==3)?0:i==7?0x3ff:0xffffffffu;
        TX_TRY(tx_guard_note(t,off[i],want[i],v,mask));
        if(r) return r;
        if((v&mask)!=(want[i]&mask)) return TX_GUARD;
        if(i==2) ss0=v;
        if(i==3) {
            ss1=v;
            if(!tx_observed_pair(ss0,ss1)) return TX_GUARD;
            if(t->baseline_valid && (ss0!=t->initial_ss0 || ss1!=t->initial_ss1))
                return TX_GUARD;
        }
    }
    for(unsigned i=0;i<15;i++) {
        uint32_t at=0x300000+KOH_PROM_DESC_OFF+4*i;
        v=0;int r=t->read(t->opaque,at,&v);
        TX_TRY(tx_guard_note(t,at,tx_desc[i],v,0xffffffffu));
        if(r) return TX_IO;
        if(v!=tx_desc[i]) return TX_GUARD;
    }
    for(unsigned i=0;i<fwsec_words;i++) {
        /* ROM data may legitimately be FFFFFFFF or resemble a BADF code.
         * Compare raw bytes; only control-register reads use that sentinel. */
        uint32_t at=0x300000+KOH_PROM_FWSEC_OFF+4*i,expected=tx_u32(t->native_fwsec+4*i);
        v=0;int r=t->read(t->opaque,at,&v);
        if(r || v!=expected) {
            TX_TRY(tx_guard_note(t,at,expected,v,0xffffffffu));
            return r?TX_IO:TX_GUARD;
        }
    }
    t->initial_ss0=ss0;t->initial_ss1=ss1;t->baseline_valid=1;
    return TX_OK;
}
static int tx_run(Tx *t) {
    uint32_t v,lo,hi,cpu;
    TX_TRY(tx_guards(t));
    TX_TRY(tx_event(t,TX_STAGE_FWSEC_RESET,0,0));
    TX_TRY(tx_reset(t,TX_GSP));
    TX_TRY(tx_event(t,TX_STAGE_FWSEC_LOAD,0,0));
    TX_TRY(tx_ctx(t,TX_GSP));
    TX_TRY(tx_read(t,TX_GSP+0x610,&v));
    TX_TRY(tx_write(t,TX_GSP+0x610,(v&~0x1fu)|0x15));
    uint8_t desc[0x54]={0}; uint64_t fw=t->address(t->opaque,TX_FWSEC_OFF);
    tx_put32(desc+0x20,4); tx_put64(desc+0x24,fw);
    tx_put32(desc+0x2c,0);
    tx_put32(desc+0x30,TX_DESC_SEC_BASE); tx_put32(desc+0x34,TX_DESC_SEC_BASE);
    tx_put32(desc+0x38,TX_DESC_SEC_SIZE);
    tx_put64(desc+0x40,fw+TX_DESC_DMEM_OFF); tx_put32(desc+0x48,TX_DESC_DMEM_SIZE);
    TX_TRY(tx_dmem(t,TX_GSP,desc,sizeof(desc)));
    TX_TRY(tx_read(t,TX_GSP+0x108,&v));
    uint32_t blocks=v&0x1ff;
    if(blocks<TX_MIN_IMEM_BLOCKS || blocks>0x100) return TX_VERIFY;
    uint32_t dest=(blocks-2)<<8;
    TX_TRY(tx_imem(t,TX_GSP,dest,t->generic_bl,0x200,0xfd00,0));
    TX_TRY(tx_write(t,TX_GSP+0x180,dest));
    TX_TRY(tx_read(t,TX_GSP+0x184,&v)); if(v!=tx_u32(t->generic_bl)) return TX_VERIFY;
    TX_TRY(tx_write(t,TX_GSP+0x1c0,0x20));
    TX_TRY(tx_read(t,TX_GSP+0x1c4,&v)); if(v!=4) return TX_VERIFY;
    TX_TRY(tx_write(t,TX_GSP+0x104,0xfd00));
    TX_TRY(tx_event(t,TX_STAGE_FWSEC_START,0xfd00,(uint32_t)(TX_FRTS>>12)));
    TX_TRY(tx_start(t,TX_GSP));
    TX_TRY(tx_delay(t,100000));
    TX_TRY(tx_halt(t,TX_GSP));
    TX_TRY(tx_read(t,0x1438,&v)); /* VBIOS scratch 0e, FRTS error in high16 */
    TX_TRY(tx_read(t,0x1fa824,&lo)); TX_TRY(tx_read(t,0x1fa828,&hi));
    TX_TRY(tx_event(t,TX_STAGE_FWSEC_RESULT,lo,hi));
    if((v>>16) || (lo&~15u)!=(uint32_t)(TX_FRTS>>8) ||
       (hi&~15u)!=(uint32_t)((TX_FRTS+0xe0000)>>8)) return TX_VERIFY;

    TX_TRY(tx_event(t,TX_STAGE_BOOTER_RESET,0,0));
    TX_TRY(tx_reset(t,TX_GSP)); /* leaves RISC-V ready, but NEVER starts dummy RM */
    TX_TRY(tx_reset(t,TX_SEC));
    /* Both firmware engines reset before ending FWSEC's input mapping.
     * Failure here leaves mutated state: platform MUST retain DMA backing. */
    if(t->next_input && t->next_input(t->opaque)) return TX_IO;
    TX_TRY(tx_event(t,TX_STAGE_BOOTER_LOAD,0,0));
    TX_TRY(tx_ctx(t,TX_SEC));
    const uint8_t *b=t->pool+TX_BOOTER_OFF;
    TX_TRY(tx_imem(t,TX_SEC,0,b,0x100,0,0));
    TX_TRY(tx_imem(t,TX_SEC,0x100,b+0x100,0x8400,0x100,1));
    TX_TRY(tx_dmem(t,TX_SEC,b+0x8500,0x6200));
    TX_TRY(tx_write(t,TX_SEC+0x180,0));
    TX_TRY(tx_read(t,TX_SEC+0x184,&v)); if(v!=tx_u32(b)) return TX_VERIFY;
    TX_TRY(tx_write(t,TX_SEC+0x1c0,0x200));
    TX_TRY(tx_read(t,TX_SEC+0x1c4,&v)); if(v!=tx_u32(b+0x8700)) return TX_VERIFY;
    uint64_t meta=t->address(t->opaque,TX_META_OFF);
    TX_TRY(tx_write(t,TX_SEC+0x104,0));
    TX_TRY(tx_write(t,TX_SEC+0x040,(uint32_t)meta));
    TX_TRY(tx_write(t,TX_SEC+0x044,(uint32_t)(meta>>32)));
    TX_TRY(tx_event(t,TX_STAGE_BOOTER_START,(uint32_t)meta,(uint32_t)(meta>>32)));
    TX_TRY(tx_start(t,TX_SEC));
    TX_TRY(tx_delay(t,3000000));
    TX_TRY(tx_halt(t,TX_SEC));
    TX_TRY(tx_read(t,TX_SEC+0x040,&v));
    TX_TRY(tx_read(t,0x409664,&lo)); TX_TRY(tx_read(t,0x40966c,&hi));
    TX_TRY(tx_event(t,TX_STAGE_BOOTER_RESULT,lo,hi));
    if(v || lo!=0x88888888 || hi!=8) return TX_VERIFY;
    TX_TRY(tx_read(t,0x409650,&v)); if(v!=0xffffff8f) return TX_VERIFY;
    TX_TRY(tx_read(t,0x1fa824,&lo)); TX_TRY(tx_read(t,0x1fa828,&hi));
    if(lo!=KOH_WPR2_LOW_INIT || hi!=KOH_WPR2_HIGH_INIT) return TX_VERIFY;
    t->unlocked=1;
    TX_TRY(tx_event(t,TX_STAGE_CLEANUP,0,0));
    TX_TRY(tx_reset(t,TX_SEC)); TX_TRY(tx_reset(t,TX_GSP));
    TX_TRY(tx_write(t,TX_SEC+0x040,0)); TX_TRY(tx_write(t,TX_SEC+0x044,0));
    TX_TRY(tx_read(t,TX_SEC+0x100,&cpu)); if(cpu&2) return TX_VERIFY;
    TX_TRY(tx_read(t,TX_GSP+0x100,&cpu)); if(cpu&2) return TX_VERIFY;
    TX_TRY(tx_read(t,0x409664,&lo)); TX_TRY(tx_read(t,0x40966c,&hi));
    if(lo!=0x88888888 || hi!=8) return TX_VERIFY;
    t->quiescent=1;
    return tx_event(t,TX_STAGE_DONE,lo,hi);
}
#undef TX_TRY
#endif
