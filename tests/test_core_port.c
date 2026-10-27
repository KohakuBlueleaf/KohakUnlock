/* Pure host tests. Uses saved blob files and a simulated register bank ONLY. */
#define TX_EXPERIMENT_V4
#include "kernel/tu102_core_port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t regs[2][0x700/4], imem[2][0x10000/4], dmem[2][0x10000/4];
    uint32_t ic[2],dc[2],ss0,ss1,plm,lo,hi,scratch;
    uint32_t boot0,workspace;
    unsigned reads,writes,fail_read,fail_write,fail_event,fw_starts,booter_starts;
    unsigned write_at_stage[16],mode;
    uint64_t delayed;
    int alias,rom_bad,reset_stuck,barrier_fail,phase_fail;
    unsigned phase_calls;
    const uint8_t *native;
    Tx *tx;
#ifdef TX_EXPERIMENT_V4
    uint32_t feature,readout,gfw;
    unsigned guard_notes,fail_guard_note;
    uint32_t selector_actual[2],selector_expected[2];
    unsigned selector_seen;
    int bad_descriptor;
#endif
} Model;
static uint8_t native[TX_FWSEC_SIZE],generic[TX_GENERIC_SIZE],booter[TX_BOOTER_SIZE];
static uint8_t signature[TX_SIGNATURE_SIZE],boot[TX_BOOT_CONTAINER_SIZE];
static Model model;
static Tx tx;
static unsigned cases;
static uint64_t model_address(void *ctx,uint32_t off);

/* Independently transcribed native layout, rather than emulating whatever
 * offsets the implementation happens to request. No firmware is executed. */
#ifdef TX_PROFILE_NVIDIA_1E07_12A4
enum { PROM_DESC=0x33f200, PROM_IMAGE=0x33f23c, NATIVE_DMEM=0x9600,
       MAPPER_PATCH=0x99ac, COMMAND_PATCH=0x99d0, MIN_BLOCKS=0x98 };
static const uint32_t observed_desc[15]={0x3c0201,0x9a10,0x9a10,0,0xdc,0,0x9600,0,
    0x300,0x9300,0x9600,0,0x410,0x9600,0x6618};
static const uint32_t observed_pair[2]={0x500060,5};
#else
enum { PROM_DESC=0x33ed98, PROM_IMAGE=0x33edd4, NATIVE_DMEM=0x9800,
       MAPPER_PATCH=0x9bbc, COMMAND_PATCH=0x9be0, MIN_BLOCKS=0x9a };
static const uint32_t observed_desc[15]={0x3c0201,0x9c20,0x9c20,0,0xe0,0,0x9800,0,
    0x400,0x9400,0x9800,0,0x420,0x9800,0x6638};
static const uint32_t observed_pair[2]={0,0};
#endif

