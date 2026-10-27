/* Modified 2026-10-21: experimental AD103 32GB GA102-pair port; see PORT-NOTES.txt. */
#include "app.h"
#include "app_io.h"
#include "compat/compat.h"
#include "checks.h"
#include "sha256.h"
#include "prom_profile.h"
#include "ownership.h"
#include "local_profile.h"
#include "vendor/unlock_610.h"
#include "vendor/gfw_eval.h"
#include <Protocol/GraphicsOutput.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/GlobalVariable.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
  PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,GlobalGuid=EFI_GLOBAL_VARIABLE;
static EFI_SYSTEM_TABLE *Table;
static UINT32 InitialConfig[64];
static const char *Stage;
static BOOLEAN ExpectDetached;
static GU_RELEASE_OWNER PendingOwner;
typedef struct { EFI_HANDLE handle,parent; VOID *gop; UINT64 base,size; UINT32 width,height,stride,format; } OTHER_DISPLAY;
static OTHER_DISPLAY OtherDisplays[8];
static UINTN OtherCount;
static BOOLEAN DisplaysLatched;
static EFI_STATUS Step(EFI_STATUS Status) {
  GuLog("CHECK stage=%s status=%#llx\n",Stage,(unsigned long long)Status);
  return !GuLogReady() && !EFI_ERROR(Status)?EFI_DEVICE_ERROR:Status;
}
static VOID ConfigLog(const UINT32 *Config) {
  for(UINTN i=0;i<64;i+=4)GuLog("config[%02x]=%08x %08x %08x %08x\n",
    (unsigned)(i*4),Config[i],Config[i+1],Config[i+2],Config[i+3]);
}
static VOID PathLog(const char *Tag,const EFI_DEVICE_PATH_PROTOCOL *Path) {
  static const char digits[]="0123456789abcdef";char hex[201];UINTN i=0;
  if(!Path){GuLog("%s path=(null)\n",Tag);return;}
  while(i<190 && !(Path->Type==0x7f && Path->SubType==0xff)) {
    const UINT8 *p=(const UINT8*)Path;UINTN len=(UINTN)Path->Length[0]|((UINTN)Path->Length[1]<<8);
    for(UINTN k=0;k<len && i<190;k++){hex[i++]=digits[p[k]>>4];hex[i++]=digits[p[k]&15];}
    if(!len)break;Path=(const EFI_DEVICE_PATH_PROTOCOL*)((const UINT8*)Path+len);
  }
  hex[i]=0;GuLog("%s path=%s\n",Tag,hex);
}
static BOOLEAN DisplayCheck(VOID) {
  EFI_HANDLE *handles=NULL;UINTN count=0,physical=0,matched=0,others=0,targetGops=0;
  OTHER_DISPLAY snapshot[8];memset(snapshot,0,sizeof(snapshot));
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(status==EFI_NOT_FOUND && ExpectDetached)return OtherCount==0 && GuLogReady();
  if(EFI_ERROR(status) || count>128 || (count && !handles)){if(handles)GuFree(handles);return FALSE;}
  for(UINTN i=0;i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
    status=Gu.bs->HandleProtocol(handles[i],&GopGuid,(VOID **)&g);
    if(EFI_ERROR(status) || !g || !g->Mode){GuLog("display substep=gop handle=%#zx status=%#llx\n",(size_t)handles[i],(unsigned long long)status);status=EFI_COMPROMISED_DATA;break;}
    if(!g->Mode->FrameBufferBase && !g->Mode->FrameBufferSize) {
      /* Count framebuffer-less GOP children of the target so matched==0 can be a
       * verified state instead of a blind pass. Resolution failures here remain
       * informational. */
      EFI_DEVICE_PATH_PROTOCOL *rpath=NULL;EFI_HANDLE rparent=NULL;
      EFI_STATUS rs=Gu.bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&rpath);
      if(!EFI_ERROR(rs)&&rpath) {
        rs=Gu.bs->LocateDevicePath(&PciGuid,&rpath,&rparent);
        if(!EFI_ERROR(rs)&&rparent==Gu.handle)targetGops++;
      }
      continue;
    }
    physical++;
    status=Gu.bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&path);
    if(EFI_ERROR(status) || !path){GuLog("display substep=path handle=%#zx status=%#llx\n",(size_t)handles[i],(unsigned long long)status);status=EFI_UNSUPPORTED;break;}
    PathLog("display gop handle",path);
    EFI_HANDLE parent=NULL;status=Gu.bs->LocateDevicePath(&PciGuid,&path,&parent);
    if(EFI_ERROR(status) || !parent){GuLog("display substep=parent handle=%#zx status=%#llx\n",(size_t)handles[i],(unsigned long long)status);status=EFI_UNSUPPORTED;break;}
    GuLog("display gop[%zu] parent=%#zx target_handle=%#zx\n",i,(size_t)parent,(size_t)Gu.handle);
    if(parent==Gu.handle)targetGops++;
    if(!g->Mode->Info || g->Mode->SizeOfInfo<sizeof(*g->Mode->Info) ||
       !g->Mode->FrameBufferSize || !g->Mode->Info->VerticalResolution ||
       !g->Mode->Info->HorizontalResolution || g->Mode->Info->PixelFormat>PixelBlueGreenRedReserved8BitPerColor ||
       g->Mode->Info->PixelsPerScanLine<g->Mode->Info->HorizontalResolution ||
       (UINT64)g->Mode->Info->PixelsPerScanLine*g->Mode->Info->VerticalResolution>g->Mode->FrameBufferSize/4) {
      status=EFI_COMPROMISED_DATA;break;
    }
    if(parent==Gu.handle)matched++;
    else {
      EFI_PCI_IO_PROTOCOL *other=NULL;UINT32 id=0;
      status=Gu.bs->HandleProtocol(parent,&PciGuid,(VOID**)&other);
      if(EFI_ERROR(status) || !other || others>=8){GuLog("display substep=parent-pci parent=%#zx status=%#llx others=%zu\n",(size_t)parent,(unsigned long long)status,others);status=EFI_UNSUPPORTED;break;}
      status=other->Pci.Read(other,EfiPciIoWidthUint32,0,1,&id);
      /* The only independent physical display devices on this machine. */
      if(EFI_ERROR(status) || id!=0xe2128086U){status=EFI_UNSUPPORTED;break;}
      OTHER_DISPLAY *s=&snapshot[others++];
      s->handle=handles[i];s->parent=parent;s->gop=g;
      s->base=g->Mode->FrameBufferBase;s->size=g->Mode->FrameBufferSize;
      s->width=g->Mode->Info->HorizontalResolution;s->height=g->Mode->Info->VerticalResolution;
      s->stride=g->Mode->Info->PixelsPerScanLine;s->format=g->Mode->Info->PixelFormat;
    }
  }
  if(!EFI_ERROR(status)) {
    if(!DisplaysLatched && !ExpectDetached) {
      memcpy(OtherDisplays,snapshot,sizeof(snapshot));OtherCount=others;DisplaysLatched=TRUE;
    } else {
      if(others!=OtherCount)status=EFI_COMPROMISED_DATA;
      for(UINTN i=0;i<others;i++) {
        BOOLEAN found=FALSE;
        for(UINTN j=0;j<OtherCount;j++)if(!memcmp(&snapshot[i],&OtherDisplays[j],sizeof(snapshot[i])))found=TRUE;
        if(!found)status=EFI_COMPROMISED_DATA;
      }
    }
  }
  GuFree(handles);
  GuLog("GOP handles=%zu physical=%zu target=%zu target_gops=%zu expect_detached=%u status=%#llx\n",count,(size_t)physical,(size_t)matched,(size_t)targetGops,
    ExpectDetached,(unsigned long long)status);
  /* matched>=1 encodes the reference machine's target display children. A headless
   * compute card whose target exposes no GOP children at all is a verified safe
   * state, not a missing prerequisite: accept matched==0 only when target_gops==0.
   * The independent-display snapshot and the ownership gating remain unchanged. */
  return !EFI_ERROR(status) && (ExpectDetached?matched==0:(matched<=8 && (matched>=1 || targetGops==0))) && GuLogReady();
}
#include "handoff.inc"
static EFI_STATUS ReadConfig(UINT32 *Config) {
  for(UINTN i=0;i<64;i++)if(EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32,(UINT32)i*4,&Config[i]))) {
    GuLog("PCI config read failed: offset=%#x valid_bytes=%u; snapshot incomplete\n",(unsigned)(i*4),(unsigned)(i*4));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS ConfigCheck(BOOLEAN Save) {
  UINT32 config[64];GU_CAPS caps;UINTN segment,bus,device,function;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  GuLog("target location: status=%#llx\n",(unsigned long long)status);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(config);if(EFI_ERROR(status))return status;
  BOOLEAN checked=GuCheckConfig(config,segment,bus,device,function,&caps);
  /* Validate discovered BAR/capability layout, then pin the whole snapshot.
   * Firmware-assigned MMIO addresses differ from the reference motherboard. */
  BOOLEAN valid=checked && config[2]==0x030000a1 &&
    !(config[4]&1U) && ((config[4]&6U)==0 || (config[4]&6U)==4) &&
    !(config[(caps.pcie+8)/4]&0x00200000U);
  BOOLEAN same=Save || !memcmp(config,InitialConfig,sizeof(config));
  if(Save || !valid || !same)ConfigLog(config);
  GuLog("target BDF=%zu:%zu:%zu.%zu checks=%u pm=%#x pcie=%#x msi=%#x msix=%#x\n",
    (size_t)segment,(size_t)bus,(size_t)device,(size_t)function,checked,
    (unsigned)caps.pm,(unsigned)caps.pcie,(unsigned)caps.msi,(unsigned)caps.msix);
  if(!valid)return EFI_UNSUPPORTED;
  if(!same)return EFI_NOT_READY;
  if(Save)memcpy(InitialConfig,config,sizeof(config));
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
}
static EFI_STATUS RestoreDecode(VOID) {
  UINT32 current[64],normalized[64],after[64];UINTN segment,bus,device,function;
  if(!Gu.pci || !Gu.owned || !Gu.armed || !Gu.display_ok || Gu.started || Gu.allocations ||
    Gu.dma_count || Gu.fault || Gu.retained)return EFI_ACCESS_DENIED;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(current);if(EFI_ERROR(status))return status;
  GuLogText("config before decode restore (256 valid bytes):\n");ConfigLog(current);
  if(!GuTargetConfig(current,segment,bus,device,function))return EFI_UNSUPPORTED;
  UINT32 missing=InitialConfig[1]&~current[1]&3U;
  memcpy(normalized,current,sizeof(normalized));normalized[1]|=missing;
  /* Restore only cleared, previously enabled IO/MEM bits. Every other bit,
     including BME, status, BAR, ROM and PM/capability state must be unchanged. */
  if(memcmp(normalized,InitialConfig,sizeof(normalized))) {
    GuLogText("decode restore refused: config differs beyond cleared original IO/MEM bits\n");
    return EFI_UNSUPPORTED;
  }
  if(!missing){GuLogText("decode restore not needed\n");return EFI_SUCCESS;}
  if(!Gu.pci->Attributes)return EFI_UNSUPPORTED;
  UINT64 needed=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  UINT64 supported=0,attributes=0;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationSupported,0,&supported);
  GuLog("decode supported attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)supported);
  if(EFI_ERROR(status))return status;
  if((supported&needed)!=needed)return EFI_UNSUPPORTED;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes);
  GuLog("decode current attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)attributes);
  if(EFI_ERROR(status))return status;
  GuLog("decode restore intent: saved_command=%#x current_command=%#x missing=%#x attributes=%#llx\n",
    InitialConfig[1]&0xffff,current[1]&0xffff,missing,(unsigned long long)needed);
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  /* Never use EFI_PCI_DEVICE_ENABLE: it would also enable bus mastering. */
  EFI_STATUS enabled=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationEnable,needed,NULL);
  GuLog("decode enable status=%#llx\n",(unsigned long long)enabled);
  if(!GuLogReady() || Gu.fault)return EFI_ERROR(enabled)?enabled:EFI_DEVICE_ERROR;
  /* Even an error may follow a partial hardware effect. Capture it if readable,
     but never turn an API error into success merely because readback matches. */
  status=ReadConfig(after);
  BOOLEAN matched=FALSE;
  if(!EFI_ERROR(status)) {
    GuLogText("config after decode restore (256 valid bytes):\n");ConfigLog(after);
    matched=!memcmp(after,InitialConfig,sizeof(after));GuLog("decode readback matched=%u\n",matched);
  }
  if(EFI_ERROR(enabled))return enabled;
  if(EFI_ERROR(status))return status;
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  return matched?EFI_SUCCESS:EFI_COMPROMISED_DATA;
}
static EFI_STATUS FindTarget(VOID) {
  EFI_HANDLE *handles=NULL;UINTN count=0,found=0;
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&handles);
  GuLog("PCI enumeration: status=%#llx handles=%zu\n",(unsigned long long)status,(size_t)count);
  /* LocateHandleBuffer supplies a pool-backed array sized to Count, across
   * all PCI root bridges. There is no fixed local handle array to overflow. */
  if(EFI_ERROR(status) || !handles || !count){if(handles)GuFree(handles);return EFI_NOT_FOUND;}
  for(UINTN i=0;i<count;i++) {
    EFI_PCI_IO_PROTOCOL *pci=NULL;UINT32 id=0;
    status=Gu.bs->HandleProtocol(handles[i],&PciGuid,(VOID **)&pci);
    if(!EFI_ERROR(status) && pci)status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id);
    if(EFI_ERROR(status) || !pci){GuLog("PCI scan failed: index=%zu status=%#llx\n",(size_t)i,(unsigned long long)status);status=EFI_DEVICE_ERROR;break;}
    if(id==GU_BOARD_ID){found++;Gu.handle=handles[i];Gu.pci=pci;}
  }
  GuFree(handles);GuLog("PCI target matches=%zu status=%#llx\n",(size_t)found,(unsigned long long)status);
  if(EFI_ERROR(status) || found!=1)return EFI_UNSUPPORTED;
  return ConfigCheck(TRUE);
}
static EFI_STATUS BarCheck(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r=NULL;
  EFI_STATUS status=Gu.pci->GetBarAttributes(Gu.pci,0,NULL,(VOID **)&r);
  GuLog("bar0 query: status=%#llx\n",(unsigned long long)status);
  if(!EFI_ERROR(status)) {
    if(r && r->Desc==ACPI_ADDRESS_SPACE_DESCRIPTOR && r->Len==sizeof(*r)-3)
      GuLog("bar0 descriptor: type=%u granularity=%llu min=%#llx max=%#llx length=%#llx translation=%#llx\n",
        (unsigned)r->ResType,(unsigned long long)r->AddrSpaceGranularity,(unsigned long long)r->AddrRangeMin,
        (unsigned long long)r->AddrRangeMax,(unsigned long long)r->AddrLen,(unsigned long long)r->AddrTranslationOffset);
    if(!r || r->Desc!=ACPI_ADDRESS_SPACE_DESCRIPTOR || r->Len!=sizeof(*r)-3 || r->ResType!=0 ||
      !r->AddrRangeMin || (r->AddrRangeMin&0xffffff) || r->AddrLen!=0x1000000 || r->AddrTranslationOffset ||
      r->AddrRangeMin!=((UINT64)(InitialConfig[4]&~15U) | (((InitialConfig[4]&6U)==4)?((UINT64)InitialConfig[5]<<32):0)) ||
      /* UEFI describes an inclusive end; pinned EDK2 PciBusDxe instead returns
         PciBar.Alignment. Both representations describe this exact 16 MiB BAR. */
      (r->AddrRangeMax!=r->AddrRangeMin+0xffffff && r->AddrRangeMax!=0x00ffffff) ||
      (r->AddrSpaceGranularity!=32 && r->AddrSpaceGranularity!=64) || ((UINT8 *)(r+1))[0]!=ACPI_END_TAG_DESCRIPTOR)status=EFI_UNSUPPORTED;
    else {
      UINT8 *raw=(UINT8 *)r,sum=0;
      for(UINTN i=0;i<sizeof(*r)+2;i++)sum=(UINT8)(sum+raw[i]);
      if(raw[sizeof(*r)+1] && sum)status=EFI_COMPROMISED_DATA;
      else {Gu.bar_base=r->AddrRangeMin;Gu.bar_size=r->AddrLen;}
    }
  }
  if(r)GuFree(r);return status;
}
static EFI_STATUS ColdCheck(VOID) {
  UINT32 chip=GuRead32(0);GuLog("chip: actual=%#x expected=%#x\n",chip,GU_BOARD_CHIP);
  if(chip!=GU_BOARD_CHIP)return EFI_UNSUPPORTED;
  struct gpu_unlock_gfw_sample s={.config_ok=true,.plm=GuRead32(GU_GFW_PLM_OFFSET),
    .boot=GuRead32(GU_GFW_BOOT_OFFSET),.cpuctl=GuRead32(GU_GFW_CPUCTL_OFFSET)};
  UINT32 hi=GuRead32(0x1fa828),plm=GuRead32(GU_FEAT_PLM_OFFSET),ss0=GuRead32(GU_FEAT_SS0_OFFSET),ss1=GuRead32(GU_FEAT_SS1_OFFSET);
  GuLog("cold state: chip=%#x plm=%#x gfw=%#x/%#x/%#x WPR2_HI=%#x SS0=%#x SS1=%#x\n",
    GU_BOARD_CHIP,plm,s.plm,s.boot,s.cpuctl,hi,ss0,ss1);
  UINT32 wpr_cfg=GuRead32(0x1fa7cc),fbpa=GuRead32(0x9a0148),wpr=GuRead32(0x1fa7c4);
  GuLog("cold PLMs: WPR_CFG=%#x FBPA=%#x WPR=%#x FEAT=%#x\n",wpr_cfg,fbpa,wpr,plm);
  /* Diagnostic reads requested for RUN07 follow-up. The meaning/bit map of
     0x823b00 is unverified on AD103: do not decode it or write it.
     0x8403c4 is the reset-PLM candidate at the standard Falcon +0x3c4.
     Neither raw value is a new execution gate. Transport faults still latch. */
  UINT32 source_raw=GuRead32(0x823b00),reset_plm=GuRead32(0x8403c4);
  GuLog("cold diagnostic: reg=0x823b00 name=SOURCE_ENABLE_CANDIDATE_RAW value=%#x decoded=0\n",source_raw);
  GuLog("cold diagnostic: reg=0x8403c4 name=RESET_PLM_CANDIDATE_RAW value=%#x decoded=0\n",reset_plm);
  UINT32 vram=GuRead32(0x1183a4);
  /* Require a 24GiB device with only a small firmware reservation.
     The transaction computes its layout from the exact register value. */
  GuLog("VRAM register=%#x (%u MiB); accepted=24GiB minus at most 16MiB firmware reservation; layout uses actual register\n",vram,vram);
  if(Gu.fault || gpu_unlock_gfw_assess(&s) || hi>>4 || plm!=0xffffff8f ||
    (ss0&0x88888888U) || (ss1&~7U) || vram<0x5ff0 || vram>0x6000)return EFI_NOT_READY;
  return Gu.fault?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static VOID LogHash(const char *Name,const VOID *Data,UINTN Bytes) {
  static const char digits[]="0123456789abcdef";
  UINT8 digest[32];char hex[65];GuSha256(Data,Bytes,digest);
  for(UINTN i=0;i<32;i++){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}
  hex[64]=0;GuLog("%s bytes=%zu SHA256=%s\n",Name,(size_t)Bytes,hex);
}
static EFI_STATUS PromCheck(VOID) {
  if(!Gu.pci->RomImage || Gu.pci->RomSize!=0x24a00 ||
     !GuHashMatches(Gu.pci->RomImage,0x24a00,"a6b431683699d4887126515a2147fe2f9d9f9b48372e84ab166906745a51e2f2"))return EFI_CRC_ERROR;
  VOID *prom=GuAlloc(1048576);if(!prom)return EFI_OUT_OF_RESOURCES;
  GuCopyFromIo(prom,(VOID *)(UINTN)(Gu.bar_base+GU_PROM_BAR_OFFSET),1048576);
  BOOLEAN good=!Gu.fault && GuHashMatches(prom,GU_PROM_VIEW_BYTES,GU_PROM_VIEW_SHA256);
  GuLog("PCI RomImage hash matched=1; PROM read width=32 io_fault=%u\n",(unsigned)Gu.fault);
  LogHash("live PROM",prom,1048576);
  LogHash("PROM verified view [0,0xd6c00)",prom,GU_PROM_VIEW_BYTES);
  GuLog("live PROM view hash matched=%u bar_offset=%#x bytes=%#x expected=%s\n",
    (unsigned)good,GU_PROM_BAR_OFFSET,GU_PROM_VIEW_BYTES,GU_PROM_VIEW_SHA256);
  if(!good) {
    UINT32 *p=prom;
    GuLog("PROM prefix: %08x %08x %08x %08x %08x %08x %08x %08x\n",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
    LogHash("PROM UEFI slice +0",prom,0x24a00);
    LogHash("PROM FWSEC image +0x44168",(UINT8 *)prom+0x44168,0x10000);
    EFI_STATUS saved=GuSavePromDump(prom,1048576);
    GuLog("PROM.BIN save status=%#llx bytes=1048576 complete_read=%u; mismatch remains blocking\n",
      (unsigned long long)saved,(unsigned)!Gu.fault);
  }
  GuFree(prom);return good?EFI_SUCCESS:Gu.fault?EFI_DEVICE_ERROR:EFI_CRC_ERROR;
}
static EFI_STATUS Arm(VOID) {
  if(!Table->ConIn || !Table->ConIn->Reset || !Table->ConIn->ReadKeyStroke)return EFI_UNSUPPORTED;
  EFI_STATUS status=Table->ConIn->Reset(Table->ConIn,FALSE);if(EFI_ERROR(status))return status;
  GuLogText("READY: RTX 4090 AD102 24GB / LOCAL USB UNLOCK TRIAL / Windows preloaded.\n");
  if(PendingOwner.agent)GuLogText("U will release the RTX firmware driver, restore verified access settings if needed, then attempt WPR_CFG -> FBPA -> WPR -> FEAT in ONE SEC2 launch, then SS0/SS1 if required gates pass; log mismatches and recover.\n");
  GuLogText("Press U once to start now. Auto-attempt after 5s unless you press Esc to skip today. One SEC2 fire containing four ordered PLM writes, then selectors/FLR/Windows on full success. The last frame may remain visible. Power OFF on STOP.\n");
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  for(UINTN i=0;i<KOH_AUTOCONFIRM_MS/100;i++) {
    EFI_INPUT_KEY key;status=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(!EFI_ERROR(status)) {
      if(key.ScanCode==0x17 || key.UnicodeChar==27)return EFI_ABORTED;
      if(key.UnicodeChar=='u' || key.UnicodeChar=='U')return EFI_SUCCESS;
    } else if(status!=EFI_NOT_READY)return status;
    GuStall(100000);
  }
  GuLogText("No key pressed: auto-attempt starting now (Esc was the skip option).\n");
  return EFI_SUCCESS;
}
static VOID PausePreflightFailure(VOID) {
  if(GuScreenDetached || Gu.started || !Table->ConIn || !Table->ConIn->ReadKeyStroke)return;
  if(Table->ConIn->Reset)Table->ConIn->Reset(Table->ConIn,FALSE);
  GuConsole("Preflight stopped before unlock. See the FINAL stage above and REPORT.TXT.\n");
  GuConsole("Press Enter or Esc to return to firmware; automatic return in 60 seconds. U cannot start an attempt here.\n");
  for(UINTN i=0;i<600;i++) {
    EFI_INPUT_KEY key;EFI_STATUS s=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(!EFI_ERROR(s) && (key.ScanCode==0x17 || key.UnicodeChar==27 || key.UnicodeChar=='\r'))break;
    if(EFI_ERROR(s) && s!=EFI_NOT_READY)break;
    Gu.bs->Stall(100000);
  }
}
static VOID Halt(VOID) {
  Gu.retained=TRUE;
  GuLogText("STOP: cleanup cannot be proven. Resources remain reserved.\nPOWER OFF using the physical power button; do not continue to Windows.\n");
  /* No ResetSystem, ExitBootServices, retry, protocol close or memory release. */
  for(;;)Gu.bs->Stall(1000000);
}
static VOID LogContainment(VOID) {
  /* Called only after any TPL_NOTIFY containment section has ended. */
  GuLog("BME containment: attribute_status=%#llx hardware_status=%#llx command=%#x cached=%#llx pending=%u\n",
    (unsigned long long)Gu.master_attribute_status,(unsigned long long)Gu.master_hardware_status,
    Gu.master_last_command,(unsigned long long)Gu.master_last_attributes,Gu.master_attributes_pending);
}
static VOID ReleaseHalt(EFI_STATUS Status,BOOLEAN Attempted) {
  /* A failed claim may follow interface removal/replacement during logging.
     Resolve, validate and clear BME without a callback window or stale access. */
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS refreshed=GuRefreshTargetPci();
  EFI_STATUS contained=EFI_ERROR(refreshed)?refreshed:GuClearMaster();
  Gu.bs->RestoreTPL(old);
  LogContainment();
  GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=1 clean=0 containment=%#llx pci_available=%u\n",
    (unsigned long long)Status,Stage,Attempted,(unsigned long long)contained,(unsigned)(Gu.pci!=NULL));
  Halt();
}
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE Image,EFI_SYSTEM_TABLE *St) {
#ifndef KOH_ENABLE_EXPERIMENT
  (void)Image;(void)St;return EFI_UNSUPPORTED;
#endif
  if(!St || !St->BootServices || !St->RuntimeServices)return EFI_INVALID_PARAMETER;
  WindowsImage=NULL;WindowsVolume=NULL;WindowsDevicePath=NULL;GuScreenDetached=FALSE;ExpectDetached=FALSE;
  DisplaysLatched=FALSE;OtherCount=0;memset(OtherDisplays,0,sizeof(OtherDisplays));
  Table=St;GuInit(St->BootServices,NULL);Gu.image=Image;Gu.display_check=DisplayCheck;
  memset(&PendingOwner,0,sizeof(PendingOwner));
  Stage="usb-log";EFI_STATUS status=GuIoOpen(Image,St);BOOLEAN attempted=FALSE,release_attempted=FALSE;
  if(EFI_ERROR(status))goto finish;
  UINT8 secure=0xff;UINTN bytes=1;
  Stage="secure-boot";
  status=St->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,NULL,&bytes,&secure);
  if(EFI_ERROR(status) || bytes!=1 || secure>1){status=EFI_SECURITY_VIOLATION;goto finish;}
  if(secure) {
    UINT8 setup=0xff;bytes=1;
    status=St->RuntimeServices->GetVariable(L"SetupMode",&GlobalGuid,NULL,&bytes,&setup);
    if(EFI_ERROR(status) || bytes!=1 || setup!=0){status=EFI_SECURITY_VIOLATION;goto finish;}
  }
  /* Firmware authenticates this signed image before entry; never override
   * LoadImage authentication or alter PK/KEK/db/dbx here. */
  GuLog("boot policy: secure_boot=%u firmware_authentication_not_overridden=1\n",secure);
  status=Step(status);if(EFI_ERROR(status))goto finish;
  Stage="watchdog";status=Step(Gu.bs->SetWatchdogTimer(0,0,0,NULL));if(EFI_ERROR(status))goto finish;
  Stage="target";status=Step(FindTarget());if(EFI_ERROR(status))goto finish;
  EFI_STATUS preflight_error=EFI_SUCCESS;const char *failed_stage=NULL;
