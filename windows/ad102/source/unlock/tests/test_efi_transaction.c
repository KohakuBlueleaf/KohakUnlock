/* SPDX-License-Identifier: GPL-2.0-only */
/* Real retained core + compatibility layer + UEFI port; only EFI services are fake. */
#include "../port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#ifndef _WIN32
/* glibc: C11 aligned_alloc + free pair. On Windows, _aligned_malloc/_aligned_free
 * are declared by malloc.h and form the matching pair; use them directly. */
#define _aligned_malloc(n,a) aligned_alloc(a,n)
#define _aligned_free(p) free(p)
#endif
#undef EPROTO
#undef EBADMSG
#undef EOVERFLOW
#undef EOPNOTSUPP
#undef ETIMEDOUT
#undef EALREADY
#include "../compat/compat.h"
#include "../vendor/unlock_610.h"
#include "../vendor/prep_610.h"

#ifndef GU_UEFI_PORT
#error This integration test must execute the real UEFI compatibility path.
#endif
#define MAX_BUFFERS 24000
#define MAX_POOLS 26000
#define GSP 0x110000U
#define SEC2 0x840000U
#define SS0 0x82381cU
#define SS1 0x823820U
#define PLM 0x823804U
static const UINT32 expected_targets[]={0x1fa7cc,0x9a0148,0x1fa7c4,0x823804};
static const UINT32 expected_values[]={0xfffff0ff,0xffffffff,0xffffffff,0xffffffff};
enum scenario {
  SUCCESS, FWSEC_TIMEOUT, POST_FLR_TIMEOUT, BME_READBACK_FAIL,
  MMIO_WRITE_FAIL, FLUSH_FAIL, FLR_WRITE_FAIL, UNMAP_FAIL,
  MAP_FAIL, MAP_PARTIAL, RESTORE_FAIL, DISPLAY_CHANGED, SELECTOR_LOST,
  FWSEC_NO_WPR, FWSEC_RUNNING, DIAG_READ_FAIL, POST_DIAG_READ_FAIL,
  PIO_UNAVAILABLE, PIO_UNAVAILABLE_NO_WPR, FWSEC_ERROR_CODE,
  SEC2_DELAYED_START, SEC2_STUCK_HALT, SEC2_RUN_HALT, SEC2_RUN_TIMEOUT,
  SEC2_UNAVAILABLE, SEC2_DIAG_FAIL, SEC2_PROTECTED_DIAG,
  SEC2_RETRY_SUCCESS, SEC2_RETRY_BOTH_FAIL, SEC2_RETRY_RESET_REFUSED,
  SEC2_RETRY_WPR_RESTORE_REFUSED,
  SEC2_STATUS_ERROR_PLM_OPEN,
  SEC2_RETRY_LOCKDOWN_RECOVERY, SEC2_RETRY_SWITCH_REFUSED,
  SEC2_RETRY_BCR_READ_FAIL, SEC2_RETRY_ACCESS_LOCKED,
  PLM_FIRST_FAIL_NEXT_SUCCEED, PLM_SECOND_TARGET_TIMEOUT,
  PLM_PRIOR_RELOCK, PLM_WRONG_WPR_CFG_MASK, PLM_PREOPEN_SKIP,
  PLM_ALL_SECOND_SUCCESS, PLM_TARGET_READ_FAILURE,
  RESET_CHANGES_WPR_FIRST, RESET_CHANGES_WPR_LATER,
  RESET_CORRUPTS_PAYLOAD_FIRST, RESET_CORRUPTS_PAYLOAD_LATER,
  PRE_RESET_RUNNING,
  AD_MARKER_MISSING, AD_MARKER_WRONG, AD_MARKER_NO_OPEN, AD_THIRD_FAIL, AD_FEAT_FAIL, AD_SS0_REJECT, AD_SS1_REJECT,
  AD_FBPA_REJECT,
  SCENARIO_COUNT
};
static const char *case_names[]={"success","fwsec-timeout","post-flr-gfw-timeout",
  "bme-readback-failure","mmio-write-failure","flush-failure","flr-write-failure",
  "unmap-failure","map-failure","partial-map","pci-restore-failure","display-changed","selector-lost",
  "fwsec-halted-no-wpr","fwsec-running-then-halt",
  "fwsec-prestart-snapshot-failure","fwsec-posthalt-snapshot-failure",
  "pio-unavailable-fwsec-success","pio-unavailable-fwsec-no-wpr","fwsec-error-code",
  "sec2-delayed-start","sec2-stuck-halt","sec2-running-then-halt","sec2-running-timeout",
  "sec2-status-unavailable","sec2-snapshot-io-error","sec2-protected-debuginfo",
  "sec2-second-attempt-success","sec2-both-attempts-fail","sec2-retry-reset-refused",
  "sec2-retry-wpr-restore-refused","sec2-status-error-plm-readback-open",
  "sec2-retry-lockdown-recovery","sec2-retry-falcon-switch-refused",
  "sec2-retry-bcr-read-failure","sec2-retry-access-still-locked",
  "plm-first-fails-later-targets-succeed","plm-second-target-timeout",
  "plm-prior-target-relocked","plm-wpr-cfg-all-ones-is-wrong","plm-preopened-skip",
  "plm-all-targets-second-attempt","plm-target-read-transport-failure",
  "reset-changes-wpr-first","reset-changes-wpr-later",
  "reset-corrupts-payload-first","reset-corrupts-payload-later",
  "pre-reset-prior-engine-running", "ad-marker-missing", "ad-marker-wrong", "ad-marker-without-plm", "ad-third-plm-failure", "ad-feat-failure", "ad-ss0-write-rejected", "ad-ss1-write-rejected", "ad-fbpa-write-rejected"};
typedef struct {VOID *cpu;UINTN bytes;UINT64 dma;BOOLEAN mapped,freed;} TEST_BUFFER;
typedef struct {VOID *cpu;BOOLEAN freed;} TEST_POOL;
static TEST_BUFFER buffers[MAX_BUFFERS];
static TEST_POOL pools[MAX_POOLS];
static EFI_BOOT_SERVICES bs;
static EFI_PCI_IO_PROTOCOL pci;
static UINT8 config[256],prom[1024*1024];
static UINT32 registers[0x1000000/4];
static UINT8 gsp_dmem[0xe00];
static struct {
  enum scenario mode;
  UINTN buffer_count,pool_count,pool_live,find_hint;
  UINTN maps,unmaps,frees,live_maps,live_buffers,mmio_writes,pci_writes;
  UINTN gsp_starts,sec2_starts,flrs,flushes,dma_reads,radix_bytes;
  UINTN dmem_reads,pio_reads,pio_writes,gsp_poll_reads,trace_count;
  UINTN master_enables,master_disables;
  UINTN retry_resets, wpr_restore_writes, first_start_maps, first_start_buffers;
  UINTN prepare_wpr_writes, reset_preparations;
  BOOLEAN reset_since_wpr;
  UINTN retry_switches, retry_dma_before_switch;
  UINT32 current_target, current_value;
  UINTN current_index, target_attempts[4], target_trace[8];
  BOOLEAN sec2_active;
  BOOLEAN retry_priv_locked;
  BOOLEAN retry_dma_idle_read;
  UINT32 saved_wpr_lo,saved_wpr_hi;
  UINT64 pci_attributes;
  UINT32 write_trace[8192][4];
  UINT64 next_dma,delay_since_flr,elapsed,meta_dma,sec2_start_tick;
  BOOLEAN bme_read_failed,unmap_failed,write_failed,restore_failed;
  char log[262144];
} fake;