static int model_read(void *ctx,uint32_t off,uint32_t *v) {
    Model *m=ctx;
    if(++m->reads==m->fail_read) return 1;
    if(off>=PROM_IMAGE && off<PROM_IMAGE+TX_FWSEC_SIZE) {
        *v=tx_u32(m->native+off-PROM_IMAGE)^(m->rom_bad?1:0); return 0;
    }
    if(off>=PROM_DESC && off<PROM_IMAGE) {
        *v=observed_desc[(off-PROM_DESC)/4];
#ifdef TX_EXPERIMENT_V4
        if(m->bad_descriptor)*v^=1;
#endif
        return 0;
    }
    switch(off) {
    case 0:*v=m->boot0;return 0;
    case 0x409650:*v=m->plm;return 0;
    case 0x409664:*v=m->ss0;return 0;
    case 0x40966c:*v=m->ss1;return 0;
    case 0x625f04:*v=m->workspace;return 0;
    case 0x1fa824:*v=m->lo;return 0;
    case 0x1fa828:*v=m->hi;return 0;
#ifdef TX_EXPERIMENT_V4
    case 0x409660:*v=m->feature;return 0;
    case 0x409668:*v=m->readout;return 0;
    case 0x118234:*v=m->gfw;return 0;
#else
    case 0x118234:*v=0x3ff;return 0;
#endif
    case 0x1438:*v=m->scratch;return 0;
    }
    unsigned bank=(off&0xffff0000u)==TX_SEC;
    assert((off&0xffff0000u)==TX_GSP || bank);
    uint32_t r=off&0xffffu; assert(r<0x700);
    if(r==0x184) *v=m->imem[bank][(m->ic[bank]&0xffff)/4];
    else if(r==0x1c4) *v=m->dmem[bank][(m->dc[bank]&0xffff)/4];
    else if(r==0x10c && m->reset_stuck) *v=6;
    else *v=m->regs[bank][r/4];
    return 0;
}
static int model_write(void *ctx,uint32_t off,uint32_t v) {
    Model *m=ctx; assert(tx_write_allowed(off));
    /* Forbidden addresses include the historical hang-prone RESET_PLM read
     * target and all host-side WPR/feature-selector/display/flash registers. */
    assert(off!=0x8403c4 && off!=0x1fa824 && off!=0x1fa828 && off!=0x409664);
    if(++m->writes==m->fail_write) return 1;
    unsigned b=(off&0xffff0000u)==TX_SEC; uint32_t r=off&0xffffu;
    m->regs[b][r/4]=v;
    if(r==0x180) m->ic[b]=v;
    if(r==0x1c0) m->dc[b]=v;
    if(r==0x184) {
        m->imem[b][(m->ic[b]&0xffff)/4]=v;
        if(m->ic[b]&(1u<<24)) m->ic[b]+=4;
    }
    if(r==0x1c4) {
        m->dmem[b][(m->dc[b]&0xffff)/4]=v;
        if(m->dc[b]&(1u<<24)) m->dc[b]+=4;
    }
    if(r==0x3c0 && v==1) m->regs[b][0x100/4]=m->alias?0x40:0;
    if((r==0x100 || r==0x130) && v==2) {
        assert(r==(m->alias?0x130u:0x100u));
        if(!b) {
            m->fw_starts++;
            assert(m->dmem[0][0x20/4]==4);
            assert(tx_u64((uint8_t *)m->dmem[0]+0x24)==model_address(m,TX_FWSEC_OFF));
            assert(!m->dmem[0][0x2c/4] && !m->dmem[0][0x3c/4]);
            assert(m->dmem[0][0x30/4]==observed_desc[8] &&
                   m->dmem[0][0x34/4]==observed_desc[8] &&
                   m->dmem[0][0x38/4]==observed_desc[9]);
            assert(m->dmem[0][0x48/4]==observed_desc[12]);
            assert(tx_u64((uint8_t *)m->dmem[0]+0x40)==
                   model_address(m,TX_FWSEC_OFF)+NATIVE_DMEM);
            assert(tx_u32(m->tx->pool+MAPPER_PATCH)==0x15);
            assert(tx_u32(m->tx->pool+COMMAND_PATCH+0x20)==0x57fe00);
            m->regs[0][0x100/4]=m->mode==3?0:0x10;
            m->lo=0x57fe000; m->hi=0x57fee00;
            if(m->mode==1) m->scratch=0x10000;
            if(m->mode==2) m->lo=0x27fe000;
            if(m->mode==4) m->hi=0;
        } else {
            assert(m->fw_starts==1 && m->lo==0x57fe000);
            m->booter_starts++;
            assert(tx_u64(m->tx->pool+TX_META_OFF+22*8)==TX_FB_SIZE);
            assert(tx_u64(m->tx->pool+TX_META_OFF+16*8)==107ULL*0x100000);
            m->regs[1][0x100/4]=m->mode==7?0:0x10;
            m->regs[1][0x040/4]=m->mode==5?0x91:0;
            m->ss0=m->mode==6?0x02201054:0x88888888;
            m->ss1=8; m->lo=0x1ffffe00; m->hi=0;
        }
    }
    return 0;
}
static int model_delay(void *ctx,uint32_t us) {
    Model *m=ctx; m->delayed+=us; assert(m->delayed<20000000); return 0;
}
static int model_barrier(void *ctx) { return ((Model *)ctx)->barrier_fail; }
#ifdef TX_EXPERIMENT_V4
static int model_guard_note(void *ctx,uint32_t off,uint32_t want,uint32_t actual,uint32_t mask) {
    Model *m=ctx;
    if((off==0x409664 || off==0x40966c) && mask==0xffffffffu) {
        unsigned word=off==0x40966c;
        m->selector_actual[word]=actual;m->selector_expected[word]=want;
        m->selector_seen|=1u<<word;
    }
    return ++m->guard_notes==m->fail_guard_note;
}
#endif
static int model_event(void *ctx,unsigned stage,uint32_t a,uint32_t b) {
    (void)a;(void)b; Model *m=ctx; m->write_at_stage[stage]=m->writes;
    return stage==m->fail_event;
}
static int model_next_input(void *ctx) {
    Model *m=ctx;
    assert(m->tx->stage==TX_STAGE_BOOTER_RESET && m->tx->mutated);
    assert(m->fw_starts==1 && !m->booter_starts);
    assert(!(m->regs[0][0x100/4]&2) && !(m->regs[1][0x100/4]&2));
    m->phase_calls++;
    return m->phase_fail;
}
static uint64_t model_address(void *ctx, uint32_t off) {
    Model *m=ctx;
    assert(off<TX_POOL_SIZE);
    return m->tx->device_address+(uint64_t)(off/65536)*131072+off%65536;
}
static void setup(void) {
    uint8_t *pool=tx.pool;
    memset(&model,0,sizeof(model)); memset(&tx,0,sizeof(tx));
    tx.pool=pool; tx.address=model_address; tx.device_address=0x800000000ULL; tx.opaque=&model;
    tx.read=model_read;tx.write=model_write;tx.delay=model_delay;
    tx.barrier=model_barrier;tx.event=model_event;tx.native_fwsec=native;tx.generic_bl=generic;
    model.tx=&tx; model.native=native; model.boot0=0x162000a1; model.workspace=1;
    model.ss0=observed_pair[0];model.ss1=observed_pair[1];
    model.plm=0xffffff8f;model.lo=0x1ffffe00;
    model.regs[0][0x108/4]=0x100; model.regs[1][0x108/4]=0x100;
#ifdef TX_EXPERIMENT_V4
    model.feature=0x2f3;model.readout=0x00400080;model.gfw=0x3ff;
    model.regs[0][0x100/4]=model.regs[1][0x100/4]=0x10;
    tx.guard_note=model_guard_note;
#endif
    assert(tx_prepare(&tx,booter,signature,boot)==TX_OK);
}
static void load(const char *name,uint8_t *data,size_t bytes) {
    FILE *f=fopen(name,"rb");assert(f);
    assert(fread(data,1,bytes,f)==bytes && fgetc(f)==EOF); assert(!fclose(f));
}
static void metadata_test(void) {
    setup(); const uint8_t *m=tx.pool+TX_META_OFF;
    assert(tx_u64(m)==0xdc3aae21371a60b3ULL);
    assert(tx_u64(m+19*8)==TX_FRTS && tx_u64(m+24*8)==0x100000);
    assert(m[241]==1 && !m[240] && !m[242]);
    assert(tx_u64(m+31*8)==0);
    /* TU102 FWSEC FRTS ABI from the pinned NVIDIA source, including padding. */
    const uint32_t expected_cmd[12]={1,24,0,0,0,2,1,20,0x57fe00,0x100,2,0};
    for(unsigned i=0;i<12;i++)
        assert(tx_u32(tx.pool+COMMAND_PATCH+4*i)==expected_cmd[i]);
    for(size_t i=0;i<NATIVE_DMEM;i++) assert(tx.pool[i]==native[i]);
    for(size_t i=NATIVE_DMEM;i<TX_FWSEC_SIZE;i++)
        if(tx.pool[i]!=native[i]) assert((i>=MAPPER_PATCH&&i<MAPPER_PATCH+4)||
                                       (i>=COMMAND_PATCH&&i<COMMAND_PATCH+48));
    assert(!memcmp(tx.pool+TX_SIGNATURE_OFF,signature,TX_SIGNATURE_SIZE));
    uint64_t root=tx_u64(tx.pool+TX_RADIX_OFF), l2=tx_u64(tx.pool+TX_RADIX_OFF+0x1000);
    assert(root==model_address(&model,TX_RADIX_OFF+0x1000));
    assert(l2==model_address(&model,TX_RADIX_OFF+0x2000));
    uint64_t pages=(TX_ELF_SIZE+4095ULL)>>12;
    for(uint64_t i=0;i<pages;i++) assert(tx_u64(tx.pool+TX_RADIX_OFF+0x2000+8*i)==model_address(&model,TX_DATA_OFF+4096*i));
    assert(tx_u64(tx.pool+TX_RADIX_OFF+0x2000+8*pages)==0);
    cases++;
}
static void asset(const char *dir,const char *name,uint8_t *data,size_t size) {
    char path[4096];
    int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    assert(n>0 && (size_t)n<sizeof(path));
    load(path,data,size);
}
static void synthetic_inputs(void) {
    /* Firmware-free fixtures for exercising both compiled profiles in CI.
     * All remaining bytes are zero; these are never executable packages. */
    uint8_t *d=native+NATIVE_DMEM;
    tx_put32(d+observed_desc[4],0x02080401);
    tx_put32(d+observed_desc[4]+4,4);
    tx_put32(d+observed_desc[4]+8,MAPPER_PATCH-NATIVE_DMEM-44);
    uint8_t *mapper=native+MAPPER_PATCH-44;
    tx_put32(mapper,0x50414d44);tx_put32(mapper+4,0x00400003);
    tx_put32(mapper+8,COMMAND_PATCH-NATIVE_DMEM);tx_put32(mapper+12,64);
}
int main(int argc,char **argv) {
    if(argc!=2) { fprintf(stderr,"usage: %s VERIFIED_ASSET_DIRECTORY\n",argv[0]); return 2; }
    int synthetic=!strcmp(argv[1],"--synthetic");
    if(synthetic) synthetic_inputs();
    else {
        asset(argv[1],TX_NATIVE_NAME,native,sizeof(native));
        asset(argv[1],"bl_gsp_tu102.bin",generic,sizeof(generic));
        asset(argv[1],"booter_ucode_prod.bin",booter,sizeof(booter));
        asset(argv[1],"v67_payload.bin",signature,sizeof(signature));
        asset(argv[1],"gsp_rm_boot_dbg.bin",boot,sizeof(boot));
    }
    tx.pool=malloc(TX_POOL_SIZE); assert(tx.pool);
    metadata_test();
    const unsigned native_checks[]={NATIVE_DMEM+observed_desc[4],
        NATIVE_DMEM+observed_desc[4]+4,NATIVE_DMEM+observed_desc[4]+8,
        MAPPER_PATCH-44,MAPPER_PATCH-40,MAPPER_PATCH-36,MAPPER_PATCH-32};
    for(unsigned i=0;i<sizeof(native_checks)/sizeof(native_checks[0]);i++) {
        setup();unsigned off=native_checks[i];native[off]^=1;
        tx.pool[0]=0xa5;
        assert(tx_prepare(&tx,booter,signature,boot)==TX_GUARD);
        assert(tx.pool[0]==0xa5 && !model.writes);native[off]^=1;cases++;
    }
    setup();assert(tx_run(&tx)==TX_OK && tx.unlocked && tx.quiescent);
    assert(model.fw_starts==1 && model.booter_starts==1); cases++;
    unsigned write_total=model.writes;
    unsigned boundaries[16];memcpy(boundaries,model.write_at_stage,sizeof(boundaries));
    setup();model.alias=1;assert(tx_run(&tx)==TX_OK);cases++;
    setup();tx.next_input=model_next_input;
    uint8_t *snapshot=malloc(TX_POOL_SIZE);assert(snapshot);memcpy(snapshot,tx.pool,TX_POOL_SIZE);
    assert(tx_run(&tx)==TX_OK && model.phase_calls==1);
    assert(!memcmp(snapshot,tx.pool,TX_POOL_SIZE));free(snapshot);cases++;
    setup();tx.next_input=model_next_input;model.phase_fail=1;
    assert(tx_run(&tx)==TX_IO && tx.mutated && !tx.quiescent && !model.booter_starts);cases++;
    setup();tx.next_input=model_next_input;model.mode=1;
    assert(tx_run(&tx)==TX_VERIFY && !model.phase_calls && !model.booter_starts);cases++;
    setup();model.boot0^=1;assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.workspace^=0x100;assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.rom_bad=1;assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.plm=0xbadf5620;assert(tx_run(&tx)==TX_VERIFY && !model.writes);cases++;
    for(unsigned mode=1;mode<=7;mode++) {
        setup();model.mode=mode;int r=tx_run(&tx);assert(r && tx.mutated && !tx.quiescent);
        assert(model.booter_starts==(mode>=5?1u:0u));cases++;
    }
    for(unsigned blocks=MIN_BLOCKS-2; blocks<MIN_BLOCKS; blocks++) {
        setup();model.regs[0][0x108/4]=blocks;
        assert(tx_run(&tx)==TX_VERIFY && !model.fw_starts);cases++;
    }
    setup();model.regs[0][0x108/4]=MIN_BLOCKS;
    assert(tx_run(&tx)==TX_OK && tx.quiescent);cases++;
    setup();model.regs[0][0x108/4]=0x101;
    assert(tx_run(&tx)==TX_VERIFY && !model.fw_starts);cases++;
    setup();model.reset_stuck=1;assert(tx_run(&tx)==TX_TIMEOUT && !model.fw_starts);cases++;
    setup();model.barrier_fail=1;assert(tx_run(&tx)==TX_IO && !model.fw_starts);cases++;
    for(unsigned stage=1;stage<=TX_STAGE_DONE;stage++) {
        setup();model.fail_event=stage;assert(tx_run(&tx)==TX_LOG);
        assert(model.writes==model.write_at_stage[stage]);cases++;
    }
    for(unsigned stage=TX_STAGE_FWSEC_RESET;stage<=TX_STAGE_CLEANUP;stage++) {
        for(unsigned delta=1;delta<=2;delta++) {
            setup();model.fail_write=boundaries[stage]+delta;
            if(model.fail_write>write_total) continue;
            assert(tx_run(&tx)==TX_IO && model.writes==model.fail_write && tx.mutated);
            cases++;
        }
    }
    const unsigned read_failures[]={1,4,23,100,9800,9890,9900};
    for(unsigned i=0;i<sizeof(read_failures)/sizeof(read_failures[0]);i++) {
        setup();model.fail_read=read_failures[i];assert(tx_run(&tx)==TX_IO);cases++;
    }
    setup();tx.device_address=0xff000000;assert(tx_prepare(&tx,booter,signature,boot)==TX_GUARD);cases++;
    setup();tx.device_address++;assert(tx_prepare(&tx,booter,signature,boot)==TX_GUARD);cases++;
    setup();tx.device_address=0x10000000000ULL-TX_POOL_SIZE;
    assert(tx_prepare(&tx,booter,signature,boot)==TX_OK);cases++;
    setup();tx.device_address=0x10000000000ULL-TX_POOL_SIZE+4096;
    assert(tx_prepare(&tx,booter,signature,boot)==TX_GUARD);cases++;
    setup();assert(tx_write(&tx,0x409664,0x88888888)==TX_GUARD && !model.writes);cases++;
    setup();assert(tx_write(&tx,0x8403c4,0xff)==TX_GUARD && !model.writes);cases++;
#ifdef TX_EXPERIMENT_V4
    const uint32_t pairs[][2]={
        {observed_pair[0],observed_pair[1]},
#ifdef TX_PROFILE_NVIDIA_1E07_12A4
        {0x20500060,5},
#endif
    };
    const unsigned pair_count=sizeof(pairs)/sizeof(pairs[0]);
    for(unsigned word=0;word<2;word++)for(unsigned fail=0;fail<3;fail++) {
        setup();
        if(word)model.ss1^=1;else model.ss0^=1;
        if(fail)model.fail_guard_note=4+fail;
        assert(tx_guards(&tx)==(fail?TX_LOG:TX_GUARD));
        assert(model.selector_seen==3 && !model.writes && !tx.mutated && !tx.baseline_valid);
        assert(model.selector_actual[0]==model.ss0 && model.selector_actual[1]==model.ss1);
        assert(model.selector_expected[0]==observed_pair[0] && model.selector_expected[1]==observed_pair[1]);
        cases++;
    }
#ifdef TX_PROFILE_NVIDIA_1E07_12A4
    setup();model.ss0=0;model.ss1=0;
#else
    setup();model.ss0=0x20500060;model.ss1=5;
    assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.ss0=0x500060;model.ss1=5;
#endif
    assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    for(unsigned i=0;i<pair_count;i++) {
        setup();model.ss0=pairs[i][0];model.ss1=pairs[i][1];
        assert(tx_guards(&tx)==TX_OK && tx.baseline_valid && !model.writes);
        assert(tx.initial_ss0==model.ss0 && tx.initial_ss1==model.ss1);
        assert(tx_run(&tx)==TX_OK && tx.unlocked && tx.quiescent);cases++;
        for(unsigned j=0;j<pair_count;j++)if(i!=j) {
            setup();model.ss0=pairs[i][0];model.ss1=pairs[i][1];assert(tx_guards(&tx)==TX_OK);
            model.ss0=pairs[j][0];model.ss1=pairs[j][1];
            assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
        }
    }
    /* Every one-bit alteration of each observed pair must either be another
     * exact observed tuple or refuse. No Cartesian-product/wildcard acceptance. */
    for(unsigned p=0;p<pair_count;p++)for(unsigned word=0;word<2;word++)for(unsigned bit=0;bit<32;bit++) {
        setup();model.ss0=pairs[p][0];model.ss1=pairs[p][1];
        if(word)model.ss1^=1u<<bit;else model.ss0^=1u<<bit;
        int known=0;
        for(unsigned q=0;q<pair_count;q++)
            if(model.ss0==pairs[q][0] && model.ss1==pairs[q][1])known=1;
        assert(tx_guards(&tx)==(known?TX_OK:TX_GUARD) && !model.writes);cases++;
    }
    setup();model.ss0=0x02221154;model.ss1=6;
    assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.ss0=0x88888888;model.ss1=8;
    assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    for(unsigned field=0;field<8;field++)for(unsigned bit=0;bit<32;bit++) {
        setup();
        uint32_t *values[]={&model.feature,&model.readout,&model.plm,&model.workspace,
            &model.lo,&model.hi,&model.regs[0][0x100/4],&model.regs[1][0x100/4]};
        *values[field]^=1u<<bit;
        assert(tx_guards(&tx)!=TX_OK && !tx.baseline_valid && !model.writes);cases++;
    }
    for(unsigned i=1;i<=27;i++) {
        setup();model.fail_read=i;
        assert(tx_run(&tx)==TX_IO && !model.writes && !tx.baseline_valid);cases++;
        setup();model.fail_guard_note=i;
        assert(tx_run(&tx)==TX_LOG && !model.writes && !tx.baseline_valid);cases++;
    }
    setup();model.bad_descriptor=1;assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.gfw^=1;assert(tx_run(&tx)==TX_GUARD && !model.writes);cases++;
    setup();model.rom_bad=1;model.fail_guard_note=28;
    assert(tx_run(&tx)==TX_LOG && !model.writes);cases++;
    setup();tx.mutated=1;assert(tx_guards(&tx)==TX_GUARD && !model.reads);cases++;
    setup();tx.native_fwsec=NULL;assert(tx_guards(&tx)==TX_GUARD && !model.reads);cases++;
#endif
    free(tx.pool);
    printf("PASS: %u offline loader cases (%s); metadata/radix bounds, alias CPU start, guards, timeouts, stage and I/O failures. No hardware access.\n",
           cases,synthetic?"synthetic firmware-free fixtures":"private file inputs; hash precheck required");
    return 0;
}