#define COLLECT() do { status=Step(status); if(EFI_ERROR(status) && !EFI_ERROR(preflight_error)) { preflight_error=status;failed_stage=Stage; } } while(0)
  Stage="display";Gu.display_ok=DisplayCheck();status=Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED;COLLECT();
  Stage="ownership";status=GuClaimController();
  if(status==EFI_ACCESS_DENIED) {
    status=GuFindReleaseOwner(St,&PendingOwner,TRUE);
    if(!EFI_ERROR(status))GuLogText("Exclusive ownership deferred until manual U and explicit driver release.\n");
  }
  if(!EFI_ERROR(status) && !PendingOwner.agent)status=EFI_UNSUPPORTED;
  COLLECT();
  Stage="config-recheck";status=ConfigCheck(FALSE);COLLECT();
  Stage="bar0";status=BarCheck();BOOLEAN bar_valid=!EFI_ERROR(status);COLLECT();
  if(bar_valid && !Gu.fault) {
    Stage="cold-state";status=ColdCheck();COLLECT();
    if(!Gu.fault){Stage="prom";status=PromCheck();COLLECT();}
  } else GuLogText("SKIP cold-state/PROM: valid BAR access prerequisite unavailable\n");
  Stage="assets";status=GuLoadAssets();COLLECT();
  Stage="windows-preload";status=PreloadWindows();COLLECT();
  if(EFI_ERROR(preflight_error)) {
    GuLogText("All independent preflight checks completed; no driver release or firmware execution.\n");
    status=preflight_error;Stage=failed_stage;goto finish;
  }