static UINTN expected_sec2_starts(enum scenario mode) {
 if(mode==PLM_TARGET_READ_FAILURE || mode==RESET_CHANGES_WPR_FIRST || mode==RESET_CORRUPTS_PAYLOAD_FIRST)return 0;
 return 1;
}

static BOOLEAN retry_scenario(enum scenario mode) {
  return (mode>=SEC2_RETRY_SUCCESS && mode<=SEC2_RETRY_WPR_RESTORE_REFUSED) ||
    (mode>=SEC2_RETRY_LOCKDOWN_RECOVERY && mode<=SEC2_RETRY_ACCESS_LOCKED);
}

static BOOLEAN lockdown_scenario(enum scenario mode) {
  return mode>=SEC2_RETRY_LOCKDOWN_RECOVERY && mode<=SEC2_RETRY_ACCESS_LOCKED;
}

static void fail(const char *expression,const char *file,int line) {
  size_t length=strlen(fake.log);
  fprintf(stderr,"ASSERTION FAILED [%s] %s:%d: %s\n",case_names[fake.mode],file,line,expression);
  fprintf(stderr,"started=%u quiesced=%u fault=%u retained=%u GSP=%zu SEC2=%zu FLR=%zu mappings=%zu buffers=%zu\n",
    Gu.started,Gu.quiesced,Gu.fault,Gu.retained,fake.gsp_starts,fake.sec2_starts,
    fake.flrs,fake.live_maps,fake.live_buffers);
  fprintf(stderr,"%s",fake.log+(length>5000?length-5000:0));
  exit(1);
}
#define CHECK(x) ((x)?(void)0:fail(#x,__FILE__,__LINE__))
static VOID trace_write(UINT32 kind,UINT32 width,UINT32 offset,UINT32 value) {
  CHECK(fake.trace_count<8192);UINT32 *r=fake.write_trace[fake.trace_count++];
  r[0]=kind;r[1]=width;r[2]=offset;r[3]=value;
}
VOID GuLog(const char *format,...) {
  va_list args;size_t used=strlen(fake.log);
  va_start(args,format);vsnprintf(fake.log+used,sizeof(fake.log)-used,format,args);va_end(args);
}
VOID GuLogText(const char *text) {GuLog("%s",text);}
static UINT64 le64(const VOID *pointer) {
  const UINT8 *p=pointer;UINT64 value=0;for(int i=7;i>=0;i--)value=(value<<8)|p[i];return value;
}
static UINT32 le32(const VOID *pointer) {
  const UINT8 *p=pointer;return p[0]|((UINT32)p[1]<<8)|((UINT32)p[2]<<16)|((UINT32)p[3]<<24);
}
static VOID quiet_check(void) {CHECK(!fake.flrs || fake.delay_since_flr>=100000);}
static TEST_BUFFER *cpu_buffer(VOID *cpu) {
  if(fake.buffer_count && buffers[fake.buffer_count-1].cpu==cpu)return &buffers[fake.buffer_count-1];
  for(UINTN step=0;step<fake.buffer_count;step++) {
    UINTN index=(fake.find_hint+step)%fake.buffer_count;
    if(buffers[index].cpu==cpu){fake.find_hint=(index+1)%fake.buffer_count;return &buffers[index];}
  }
  CHECK(0);return NULL;
}
static UINT8 *dma_pointer(UINT64 address,UINTN bytes) {
  UINTN low=0,high=fake.buffer_count;
  while(low<high) {
    UINTN mid=low+(high-low)/2;
    if(buffers[mid].dma<=address)low=mid+1;else high=mid;
  }
  CHECK(low>0);TEST_BUFFER *b=&buffers[low-1];
  CHECK(b->mapped && !b->freed && address>=b->dma && address-b->dma<=b->bytes);
  CHECK(bytes<=b->bytes-(UINTN)(address-b->dma));
  CHECK(address!=(UINT64)(UINTN)b->cpu+(address-b->dma));
  fake.dma_reads++;return (UINT8 *)b->cpu+(UINTN)(address-b->dma);
}
static VOID check_fwsec_image(void) {
  UINT64 last_source=((UINT64)registers[(GSP+0x110)/4]<<8)|((UINT64)registers[(GSP+0x128)/4]<<40);
  CHECK(last_source>=0xf200);UINT8 *image=dma_pointer(last_source-0xf200,0x10000);
  CHECK(!memcmp(gsp_dmem,image+0xf200,sizeof(gsp_dmem)));
  CHECK(!memcmp(gsp_dmem+0xb24,prom+0x43e68+0x180,384));
  CHECK(registers[0x1183a4/4]==0x6000 || registers[0x1183a4/4]==0x5ff8);
  CHECK(le32(gsp_dmem+0xd40+32)==(registers[0x1183a4/4]==0x5ff8?0x5ff600:0x5ffe00));
  CHECK(le32(gsp_dmem+0xd40+36)==0x100);
}
static VOID check_booter_publication(void) {
  UINT64 meta_address=(UINT64)registers[(SEC2+0x40)/4]|((UINT64)registers[(SEC2+0x44)/4]<<32);
  UINT8 *meta=dma_pointer(meta_address,256);fake.meta_dma=meta_address;
  CHECK(le64(meta)==GU_WPR_META_MAGIC && le64(meta+8)==1);
  CHECK(le64(meta+24)==0x5053000 && le64(meta+80)==0xfa00);
  /* For this fixed synthetic VGA/MMU fixture, reducing FB by 8 MiB must
     shift every end-of-FB reservation down by precisely 8 MiB. */
  UINT64 delta=registers[0x1183a4/4]==0x5ff8?0x800000ULL:0;
  CHECK(le64(meta+112)==0x5f5400000ULL-delta && le64(meta+136)==0x5fada0000ULL-delta);
  CHECK(le64(meta+144)==0x5ffdf7000ULL-delta && le64(meta+152)==0x5ffe00000ULL-delta);
  CHECK(le64(meta+168)==0x5fff00000ULL-delta && le64(meta+176)==0x600000000ULL-delta);
  UINT8 *payload=dma_pointer(le64(meta+72),64000);
  CHECK(le32(payload+0xf960)==expected_targets[0] && le32(payload+0xf948)==expected_values[0]);
  CHECK(le32(payload+0xf984)==expected_targets[1]);
  CHECK(le32(payload+0xf9a8)==expected_targets[2]);
  CHECK(le32(payload+0xf9cc)==expected_targets[3]);
  CHECK(fake.sec2_starts==1 && !memcmp(payload,GuAssets[1].firmware.data,64000));
  fake.current_index=3;fake.current_target=expected_targets[3];fake.current_value=expected_values[3];
  fake.target_attempts[3]++;
  CHECK(!memcmp(dma_pointer(le64(meta+32),36864),GuAssets[3].firmware.data,36864));
  /* Walk published DMA addresses as firmware would; expected ELF bytes are pinned. */
  for(UINT64 offset=0,page=0;offset<0x5053000;offset+=4096,page++) {
    UINT8 *l0=dma_pointer(le64(meta+16),4096);
    UINT8 *l1=dma_pointer(le64(l0+(page>>18)*8),4096);
    UINT8 *l2=dma_pointer(le64(l1+((page>>9)&511)*8),4096);
    UINT8 *leaf=dma_pointer(le64(l2+(page&511)*8),4096);
    UINTN count=(UINTN)(0x5053000-offset);if(count>4096)count=4096;
    CHECK(!memcmp(leaf,GuAssets[2].firmware.data+0x40+(size_t)offset,count));
    for(UINTN i=count;i<4096;i++)CHECK(!leaf[i]);
    fake.radix_bytes+=count;
  }
}
static VOID check_stock_signature(void) {
  UINT8 *meta=dma_pointer(fake.meta_dma,256);
  CHECK(le64(meta+80)==4096);
  struct gpu_unlock_span signature;
  CHECK(!gpu_unlock_elf_section(GuAssets[2].firmware.data,GuAssets[2].firmware.size,
                               ".fwsignature_ad10x",&signature));
  CHECK(signature.size==4096);
  CHECK(!memcmp(dma_pointer(le64(meta+72),4096),GuAssets[2].firmware.data+signature.offset,4096));
}
static EFI_STATUS EFIAPI allocate_pool(EFI_MEMORY_TYPE type,UINTN size,VOID **memory) {
  CHECK(type==EfiLoaderData && fake.pool_count<MAX_POOLS);
  *memory=calloc(1,size);if(!*memory)return EFI_OUT_OF_RESOURCES;
  pools[fake.pool_count++]=(TEST_POOL){*memory,FALSE};fake.pool_live++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI free_pool(VOID *memory) {
  for(UINTN i=fake.pool_count;i>0;i--)if(pools[i-1].cpu==memory && !pools[i-1].freed) {
    pools[i-1].freed=TRUE;fake.pool_live--;free(memory);return EFI_SUCCESS;
  }
  CHECK(0);return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI allocate_buffer(EFI_PCI_IO_PROTOCOL *p,EFI_ALLOCATE_TYPE type,
  EFI_MEMORY_TYPE memory_type,UINTN pages,VOID **cpu,UINT64 attributes) {
  CHECK(p==&pci && type==AllocateAnyPages && memory_type==EfiBootServicesData && !attributes);
  CHECK(pages && fake.buffer_count<MAX_BUFFERS);
  *cpu=_aligned_malloc(pages*4096,4096);if(!*cpu)return EFI_OUT_OF_RESOURCES;
  buffers[fake.buffer_count++]=(TEST_BUFFER){*cpu,pages*4096,0,FALSE,FALSE};
  fake.live_buffers++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI free_buffer(EFI_PCI_IO_PROTOCOL *p,UINTN pages,VOID *cpu) {
  CHECK(p==&pci);TEST_BUFFER *b=cpu_buffer(cpu);
  CHECK(!b->freed && !b->mapped && pages*4096==b->bytes);
  CHECK(!Gu.started || Gu.quiesced);
  b->freed=TRUE;fake.frees++;fake.live_buffers--;_aligned_free(cpu);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI map_buffer(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_OPERATION operation,
  VOID *cpu,UINTN *bytes,EFI_PHYSICAL_ADDRESS *device_address,VOID **mapping) {
  CHECK(p==&pci && operation==EfiPciIoOperationBusMasterCommonBuffer);
  TEST_BUFFER *b=cpu_buffer(cpu);CHECK(*bytes==b->bytes && !b->mapped && !b->freed);
  fake.maps++;if(fake.mode==MAP_FAIL && fake.maps==1)return EFI_UNSUPPORTED;
  b->dma=fake.next_dma;fake.next_dma+=b->bytes;CHECK(fake.next_dma<=0xffffffffULL);
  CHECK(b->dma!=(UINT64)(UINTN)cpu);b->mapped=TRUE;fake.live_maps++;
  *device_address=b->dma;*mapping=b;
  if(fake.mode==MAP_PARTIAL && fake.maps==48)*bytes/=2;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI unmap_buffer(EFI_PCI_IO_PROTOCOL *p,VOID *mapping) {
  CHECK(p==&pci);TEST_BUFFER *b=mapping;
  CHECK(b>=buffers && b<buffers+fake.buffer_count && b->mapped && !b->freed);
  CHECK(!Gu.started || Gu.quiesced);fake.unmaps++;
  if(fake.mode==UNMAP_FAIL && !fake.unmap_failed){fake.unmap_failed=TRUE;return EFI_DEVICE_ERROR;}
  b->mapped=FALSE;fake.live_maps--;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_config(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH width,
  UINT32 offset,UINTN count,VOID *value) {
  CHECK(p==&pci && width<=EfiPciIoWidthUint32 && count==1);quiet_check();
  UINTN bytes=(UINTN)1<<width;CHECK(offset+bytes<=sizeof(config));
  if(fake.mode==BME_READBACK_FAIL && offset==4 && (config[4]&4) && !fake.bme_read_failed) {
    fake.bme_read_failed=TRUE;return EFI_DEVICE_ERROR;
  }
  memcpy(value,config+offset,bytes);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_config(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH width,
  UINT32 offset,UINTN count,VOID *value) {
  CHECK(p==&pci && width<=EfiPciIoWidthUint32 && count==1);quiet_check();
  UINTN bytes=(UINTN)1<<width;CHECK(offset+bytes<=sizeof(config));
  CHECK(!(offset<=6 && offset+bytes>6) && offset!=0x82 && offset!=0x64);
  UINT32 trace_value=0;memcpy(&trace_value,value,bytes);trace_write(1,(UINT32)width,offset,trace_value);
  fake.pci_writes++;memcpy(config+offset,value,bytes);
  if(offset==0x80 && width==EfiPciIoWidthUint16 && (*(UINT16 *)value&0x8000)) {
    CHECK(!(config[4]&4) && fake.gsp_starts==1 && fake.sec2_starts==expected_sec2_starts(fake.mode));
    CHECK(!fake.unmaps && !fake.frees);check_stock_signature();
    fake.flrs++;CHECK(fake.flrs==1);fake.delay_since_flr=0;
    memset(config+0x10,0,24);config[4]=0;config[0x80]=config[0x81]=0;
    registers[(GSP+0x3c0)/4]=registers[(SEC2+0x3c0)/4]=0;
    registers[0x1fa824/4]=registers[0x1fa828/4]=0;
    registers[0x118128/4]=1;registers[0x118234/4]=0xff;registers[0x110100/4]=0x10;
    if(fake.mode==SEC2_STATUS_ERROR_PLM_OPEN)registers[(SEC2+0x100)/4]=0x10;
    if(fake.mode==POST_FLR_TIMEOUT)registers[0x110100/4]=registers[0x118234/4]=0;
    if(fake.mode==SELECTOR_LOST)registers[SS1/4]=7;
    if(fake.mode==FLR_WRITE_FAIL)return EFI_DEVICE_ERROR;
  }
  if(fake.mode==RESTORE_FAIL && fake.flrs && offset==0x10 && !fake.restore_failed) {
    fake.restore_failed=TRUE;return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_memory(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH width,
  UINT8 bar,UINT64 offset,UINTN count,VOID *value) {
  CHECK(p==&pci && !bar);quiet_check();
  CHECK(width==EfiPciIoWidthUint32);
  CHECK(!(offset&3) && !((UINTN)value&3));
  CHECK(count==1 || (count<=256 && offset>=0x300000 && offset+count*4<=0x400000));
  UINTN bytes=((UINTN)1<<width)*count;CHECK(offset+bytes<=sizeof(registers));
  if(offset==GSP+0x1c0 || offset==GSP+0x1c4)fake.pio_reads++;
  if(offset==GSP+0x1c4) {
    UINT32 at=registers[(GSP+0x1c0)/4];
    CHECK(!fake.gsp_starts && !(at&0xff000003U) && at+4<=sizeof(gsp_dmem));
    fake.dmem_reads++;
    if(fake.mode==PIO_UNAVAILABLE || fake.mode==PIO_UNAVAILABLE_NO_WPR){*(UINT32 *)value=0xffffffffU;return EFI_SUCCESS;}
    memcpy(value,gsp_dmem+at,4);return EFI_SUCCESS;
  }
  if(fake.mode==PLM_TARGET_READ_FAILURE && offset==0x9a0148 && fake.gsp_starts)return EFI_DEVICE_ERROR;
  if(offset==GSP+0x94 &&
      ((fake.mode==DIAG_READ_FAIL && !fake.gsp_starts) ||
       (fake.mode==POST_DIAG_READ_FAIL && fake.gsp_starts)))return EFI_DEVICE_ERROR;
  if(offset==GSP+0x100 && fake.gsp_starts && !fake.sec2_starts) {
    fake.gsp_poll_reads++;
    if(fake.mode==FWSEC_RUNNING)
      registers[offset/4]=fake.gsp_poll_reads<=2?0:0x10;
  }
  if(lockdown_scenario(fake.mode) && fake.sec2_starts && !fake.flrs) {
    if(fake.retry_resets && offset==0x841668 && fake.mode==SEC2_RETRY_BCR_READ_FAIL)
      return EFI_DEVICE_ERROR;
    if(fake.retry_priv_locked && (offset==SEC2+0x10c || offset==SEC2+0x118)) {
      if(fake.retry_resets && !fake.retry_switches)fake.retry_dma_before_switch++;
      *(UINT32 *)value=0xbadf5620;return EFI_SUCCESS;
    }
    if(fake.retry_resets && fake.retry_switches && !fake.retry_priv_locked && offset==SEC2+0x118)
      fake.retry_dma_idle_read=TRUE;
  }
  if(fake.sec2_active && !fake.flrs) {
    UINT64 delta=fake.elapsed-fake.sec2_start_tick;
    if(offset==SEC2+0x94 && fake.mode==SEC2_DIAG_FAIL)return EFI_DEVICE_ERROR;
    if(offset==SEC2+0x94 && fake.mode==SEC2_PROTECTED_DIAG){*(UINT32 *)value=0xbadf5620;return EFI_SUCCESS;}
    if(offset==SEC2+0x100) {
      registers[offset/4]=delta<1000?0:0x10;
      if(fake.mode==SEC2_DELAYED_START) {
        registers[offset/4]=delta<2000?0x10:(delta<6000?0:0x10);
        if(delta>=5000){registers[fake.current_target/4]=fake.current_value;registers[(SEC2+0x40)/4]=0x67b00001;registers[(SEC2+0x94)/4]=0xda550000;}
      }
      if(fake.mode==SEC2_STUCK_HALT)registers[offset/4]=0x10;
      if(fake.mode==SEC2_RUN_HALT)registers[offset/4]=delta<1000?0:0x10;
      if(fake.mode==SEC2_RUN_TIMEOUT)registers[offset/4]=0;
      if(fake.mode==SEC2_UNAVAILABLE)registers[offset/4]=delta<50?0:0xbadf5620;
      if(retry_scenario(fake.mode) || fake.mode>=PLM_FIRST_FAIL_NEXT_SUCCEED)registers[offset/4]=delta<1000?0:0x10;
      if(fake.mode==PRE_RESET_RUNNING)registers[offset/4]=0;
      if(fake.mode==PLM_SECOND_TARGET_TIMEOUT)registers[offset/4]=0;
      if(fake.mode==SEC2_STATUS_ERROR_PLM_OPEN)registers[offset/4]=0xbadf5620;
    }
  }
  memcpy(value,(UINT8 *)registers+(UINTN)offset,bytes);return EFI_SUCCESS;
}
static UINT8 *host_payload_copy(void) {
  for(UINTN i=0;i<fake.buffer_count;i++)
    if(buffers[i].mapped && !buffers[i].freed && buffers[i].bytes>=64000 &&
       !memcmp(buffers[i].cpu,GuAssets[1].firmware.data,256))return buffers[i].cpu;
  CHECK(0);return NULL;
}
static EFI_STATUS EFIAPI write_memory(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH width,
  UINT8 bar,UINT64 offset,UINTN count,VOID *value) {
  CHECK(p==&pci && !bar && width==EfiPciIoWidthUint32 && count==1);quiet_check();
  CHECK(offset+4<=sizeof(registers) && Gu.started);
  UINT32 v=*(UINT32 *)value;fake.mmio_writes++;
  trace_write(0,(UINT32)width,(UINT32)offset,v);
  if(!fake.flrs && offset==SEC2+0x3c0 && (v&1)) {
    CHECK(fake.prepare_wpr_writes==2 && !fake.reset_since_wpr);
    CHECK(registers[0x1fa824/4]==fake.saved_wpr_lo && registers[0x1fa828/4]==fake.saved_wpr_hi);
    UINT8 *payload=host_payload_copy();UINT32 target=le32(payload+0xf960);
    UINTN index=0;while(index<4 && expected_targets[index]!=target)index++;
    CHECK(index<4 && le32(payload+0xf948)==expected_values[index]);
    fake.prepare_wpr_writes=0;fake.reset_since_wpr=TRUE;fake.reset_preparations++;
    if((fake.mode==RESET_CHANGES_WPR_FIRST && !fake.sec2_starts) ||
       (fake.mode==RESET_CHANGES_WPR_LATER && fake.sec2_starts==1))registers[0x1fa828/4]=0;
    if((fake.mode==RESET_CORRUPTS_PAYLOAD_FIRST && !fake.sec2_starts) ||
       (fake.mode==RESET_CORRUPTS_PAYLOAD_LATER && fake.sec2_starts==1))payload[0x1200]^=1;
  }
  if(fake.sec2_starts && !fake.flrs && offset==SEC2+0x3c0) {
    fake.retry_resets++;
    if(v&1){fake.sec2_active=FALSE;registers[(SEC2+0x100)/4]=0x10;fake.retry_dma_idle_read=FALSE;}
    if(fake.mode==SEC2_RETRY_RESET_REFUSED)return EFI_SUCCESS;
    if(lockdown_scenario(fake.mode) && (v&1)) {
      fake.retry_priv_locked=TRUE;
      registers[(SEC2+0xf4)/4]|=0x2000;
      registers[0x841668/4]=0x10;
    }
  }
  if(lockdown_scenario(fake.mode) && fake.sec2_starts && !fake.flrs && offset==0x841668) {
    CHECK(!(registers[(SEC2+0x3c0)/4]&1)); /* Never switch while reset is asserted. */
    fake.retry_switches++;
    if(fake.mode==SEC2_RETRY_SWITCH_REFUSED)return EFI_SUCCESS;
    CHECK(v==0);
    if(fake.mode!=SEC2_RETRY_ACCESS_LOCKED) {
      fake.retry_priv_locked=FALSE;registers[(SEC2+0xf4)/4]&=~0x2000U;
    }
  }
  if(fake.gsp_starts && !fake.flrs && (offset==0x1fa824 || offset==0x1fa828)) {
    CHECK(!fake.reset_since_wpr); /* No post-reset WPR2 repair in this experiment. */
    CHECK(v==(offset==0x1fa824?fake.saved_wpr_lo:fake.saved_wpr_hi));
    CHECK(offset==(fake.prepare_wpr_writes?0x1fa828:0x1fa824));
    fake.prepare_wpr_writes++;
    if(fake.sec2_starts) {
      fake.wpr_restore_writes++;
      CHECK(registers[(SEC2+0x100)/4]&0x10);
      if(fake.mode==SEC2_RETRY_WPR_RESTORE_REFUSED)return EFI_SUCCESS;
    }
  }
  for(UINTN i=0;i<4;i++)CHECK(offset!=expected_targets[i]); /* PLMs are written by the simulated HS payload only. */
  CHECK(offset!=0x9a0204 && offset!=0x100ce0); /* No CMP geometry stores. */
  if(offset==SS0 || offset==SS1)for(UINTN i=0;i<4;i++)if(i!=1)CHECK(registers[expected_targets[i]/4]==expected_values[i]);
  if((fake.mode==AD_SS0_REJECT && offset==SS0) || (fake.mode==AD_SS1_REJECT && offset==SS1))return EFI_SUCCESS;
  registers[offset/4]=v;
  CHECK(offset!=GSP+0x1c4); /* Diagnostics must never write firmware data. */
  if(offset==GSP+0x1c0) {
    CHECK(!fake.gsp_starts);fake.pio_writes++;
  }
  if(fake.mode==MMIO_WRITE_FAIL && !fake.write_failed){fake.write_failed=TRUE;return EFI_DEVICE_ERROR;}
  if(offset==GSP+0x118 || offset==SEC2+0x118) {
    UINT32 base=offset==GSP+0x118?GSP:SEC2;
    UINT64 source=((UINT64)registers[(base+0x110)/4]<<8)|((UINT64)registers[(base+0x128)/4]<<40);
    UINT8 *source_cpu=dma_pointer(source+registers[(base+0x11c)/4],256);
    if(base==GSP && !(v&0x10)) {
      UINT32 dest=registers[(base+0x114)/4];CHECK(dest+256<=sizeof(gsp_dmem));
      memcpy(gsp_dmem+dest,source_cpu,256);
    }
    registers[offset/4]=2;
  }
  if(offset==0x111668 || offset==0x841668)registers[offset/4]=1;
  if(offset==GSP+0x100 || offset==GSP+0x130) {
    CHECK(fake.pci_attributes&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);
    fake.gsp_starts++;CHECK(fake.gsp_starts==1 && (config[4]&4));check_fwsec_image();
    if(fake.mode!=FWSEC_NO_WPR && fake.mode!=PIO_UNAVAILABLE_NO_WPR) {
      UINT32 delta=registers[0x1183a4/4]==0x5ff8?0x8000:0;
      registers[0x1fa824/4]=0x5ffe000-delta;registers[0x1fa828/4]=0x5ffeff0-delta;
      fake.saved_wpr_lo=registers[0x1fa824/4];fake.saved_wpr_hi=registers[0x1fa828/4];
    }
    if(fake.mode==FWSEC_ERROR_CODE)registers[0x1438/4]=0x10000;
    registers[GSP/4+0x100/4]=fake.mode==FWSEC_TIMEOUT?2:0x10;
  }
  if(offset==SEC2+0x100 || offset==SEC2+0x130) {
    CHECK(fake.reset_since_wpr && !fake.prepare_wpr_writes);fake.reset_since_wpr=FALSE;
    fake.sec2_starts++;CHECK(fake.reset_preparations==fake.sec2_starts);CHECK(fake.sec2_starts<=8 && (config[4]&4));
    if(fake.sec2_starts==1) {
      fake.first_start_maps=fake.maps;fake.first_start_buffers=fake.buffer_count;
    } else {
      CHECK(fake.retry_resets==2*(fake.sec2_starts-1) && fake.maps==fake.first_start_maps && fake.buffer_count==fake.first_start_buffers);
      CHECK(!fake.unmaps && !fake.frees && !Gu.quiesced);
      CHECK(fake.master_enables==1 && !fake.master_disables);
      CHECK(registers[0x1fa824/4]==fake.saved_wpr_lo && registers[0x1fa828/4]==fake.saved_wpr_hi);
    }
    check_booter_publication();fake.sec2_start_tick=fake.elapsed;fake.sec2_active=TRUE;
    for(UINTN n=0;n<4;n++)registers[expected_targets[n]/4]=expected_values[n];
    registers[(SEC2+0x40)/4]=0x67b00001;
    registers[(SEC2+0x94)/4]=0xda550000;
    if(fake.mode>=SEC2_DELAYED_START && fake.mode<=SEC2_UNAVAILABLE) {
      registers[fake.current_target/4]=0xffffff8f;
      registers[(SEC2+0x40)/4]=0xbeef;
      registers[(SEC2+0x94)/4]=0xdead0001;
    }
    if(fake.mode==AD_MARKER_MISSING)registers[(SEC2+0x40)/4]=0;
    if(fake.mode==AD_MARKER_WRONG)registers[(SEC2+0x40)/4]=0x67b00002;
    if(fake.mode==AD_MARKER_NO_OPEN)for(UINTN n=0;n<4;n++)registers[expected_targets[n]/4]=0xffffff8f;
    if(fake.mode==PLM_SECOND_TARGET_TIMEOUT) {
      for(UINTN n=1;n<4;n++)registers[expected_targets[n]/4]=0xffffff8f;
      registers[(SEC2+0x40)/4]=0x6f790000;
    }
    if(fake.mode==AD_FBPA_REJECT)registers[expected_targets[1]/4]=0xffffff0f;
    if(fake.mode==AD_THIRD_FAIL)registers[expected_targets[2]/4]=0x4cb8f;
    if(fake.mode==AD_FEAT_FAIL)registers[expected_targets[3]/4]=0xffffff8f;
    if(fake.mode==PLM_WRONG_WPR_CFG_MASK)registers[expected_targets[0]/4]=~0U;
    if(fake.mode==PLM_PRIOR_RELOCK)registers[expected_targets[0]/4]=0x4cb8f;
    if(retry_scenario(fake.mode) && fake.sec2_starts==1) {
      /* Firmware leaves changed but readable boundaries. The next START
         must use the FWSEC publication, rather than these stale values. */
      registers[0x1fa824/4]=registers[0x1fa828/4]=0;
    }
    if(lockdown_scenario(fake.mode))fake.retry_priv_locked=TRUE;
  }
  if(offset==SS1)registers[offset/4]=v&15;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush(EFI_PCI_IO_PROTOCOL *p) {
  CHECK(p==&pci);fake.flushes++;
  return fake.mode==FLUSH_FAIL && fake.flushes==1?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI master_attributes(EFI_PCI_IO_PROTOCOL *p,
 EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
  CHECK(p==&pci);
  if(op==EfiPciIoAttributeOperationSupported) {
    CHECK(out && !mask);*out=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
    return EFI_SUCCESS;
  }
  if(op==EfiPciIoAttributeOperationGet) {
    CHECK(out && !mask);*out=fake.pci_attributes;return EFI_SUCCESS;
  }
  CHECK(!out && mask==EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);
  UINT16 command=*(UINT16 *)(config+4);
  if(op==EfiPciIoAttributeOperationEnable) {
    fake.master_enables++;command|=4;fake.pci_attributes|=mask;
  } else {
    CHECK(op==EfiPciIoAttributeOperationDisable);
    fake.master_disables++;command&=(UINT16)~4U;fake.pci_attributes&=~mask;
  }
  return write_config(p,EfiPciIoWidthUint16,4,1,&command);
}
static EFI_STATUS EFIAPI stall(UINTN microseconds) {
  fake.elapsed+=microseconds;if(fake.flrs)fake.delay_since_flr+=microseconds;return EFI_SUCCESS;
}
static BOOLEAN display_check(void) {return fake.mode!=DISPLAY_CHANGED;}
static VOID setup(enum scenario mode) {
  memset(&fake,0,sizeof(fake));memset(buffers,0,sizeof(buffers));memset(pools,0,sizeof(pools));
  memset(&bs,0,sizeof(bs));memset(&pci,0,sizeof(pci));memset(config,0,sizeof(config));
  memset(registers,0,sizeof(registers));memset(gsp_dmem,0,sizeof(gsp_dmem));
  fake.mode=mode;fake.next_dma=0x12000000;
  bs.AllocatePool=allocate_pool;bs.FreePool=free_pool;bs.Stall=stall;
  pci.AllocateBuffer=allocate_buffer;pci.FreeBuffer=free_buffer;pci.Map=map_buffer;pci.Unmap=unmap_buffer;
  pci.Pci.Read=read_config;pci.Pci.Write=write_config;pci.Mem.Read=read_memory;pci.Mem.Write=write_memory;pci.Flush=flush;
  pci.Attributes=master_attributes;fake.pci_attributes=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY;
  *(UINT32 *)(config+0)=0x268410de;*(UINT32 *)(config+0x2c)=0x167c10de;
  *(UINT32 *)(config+4)=0x00100003;*(UINT32 *)(config+8)=0x030000a1;*(UINT32 *)(config+12)=0x00800010;
  *(UINT32 *)(config+0x10)=0x80000000;*(UINT32 *)(config+0x14)=0xc;*(UINT32 *)(config+0x18)=0x60;
  *(UINT32 *)(config+0x1c)=0xc;*(UINT32 *)(config+0x20)=0x64;*(UINT32 *)(config+0x24)=0x5001;
  *(UINT32 *)(config+0x30)=0x85000000;config[0x34]=0x60;config[0x3c]=0xff;
  *(UINT32 *)(config+0x60)=0x48036801;*(UINT32 *)(config+0x64)=8;
  *(UINT32 *)(config+0x68)=0x00807805;*(UINT32 *)(config+0x78)=0x0012b410;
  *(UINT32 *)(config+0x7c)=0x112c8de1;*(UINT32 *)(config+0x80)=0x00092930;
  *(UINT32 *)(config+0xc8)=0x00050011;
  registers[0]=0x192000a1;registers[0x118128/4]=0x8b8f;registers[0x118234/4]=0x03ff;
  registers[GSP/4+0x100/4]=0x10;registers[0x1fa824/4]=0x1ffffe00;
  for(UINTN i=0;i<4;i++)registers[expected_targets[i]/4]=0xffffff8f;
  if(mode==PLM_PREOPEN_SKIP)registers[expected_targets[0]/4]=expected_values[0];
  registers[PLM/4]=0xffffff8f;registers[SS0/4]=0x31322012;registers[SS1/4]=7;
  registers[0x1183a4/4]=0x6000;registers[0x625f04/4]=1;registers[0x1fa7c8/4]=0x45cb8f;
  registers[0x1fa82c/4]=0x100000;registers[0x1fa830/4]=0x26ffff0;
  registers[0x824148/4]=1;registers[0x8241e0/4]=1;
  registers[(GSP+0xf4)/4]=registers[(SEC2+0xf4)/4]=1U<<31;
  if(lockdown_scenario(mode)) {
    registers[(SEC2+0xf4)/4]|=1U<<10;
    registers[0x841668/4]=1;
  }
  registers[(GSP+0x118)/4]=registers[(SEC2+0x118)/4]=2;
  registers[(GSP+0x1c0)/4]=0x40;
  registers[(GSP+0x94)/4]=0x12345678;
  memcpy((UINT8 *)registers+0x300000,prom,sizeof(prom));
  GuInit(&bs,&pci);Gu.bar_base=0x80000000;Gu.bar_size=sizeof(registers);
  Gu.pcie_cap=0x78;Gu.pcie_version=2;Gu.pm_cap=0x60;Gu.msi_cap=0x68;Gu.msix_cap=0;
  Gu.owned=Gu.armed=Gu.display_ok=TRUE;Gu.display_check=display_check;
}
static VOID dispose_host_fixture(void) {
  /* Test process disposal only, after retention assertions. This is not a recovery path. */
  for(UINTN i=0;i<fake.buffer_count;i++)if(!buffers[i].freed)_aligned_free(buffers[i].cpu);
  for(UINTN i=0;i<fake.pool_count;i++)if(!pools[i].freed)free(pools[i].cpu);
}
static UINT8 *read_file(const char *path,size_t *length) {
  FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
  long size=ftell(file);CHECK(size>0 && !fseek(file,0,SEEK_SET));
  UINT8 *data=malloc((size_t)size);CHECK(data);
  CHECK(fread(data,1,(size_t)size,file)==(size_t)size && fgetc(file)==EOF);fclose(file);
  *length=(size_t)size;return data;
}
static VOID load_fixture_assets(const char *prom_path,const char *assets_path) {
  static const char *names[]={"booter_load_ad102.bin","v67-ad102-610.43.03-four-plm.bin",
    "gsp_ga10x.bin","gsp_boot_ad102.bin","gsp_boot_desc_ad102.bin"};
  static const char *full_names[]={"gpu-unlock/610.43.03/booter_load_ad102.bin",
    "gpu-unlock/610.43.03/v67-ad102-610.43.03-four-plm.bin","gpu-unlock/610.43.03/gsp_ga10x.bin",
    "gpu-unlock/610.43.03/gsp_boot_ad102.bin","gpu-unlock/610.43.03/gsp_boot_desc_ad102.bin"};
  size_t length;UINT8 *rom=read_file(prom_path,&length);CHECK(length==sizeof(prom));memcpy(prom,rom,sizeof(prom));free(rom);
  for(UINTN i=0;i<5;i++) {
    char path[4096];int count=snprintf(path,sizeof(path),"%s/%s",assets_path,names[i]);
    CHECK(count>0 && (size_t)count<sizeof(path));GuAssets[i].name=full_names[i];
    GuAssets[i].firmware.data=read_file(path,&GuAssets[i].firmware.size);
  }
}
int main(int argc,char **argv) {
  setvbuf(stdout,NULL,_IONBF,0);CHECK(argc==3);load_fixture_assets(argv[1],argv[2]);
  for(int mode=SUCCESS;mode<SCENARIO_COUNT;mode++) {
    setup((enum scenario)mode);UINT64 mask=0xffffffffffffULL;
    struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},.pm_cap=0x60,.refs=1};
    struct gpu_unlock_610_shared shared={0};
    int result=gpu_unlock_610_load_shared(&device,&shared);CHECK(!result);
    result=gpu_unlock_610_unlock(&device,&shared);gpu_unlock_610_release_shared(&shared);
    BOOLEAN safe=GuSafeToReturn();
    if(mode==SUCCESS || mode==FWSEC_RUNNING || mode==PIO_UNAVAILABLE || mode==SEC2_DELAYED_START || mode==SEC2_PROTECTED_DIAG || mode==SEC2_RETRY_SUCCESS || mode==SEC2_RETRY_LOCKDOWN_RECOVERY || mode==PLM_PREOPEN_SKIP || mode==PLM_ALL_SECOND_SUCCESS) {
      CHECK(!result && safe && Gu.started && Gu.quiesced && !Gu.fault && !Gu.retained);
      CHECK(fake.gsp_starts==1 && fake.sec2_starts==expected_sec2_starts((enum scenario)mode) && fake.flrs==1);
      CHECK(fake.radix_bytes==0x5053000*fake.sec2_starts);
      CHECK(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live);
      CHECK(fake.unmaps==fake.maps && fake.frees==fake.buffer_count);
      CHECK(registers[SS0/4]==0x88888888 && registers[SS1/4]==8);
      CHECK(registers[0x110100/4]&0x10);CHECK(!(registers[0x1fa828/4]>>4));
      CHECK(mask==0xffffffffffffULL && device.dev.coherent_dma_mask==0xfffffffffULL && device.refs==1);
      if(mode==FWSEC_RUNNING)CHECK(strstr(fake.log,"observed_nonhalt=1") && fake.gsp_poll_reads>=3);
      if(mode==SEC2_DELAYED_START)CHECK(strstr(fake.log,"reason=plm-open first=0x10 last=0") && strstr(fake.log,"requested_wait_us=5000"));
      if(mode==SEC2_PROTECTED_DIAG)CHECK(strstr(fake.log,"name=DEBUGINFO reg=0x840094 value=0xbadf5620 sentinel_like=1"));
      if(mode==SEC2_RETRY_SUCCESS)CHECK(fake.retry_resets && fake.wpr_restore_writes>=2 && fake.master_enables==1);
      if(mode==SEC2_STATUS_ERROR_PLM_OPEN)CHECK(strstr(fake.log,"reason=register-unavailable") &&
        strstr(fake.log,"booter_status=-19") && strstr(fake.log,"after=0xfffff0ff expected=0xfffff0ff opened=1") && !Gu.fault);
    } else if(mode==MAP_FAIL || mode==MAP_PARTIAL || mode==BME_READBACK_FAIL) {
      CHECK(result && !Gu.started && safe && !Gu.retained);
      CHECK(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs && !fake.mmio_writes);
      CHECK(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live);
      if(mode==BME_READBACK_FAIL)CHECK(fake.bme_read_failed && Gu.fault && !(config[4]&4));
      else CHECK(!fake.pci_writes);
    } else if(mode==UNMAP_FAIL) {
      CHECK(fake.unmap_failed && !safe && Gu.retained && Gu.quiesced);
      CHECK(fake.live_maps==fake.maps && fake.live_buffers==fake.buffer_count && Gu.allocations);
      CHECK(fake.unmaps==1 && !fake.frees && fake.pool_live);
      CHECK(fake.gsp_starts==1 && fake.sec2_starts==expected_sec2_starts((enum scenario)mode) && fake.flrs==1);
    } else {
      CHECK(result && !safe && Gu.started && !Gu.quiesced && Gu.retained);
      CHECK(fake.live_maps==fake.maps && fake.live_buffers==fake.buffer_count && Gu.allocations);
      CHECK(!fake.unmaps && !fake.frees);
      if(mode==FWSEC_TIMEOUT)CHECK(fake.gsp_starts==1 && !fake.sec2_starts && !fake.flrs && fake.elapsed>=5000000);
      if(mode==MMIO_WRITE_FAIL || mode==FLUSH_FAIL)CHECK(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs);
      if(mode==POST_FLR_TIMEOUT || mode==FLR_WRITE_FAIL || mode==RESTORE_FAIL)CHECK(fake.flrs==1 && fake.delay_since_flr>=100000);
      if(mode==DISPLAY_CHANGED)CHECK(fake.gsp_starts==1 && fake.sec2_starts==expected_sec2_starts((enum scenario)mode) && !fake.flrs);
      if(mode==FWSEC_NO_WPR || mode==PIO_UNAVAILABLE_NO_WPR) {
        CHECK(fake.gsp_starts==1 && !fake.sec2_starts && !fake.flrs);
        CHECK(registers[SS0/4]==0x31322012 && registers[SS1/4]==7);
        CHECK(strstr(fake.log,"FWSEC halt observation:") && strstr(fake.log,"observed_nonhalt=0"));
        CHECK(strstr(fake.log,"FWSEC snapshot: phase=post-halt") && strstr(fake.log,"value=0x12345678"));
      }
      if(mode==DIAG_READ_FAIL)CHECK(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs && Gu.fault);
      if(mode==POST_DIAG_READ_FAIL)CHECK(Gu.fault && fake.gsp_starts==1 && !fake.sec2_starts && !fake.flrs);
      if(mode==FWSEC_ERROR_CODE) {
        CHECK(result==-EIO && fake.gsp_starts==1 && !fake.sec2_starts && !fake.flrs);
        CHECK(strstr(fake.log,"FWSEC result: scratch=0x10000"));
      }
      if(mode>=SEC2_STUCK_HALT && mode<=SEC2_DIAG_FAIL) {
        CHECK(fake.sec2_starts==expected_sec2_starts((enum scenario)mode) && !fake.flrs && registers[SS0/4]==0x31322012 && registers[SS1/4]==7);
        CHECK(strstr(fake.log,"SEC2 snapshot: phase=pre-start"));
        if(mode<=SEC2_RUN_TIMEOUT) {
          CHECK(fake.elapsed-fake.sec2_start_tick==5000000);
          CHECK(strstr(fake.log,"SEC2 snapshot: phase=observe-failure"));
          CHECK(strstr(fake.log,"name=FALCON_MAILBOX0 reg=0x840040 value=0xbeef"));
          CHECK(strstr(fake.log,"name=DEBUGINFO reg=0x840094 value=0xdead0001"));
        }
        if(mode==SEC2_STUCK_HALT)CHECK(result==-EIO && strstr(fake.log,"observed_nonhalt=0"));
        if(mode==SEC2_RUN_HALT)CHECK(result==-EIO && strstr(fake.log,"reason=halted-without-plm first=0 last=0x10"));
        if(mode==SEC2_RUN_TIMEOUT)CHECK(result==-ETIMEDOUT && strstr(fake.log,"reason=running-without-plm"));
        if(mode==SEC2_UNAVAILABLE)CHECK(result==-ENODEV && strstr(fake.log,"reason=register-unavailable"));
        if(mode==SEC2_DIAG_FAIL)CHECK(result==-EIO && Gu.fault && strstr(fake.log,"SEC2 snapshot: phase=plm-open samples=6/35 error="));
      }
      if(retry_scenario((enum scenario)mode)) {
        CHECK(fake.sec2_starts==expected_sec2_starts((enum scenario)mode) && !fake.flrs);
        CHECK(registers[SS0/4]==0x31322012 && registers[SS1/4]==7);
        CHECK((fake.retry_resets || mode==SEC2_RETRY_WPR_RESTORE_REFUSED) && fake.master_enables==1);
        CHECK(Gu.fault==(mode==SEC2_RETRY_BCR_READ_FAIL));
        if(mode==SEC2_RETRY_BOTH_FAIL)CHECK(fake.wpr_restore_writes>=2 && result==-EIO);
        if(mode==SEC2_RETRY_RESET_REFUSED)CHECK(fake.wpr_restore_writes==2);
        if(mode==SEC2_RETRY_WPR_RESTORE_REFUSED)CHECK(fake.wpr_restore_writes &&
          (registers[0x1fa824/4]!=fake.saved_wpr_lo || registers[0x1fa828/4]!=fake.saved_wpr_hi));
      }
    }
    if(lockdown_scenario((enum scenario)mode)) {
      CHECK(!fake.retry_dma_before_switch);
      if(mode==SEC2_RETRY_LOCKDOWN_RECOVERY) {
        CHECK(fake.retry_switches==7 && fake.sec2_starts==8 && fake.retry_dma_idle_read);
        CHECK(strstr(fake.log,"PLM retry access restored:") && strstr(fake.log,"PLM retry DMA idle:"));
      } else {
        CHECK(fake.sec2_starts==1 && fake.wpr_restore_writes==2);
        CHECK(!strstr(fake.log,"PLM retry ready: fire=2"));
      }
    }
    if(mode==SEC2_STATUS_ERROR_PLM_OPEN) {
      CHECK(fake.sec2_starts==1 && !fake.wpr_restore_writes && !fake.flrs && registers[SS1/4]==7);
      CHECK(strstr(fake.log,"booter_status=-19") && strstr(fake.log,"poll inaccessible: reg=0x840100"));
    }
    if(mode>=PLM_FIRST_FAIL_NEXT_SUCCEED) {
      CHECK(fake.sec2_starts==expected_sec2_starts((enum scenario)mode));
      if(mode==PLM_FIRST_FAIL_NEXT_SUCCEED) {
        CHECK(fake.target_attempts[0]==2 && fake.target_attempts[1]==1 && fake.target_attempts[2]==1 && fake.target_attempts[3]==1);
        CHECK(registers[PLM/4]==~0U && registers[SS1/4]==7 && !fake.flrs && !safe);
        check_stock_signature();
      }
      if(mode==PLM_WRONG_WPR_CFG_MASK || mode==PLM_PRIOR_RELOCK)
        CHECK(!safe && !fake.flrs && registers[SS1/4]==7 && strstr(fake.log,"all_expected=0"));
      if(mode==PLM_PREOPEN_SKIP)CHECK(!fake.target_attempts[0] && strstr(fake.log,"payload_execution_proven=0"));
      if(mode==PLM_ALL_SECOND_SUCCESS)for(UINTN i=0;i<4;i++)CHECK(fake.target_attempts[i]==2);
      if(mode==PLM_SECOND_TARGET_TIMEOUT)CHECK(!fake.target_attempts[2] && !fake.target_attempts[3] && !fake.flrs);
      if(mode==PLM_TARGET_READ_FAILURE)CHECK(Gu.fault && !fake.sec2_starts);
      if(mode==RESET_CHANGES_WPR_FIRST || mode==RESET_CHANGES_WPR_LATER) {
        CHECK(!fake.flrs && registers[SS1/4]==7 && strstr(fake.log,"reset_changed=1 matched=0"));
        CHECK(fake.reset_since_wpr && !fake.prepare_wpr_writes);
      }
      if(mode==RESET_CORRUPTS_PAYLOAD_FIRST || mode==RESET_CORRUPTS_PAYLOAD_LATER)
        CHECK(!fake.flrs && registers[SS1/4]==7 && strstr(fake.log,"phase=after-reset copy_match=0"));
      if(mode==PRE_RESET_RUNNING)CHECK(!fake.wpr_restore_writes && !fake.retry_resets && !fake.flrs);
    }
    /* App-level containment must work even when the normal I/O path is fault-latched. */
    CHECK(!EFI_ERROR(GuClearMaster()) && !(config[4]&4));
    if(!safe)CHECK(!GuSafeToReturn());
    CHECK(fake.gsp_starts<=1 && fake.sec2_starts<=8 && fake.flrs<=1);
    CHECK(!fake.pio_reads && !fake.pio_writes && !fake.dmem_reads);
    printf("EFI integration %s: result=%d safe=%u retained=%u live_maps=%zu starts=%zu/%zu flr=%zu PASS\n",
      case_names[mode],result,safe,Gu.retained,fake.live_maps,fake.gsp_starts,fake.sec2_starts,fake.flrs);
    dispose_host_fixture();
  }
  for(UINTN i=0;i<5;i++)free((VOID *)GuAssets[i].firmware.data);
  printf("%u real core + compat + UEFI port scenarios passed; all PCI/MMIO/DMA/firmware execution simulated.\n",SCENARIO_COUNT);
  return 0;
}