#undef COLLECT
  Stage="arming";status=Step(Arm());if(EFI_ERROR(status))goto finish;
  Stage="armed-config";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
  Stage="armed-display";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
  Stage="armed-cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  if(PendingOwner.agent) {
    GU_RELEASE_OWNER current;
    Stage="owner-recheck";
    GuLogText("Manual U confirmed: release RTX firmware driver once, then revalidate.\n");
    if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
    /* Durable intent precedes the last owner snapshot. No file IO between a
       successful final comparison and DisconnectController. */
    status=GuFindReleaseOwner(St,&current,FALSE);
    if(!EFI_ERROR(status) && !GuSameReleaseOwner(&PendingOwner,&current))status=EFI_ABORTED;
    if(EFI_ERROR(status)){status=Step(status);goto finish;}
    EFI_PCI_IO_PROTOCOL *prior=Gu.pci;
    Stage="driver-release";release_attempted=TRUE;Gu.armed=TRUE;Gu.pci=NULL;
    GuScreenDetached=TRUE;ExpectDetached=TRUE;
    EFI_STATUS released=Gu.bs->DisconnectController(Gu.handle,current.agent,NULL);
    EFI_TPL refresh_tpl=Gu.bs->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS refreshed=GuRefreshTargetPci();
    Gu.bs->RestoreTPL(refresh_tpl);
    GuLog("driver release: status=%#llx refresh=%#llx same_pci=%u\n",
      (unsigned long long)released,(unsigned long long)refreshed,(unsigned)(Gu.pci==prior));
    status=Step(EFI_ERROR(released)?released:EFI_ERROR(refreshed)?refreshed:Gu.pci!=prior?EFI_COMPROMISED_DATA:EFI_SUCCESS);
    if(EFI_ERROR(status))goto finish;
    Stage="ownership-after-release";status=Step(GuClaimController());if(EFI_ERROR(status))goto finish;
    Stage="display-after-release";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
    Stage="decode-after-release";status=Step(RestoreDecode());if(EFI_ERROR(status))goto finish;
    Stage="config-after-release";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
    Stage="bar0-after-release";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
    Stage="cold-after-release";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
    Stage="prom-after-release";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  }
  GuLogText("ARMED: one transaction is starting. Do not reset or remove the USB.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
  Stage="transaction";Gu.armed=TRUE;attempted=TRUE;
  UINT64 mask=0xffffffffULL;
  struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xffffffffULL},.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  int result=gpu_unlock_610_load_shared(&device,&shared);
  if(!result)result=gpu_unlock_610_unlock(&device,&shared);
  /* Also covers failures before GuStarted, including BME-enable readback. */
  EFI_STATUS contained=GuClearMaster();
  LogContainment();
  GuLog("transaction result=%d containment=%#llx started=%u quiesced=%u fault=%u retained=%u maps=%u FLR=%u\n",
    result,(unsigned long long)contained,Gu.started,Gu.quiesced,Gu.fault,Gu.retained,Gu.dma_count,Gu.flrs);
  if(EFI_ERROR(contained) || !GuSafeToReturn() || Gu.allocations || (Gu.started && result))Halt();
  gpu_unlock_610_release_shared(&shared);Gu.armed=FALSE;
  status=result?EFI_DEVICE_ERROR:EFI_SUCCESS;
finish:
  if(!EFI_ERROR(status) && attempted && release_attempted) {
    Stage="post-unlock-handoff";status=RestoreDisplayForWindows(PendingOwner.agent);
  }
  if(release_attempted && (EFI_ERROR(status) || Gu.fault))ReleaseHalt(status,attempted);
  if(attempted && !GuSafeToReturn())Halt();
  if(GuLogReady())GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=%u clean=%u\n",(unsigned long long)status,Stage,attempted,release_attempted,GuSafeToReturn());
  else GuConsole("Preflight or logging failed; no new attempt will start.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;if(release_attempted)ReleaseHalt(status,attempted);if(Gu.started)Halt();}
  if(attempted && !GuSafeToReturn())Halt();
  EFI_STATUS closed=GuIoClose();
  if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}
  if(Gu.owned) {
    closed=Gu.bs->CloseProtocol(Gu.handle,&PciGuid,Image,NULL);
    if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}else Gu.owned=FALSE;
  }
  GuReleaseAssets();
  if(!EFI_ERROR(status) && attempted) {
    GuConsole("GPU restored after FLR and GOP reconnect; selector outcome logged. Starting Windows.\n");
    UINTN bytes=0;CHAR16 *data=NULL;
    status=Gu.bs->StartImage(WindowsImage,&bytes,&data);
    if(data)GuFree(data);
    /* Returning after a hardware attempt could permit accidental rerun. */
    GuScreenDetached=TRUE;Halt();
  }
  else GuConsole("Attempt cancelled or rejected. Returning without another attempt.\n");
  if(WindowsImage)Gu.bs->UnloadImage(WindowsImage);
  GuFree(WindowsDevicePath);WindowsDevicePath=NULL;
  if(EFI_ERROR(status) && status!=EFI_ABORTED && status!=EFI_TIMEOUT && !release_attempted)PausePreflightFailure();
  return status;
}
