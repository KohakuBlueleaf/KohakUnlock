#include "local_profile.h"
/* The actual app, core, compatibility and port run together; firmware services are fake. */
#define main transaction_fixture_main
#define GuLog FixtureLog
#define GuLogText FixtureLogText
#include "test_efi_transaction.c"
#undef main
#undef GuLog
#undef GuLogText
#include "ad102_cases.h"
#include "../app.h"
#include "../app_io.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE,EFI_SYSTEM_TABLE*);
static int extra_case,windows_loaded,windows_started,connect_calls;
static struct { HARDDRIVE_DEVICE_PATH hd; EFI_DEVICE_PATH_PROTOCOL end; } local_esp_path;
static struct { HARDDRIVE_DEVICE_PATH hd; EFI_DEVICE_PATH_PROTOCOL end; } wrong_esp_path;
static struct { HARDDRIVE_DEVICE_PATH hd; EFI_DEVICE_PATH_PROTOCOL end; } payload_esp_path;
static struct { HARDDRIVE_DEVICE_PATH hd; EFI_DEVICE_PATH_PROTOCOL end; } esp_only_path;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL esp_only_fs;
static struct { HARDDRIVE_DEVICE_PATH hd; EFI_DEVICE_PATH_PROTOCOL end; } payload2_path;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL payload2_fs;
static EFI_LOADED_IMAGE_PROTOCOL windows_loaded_image;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL windows_fs;
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>
#include <IndustryStandard/Acpi.h>
#include <setjmp.h>
enum app_case { APP_SUCCESS, APP_ESCAPE, APP_TIMEOUT, APP_OLD_KEY, APP_OWNER,
 APP_SECUREBOOT, APP_DUPLICATE, APP_BAD_BAR, APP_BAD_GOP, APP_RTX_GOP,
 APP_NO_USB, APP_SHORT_WRITE, APP_FLUSH, APP_BAD_SIZE, APP_BAD_HASH,
 APP_BAD_PROM, APP_WATCHDOG, APP_BME, APP_DIRTY, APP_UNMAP, APP_LOG_DIRTY,
 APP_CONFIG, APP_OWNER_RACE, APP_SELECTOR, APP_FINAL_LOG, APP_CLOSE, APP_BAR_MAX, APP_BAR_CHECKSUM,
 APP_EDK2_BAR, APP_BAD_ALIGNMENT, APP_ZERO_INFO, APP_QUERY_ERROR, APP_QUERY_SHORT,
 APP_QUERY_NULL, APP_QUERY_RGB, APP_ZERO_PHYSICAL, APP_SHORT_INFO, APP_QUERY_MISSING,
 APP_QUERY_FREE, APP_QUERY_MODE_CHANGE, APP_QUERY_FB_CHANGE, APP_QUERY_RECHECK,
 APP_QUERY_FORWARD, APP_QUERY_FORWARD_REVERSED, APP_QUERY_OTHER_CONSOLE, APP_QUERY_GEOMETRY, APP_QUERY_LATE,
 APP_CACHED_RGB, APP_FWSEC_NO_WPR, APP_FWSEC_PIO_UNAVAILABLE, APP_FWSEC_DIAG_LOG,
 APP_HANDOFF, APP_HANDOFF_ESCAPE, APP_HANDOFF_TIMEOUT, APP_HANDOFF_MULTI,
 APP_HANDOFF_BINDING, APP_HANDOFF_IMAGE, APP_HANDOFF_CHILD, APP_HANDOFF_BAD_CHILD,
 APP_HANDOFF_OTHER_CHILD, APP_HANDOFF_CHANGED, APP_HANDOFF_FAIL, APP_HANDOFF_NOOP,
 APP_HANDOFF_REBIND, APP_HANDOFF_CONFIG, APP_HANDOFF_DISPLAY, APP_HANDOFF_PROM,
 APP_HANDOFF_MISSING_PCI, APP_HANDOFF_REPLACED_PCI, APP_HANDOFF_BME,
 APP_HANDOFF_LOG, APP_HANDOFF_CLOSE, APP_HANDOFF_LATE_MISSING, APP_HANDOFF_LATE_REPLACED,
 APP_DECODE_BOTH, APP_DECODE_IO, APP_DECODE_OTHER, APP_DECODE_BME, APP_DECODE_GET_ERROR,
 APP_DECODE_UNSUPPORTED, APP_DECODE_ENABLE_ERROR, APP_DECODE_NOOP, APP_DECODE_PARTIAL,
 APP_DECODE_EXTRA_BME, APP_DECODE_LOG_FAIL, APP_DECODE_READ_FAIL, APP_DECODE_NO_API,
 APP_DECODE_SUPPORT_ERROR, APP_DECODE_ENABLE_ERROR_MATCH,
 APP_MASTER_ENABLE_ERROR, APP_MASTER_DISABLE_ERROR, APP_MASTER_ENABLE_ERROR_CLEAR_BAD,
 APP_ESP_BOOT, APP_ESP_WRONG_GUID, APP_PAYLOAD_BOOT, APP_ESP_NO_ASSETS, APP_PAYLOAD_RENUM,
 APP_PAYLOAD_MULTI, APP_COUNT };
static const char *app_names[]={"success","escape","timeout","old-key","driver-owner",
 "secure-boot","duplicate-target","bad-bar","unknown-gop","rtx-gop","not-usb",
 "partial-report-write","report-flush","asset-size","asset-hash","prom-hash",
 "watchdog","bme-readback","fwsec-timeout","unmap-failure","log-after-start","config-guard","owner-notify-race",
 "selector-lost","final-log-failure","report-close-failure","bar-max","bar-checksum","edk2-bar-alignment","bad-bar-alignment",
 "console-zero-info","console-query-error","console-query-short","console-query-null","console-query-rgb",
 "physical-zero-info","console-short-info","console-no-query","console-query-free-error",
 "console-query-mode-change","console-query-framebuffer-change","console-query-recheck",
 "console-forwarded-intel","console-forwarded-reversed","console-forwarded-wrong-handle",
 "console-forwarded-wrong-geometry","console-query-after-start","console-cached-rgb",
 "pio-unavailable-no-wpr-retains","pio-unavailable-fwsec-success","fwsec-diagnostic-log-failure",
 "handoff-success","handoff-escape","handoff-timeout","handoff-multiple-owner",
 "handoff-bad-binding","handoff-bad-image","handoff-child-refused","handoff-intel-child",
 "handoff-other-protocol-child","handoff-owner-changed","handoff-stop-failure","handoff-stop-noop",
 "handoff-new-owner","handoff-memory-decode-restored","handoff-display-change","handoff-prom-change",
 "handoff-pci-missing","handoff-pci-replaced","handoff-bme-failure","handoff-log-failure","handoff-close-failure",
 "handoff-pci-lost-before-claim","handoff-pci-replaced-before-claim",
 "decode-io-memory","decode-io-only","decode-extra-config-change","decode-preexisting-bme",
 "decode-get-error","decode-unsupported","decode-enable-error","decode-enable-noop",
 "decode-partial-result","decode-unwanted-bme","decode-intent-log-failure","decode-readback-error","decode-missing-api",
 "decode-support-query-error","decode-enable-error-matching-readback",
 "master-enable-error-after-write","master-disable-error-retains","master-nonstarted-bad-hardware-cleanup",
 "esp-boot-volume","esp-wrong-guid","payload-boot-volume","esp-load-payload-assets","payload-renumbered",
 "payload-multi-match"};
static enum app_case app_mode;
static EFI_SYSTEM_TABLE st;
static EFI_RUNTIME_SERVICES rt;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL conout;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL conin;
static EFI_LOADED_IMAGE_PROTOCOL loaded;
static EFI_LOADED_IMAGE_PROTOCOL owner_image;
static EFI_DRIVER_BINDING_PROTOCOL owner_binding;
static EFI_PCI_IO_PROTOCOL replacement_pci;
static BOOLEAN bound,late_pci_change;
static UINTN disconnect_calls;
static UINTN attribute_enables;
static UINT64 enabled_attributes;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
static EFI_GRAPHICS_OUTPUT_PROTOCOL gops[2];
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE modes[2];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION infos[2];
static EFI_GUID pg=EFI_PCI_IO_PROTOCOL_GUID,lg=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  dg=EFI_DEVICE_PATH_PROTOCOL_GUID,fg=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
  gg=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,ig=EFI_FILE_INFO_ID,bg=EFI_DRIVER_BINDING_PROTOCOL_GUID;
static const UINT8 intel_path[]={2,1,12,0,0xd0,0x41,3,0xa,0,0,0,0,1,1,6,0,0,2,2,3,8,0,0x11,0x43,1,0x80,0x7f,0xff,4,0};
static const UINT8 rtx_path[]={2,1,12,0,0xd0,0x41,3,0xa,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,0x7f,0xff,4,0};
static UINT8 changed_child_path[]={2,1,12,0,0xd0,0x41,3,0xa,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,1,0,0,0,0x7f,0xff,4,0};
static UINT8 usb_path[]={3,5,6,0,1,0,0x7f,0xff,4,0};
#define H(n) ((EFI_HANDLE)(UINTN)(n))
typedef struct {EFI_FILE_PROTOCOL protocol;int kind;UINTN position;BOOLEAN open;} FILE_FAKE;
static FILE_FAKE files[13]; /* root, parent, run, report, assets, Windows, PROM dump, payload roots (kind 12) */
static UINTN prom_dump_calls;
static GU_ASSET originals[5];
static UINT32 captured_config[64];
static UINTN exclusive_calls,close_protocol_calls,key_calls,watchdog_calls,report_bytes,file_closes;
static UINTN query_calls;
static VOID *query_allocation;
static char report_text[262144];
static EFI_TPL current_tpl;
static BOOLEAN exclusive,halted,key_sent,key_reset,prompt,run_created,owner_raced;
static jmp_buf escape_halt;
static int guid(EFI_GUID *a,EFI_GUID *b){return !memcmp(a,b,sizeof(*a));}
static void narrow(char *d,UINTN n,CHAR16 *s){UINTN i=0;while(i+1<n && s[i]){d[i]=(char)s[i];i++;}d[i]=0;}
static EFI_STATUS EFIAPI app_output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p,CHAR16 *s) {
  CHECK(p==&conout && !GuScreenDetached);char text[2048];narrow(text,sizeof(text),s);
  UINTN used=strlen(fake.log),n=strlen(text);if(n<sizeof(fake.log)-used)memcpy(fake.log+used,text,n+1);
  if(strstr(fake.log,"Press U once"))prompt=TRUE;
  if(strstr(fake.log,"POWER OFF"))halted=TRUE;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI input_reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,BOOLEAN extended) {
  CHECK(p==&conin && !extended);key_reset=TRUE;key_sent=FALSE;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *key) {
  CHECK(p==&conin);key_calls++;
  if(!prompt || key_sent || app_mode==APP_TIMEOUT || app_mode==APP_OLD_KEY || app_mode==APP_HANDOFF_TIMEOUT)return EFI_NOT_READY;
  BOOLEAN cancel=app_mode==APP_ESCAPE || app_mode==APP_HANDOFF_ESCAPE;
  CHECK(key_reset);key_sent=TRUE;key->ScanCode=cancel?0x17:0;
  key->UnicodeChar=cancel?0:'U';return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI app_stall(UINTN n) {
  if(halted || Gu.retained){halted=TRUE;longjmp(escape_halt,1);}return stall(n);
}
static EFI_STATUS EFIAPI watchdog(UINTN timeout,UINT64 code,UINTN bytes,CHAR16 *data) {
  CHECK(!timeout && !code && !bytes && !data);watchdog_calls++;
  return app_mode==APP_WATCHDOG?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_variable(CHAR16 *name,EFI_GUID *g,UINT32 *attributes,UINTN *n,VOID *data) {
  char text[64];narrow(text,sizeof(text),name);CHECK(g && *n==1);
  if(attributes)*attributes=6;
  if(!strcmp(text,"SecureBoot"))*(UINT8 *)data=(app_mode==APP_SECUREBOOT || extra_case==100 || extra_case==101);
  else if(!strcmp(text,"SetupMode")) {
    if(extra_case==101)return EFI_DEVICE_ERROR;
    *(UINT8 *)data=app_mode==APP_SECUREBOOT?1:0;
  } else return EFI_NOT_FOUND;
  *n=1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI other_pci_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *data) {
  CHECK(w==EfiPciIoWidthUint32 && o==0 && n==1);*(UINT32*)data=0x12348086;return EFI_SUCCESS;
}
static EFI_PCI_IO_PROTOCOL other_pci;
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE search,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **h) {
  if(guid(g,&fg)) {
    *n=extra_case==1?2:1;CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,*n*sizeof(**h),(VOID**)h)));
    if(app_mode==APP_ESP_NO_ASSETS||app_mode==APP_PAYLOAD_RENUM)*n=2;
    if(app_mode==APP_PAYLOAD_MULTI)*n=3;
    for(UINTN i=0;i<*n;i++)(*h)[i]=app_mode==APP_PAYLOAD_MULTI?(i==1?H(15):(i==2?H(16):H(12))):
      app_mode==APP_ESP_NO_ASSETS||app_mode==APP_PAYLOAD_RENUM?(i?H(15):H(12)):H(12+i);return EFI_SUCCESS;
  }
  if(guid(g,&pg) && (extra_case==102 || extra_case==103)) {
    *n=extra_case==102?317:4096;CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,*n*sizeof(**h),(VOID**)h)));
    for(UINTN i=0;i<*n-1;i++)(*h)[i]=H(1000+i);(*h)[*n-1]=H(3);return EFI_SUCCESS;
  }
  CHECK(search==ByProtocol && !key);*n=guid(g,&pg)?(app_mode==APP_DUPLICATE?2:1):2;
  CHECK(guid(g,&pg) || guid(g,&gg));CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,*n*sizeof(**h),(VOID **)h)));
  for(UINTN i=0;i<*n;i++)(*h)[i]=guid(g,&pg)?H(3+i):
    app_mode==APP_QUERY_FORWARD_REVERSED?H(6-i):H(5+i);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;
  if((UINTN)h>=20 && (UINTN)h<=23) {
    if(guid(g,&dg)){*p=(VOID*)((extra_case==105 && h==H(21))?intel_path:rtx_path);return EFI_SUCCESS;}
    if(guid(g,&gg)) {
      if(h==H(20)){*p=&gops[0];return EFI_SUCCESS;}
      if(h==H(22)){*p=&gops[1];return EFI_SUCCESS;}
      return EFI_UNSUPPORTED;
    }
    return EFI_UNSUPPORTED;
  }
  if((UINTN)h>=1000 && (UINTN)h<5095 && guid(g,&pg)) {
    other_pci.Pci.Read=other_pci_read;*p=&other_pci;return EFI_SUCCESS;
  }
  if((h==H(12)||h==H(13)) && guid(g,&fg))*p=&windows_fs;
  else if(h==H(15) && guid(g,&fg))*p=&esp_only_fs;
  else if(h==H(16) && guid(g,&fg))*p=&payload2_fs;
  else if(h==H(14) && guid(g,&lg))*p=&windows_loaded_image;
  else if(h==H(1) && guid(g,&lg))*p=&loaded;
  else if(h==H(2) && guid(g,&fg))*p=&fs;
  else if((h==H(3) || h==H(4)) && guid(g,&pg)) {
    if((disconnect_calls && app_mode==APP_HANDOFF_MISSING_PCI) || (late_pci_change && app_mode==APP_HANDOFF_LATE_MISSING))return EFI_NOT_FOUND;
    *p=(disconnect_calls && app_mode==APP_HANDOFF_REPLACED_PCI) || (late_pci_change && app_mode==APP_HANDOFF_LATE_REPLACED)?&replacement_pci:&pci;
  }
  else if(h==H(8) && guid(g,&bg))*p=&owner_binding;
  else if(h==H(9) && guid(g,&lg))*p=&owner_image;
  else if(h==H(10) && guid(g,&gg)) {
    if(extra_case==14)return EFI_UNSUPPORTED;
    *p=extra_case==15?&gops[1]:&gops[0];
  }
  else if((h==H(5) || h==H(6)) && guid(g,&gg))*p=&gops[(UINTN)h-5];
  else if(guid(g,&dg)) {
    if(h==H(12) || h==H(13))*p=&local_esp_path;
    else if(h==H(15))*p=&esp_only_path;
    else if(h==H(16))*p=&payload2_path;
    else if(h==H(2) && app_mode!=APP_NO_USB)*p=app_mode==APP_ESP_BOOT?(VOID*)&local_esp_path:
      app_mode==APP_ESP_WRONG_GUID?(VOID*)&wrong_esp_path:
      app_mode==APP_PAYLOAD_BOOT?(VOID*)&payload_esp_path:
      app_mode==APP_ESP_NO_ASSETS || app_mode==APP_PAYLOAD_RENUM || app_mode==APP_PAYLOAD_MULTI?(VOID*)&local_esp_path:usb_path;
    else if(h==H(5))*p=(VOID *)rtx_path;
    else if(h==H(10))*p=(VOID *)(extra_case==16 && key_sent?changed_child_path:
      extra_case==11 || app_mode==APP_HANDOFF_BAD_CHILD || app_mode==APP_HANDOFF_OTHER_CHILD?intel_path:rtx_path);
    else return EFI_UNSUPPORTED;
  } else return EFI_UNSUPPORTED;
  return *p?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI query_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,UINT32 mode,
    UINTN *bytes,EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info) {
  CHECK(g==&gops[1] && mode==3 && !modes[1].FrameBufferBase && !modes[1].FrameBufferSize);
  query_calls++;*info=NULL;*bytes=0;
  if(app_mode==APP_QUERY_ERROR || (app_mode==APP_QUERY_RECHECK && query_calls==2) ||
    (app_mode==APP_QUERY_LATE && query_calls==3))return EFI_DEVICE_ERROR;
  if(app_mode==APP_QUERY_NULL){*bytes=sizeof(**info);return EFI_SUCCESS;}
  *bytes=app_mode==APP_QUERY_SHORT?sizeof(**info)-1:sizeof(**info);
  CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,*bytes,(VOID **)info)));query_allocation=*info;
  if(*bytes>=sizeof(**info)) {
    **info=infos[1];if(app_mode==APP_QUERY_RGB)(*info)->PixelFormat=PixelRedGreenBlueReserved8BitPerColor;
    if(app_mode>=APP_QUERY_FORWARD && app_mode<=APP_QUERY_GEOMETRY)(*info)->PixelFormat=infos[0].PixelFormat;
    if(app_mode==APP_QUERY_GEOMETRY)(*info)->HorizontalResolution=512;
  }
  if(app_mode==APP_QUERY_MODE_CHANGE)modes[1].Mode++;
  if(app_mode==APP_QUERY_FB_CHANGE)modes[1].FrameBufferBase=0x6000000000ULL;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI app_free_pool(VOID *memory) {
  BOOLEAN query=memory==query_allocation;
  EFI_STATUS result=free_pool(memory);if(query)query_allocation=NULL;
  return query && app_mode==APP_QUERY_FREE?EFI_DEVICE_ERROR:result;
}
static EFI_TPL EFIAPI raise_tpl(EFI_TPL t){EFI_TPL old=current_tpl;CHECK(t==TPL_CALLBACK || t==TPL_NOTIFY);current_tpl=t;return old;}
static VOID EFIAPI restore_tpl(EFI_TPL t){CHECK(current_tpl==TPL_CALLBACK || current_tpl==TPL_NOTIFY);current_tpl=t;}
static EFI_STATUS EFIAPI open_info(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **e,UINTN *n) {
  CHECK(h==H(3) && (guid(g,&pg) || guid(g,&dg)) && current_tpl>=TPL_CALLBACK);
  *n=app_mode==APP_OWNER && guid(g,&pg)?1:0;*e=NULL;
  if(bound && guid(g,&pg))*n=app_mode==APP_HANDOFF_MULTI || app_mode==APP_HANDOFF_CHILD || app_mode==APP_HANDOFF_BAD_CHILD?2:1;
  if(bound && guid(g,&dg) && app_mode==APP_HANDOFF_OTHER_CHILD)*n=1;
  if((bound || (extra_case==17 && disconnect_calls)) && extra_case>=10 && guid(g,&dg))*n=extra_case==19?2:1;
  if(*n) {
    CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,*n*sizeof(**e),(VOID **)e)));
    (*e)[0].Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER;
    if(bound) {
      (*e)[0].AgentHandle=app_mode==APP_HANDOFF_CHANGED && key_sent?H(11):H(8);
      if(disconnect_calls && app_mode==APP_HANDOFF_REBIND)(*e)[0].AgentHandle=H(11);
      (*e)[0].ControllerHandle=H(3);(*e)[0].OpenCount=1;
      if(*n==2) {
        (*e)[1]=(*e)[0];
        if(app_mode==APP_HANDOFF_MULTI)(*e)[1].AgentHandle=H(11);
        else {(*e)[1].Attributes=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;(*e)[1].ControllerHandle=H(10);}
      }
      if(guid(g,&dg)){(*e)[0].Attributes=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;(*e)[0].ControllerHandle=H(10);}
    }
  }
  if(*n && guid(g,&dg) && extra_case>=10) {
    memset(*e,0,*n*sizeof(**e));
    (*e)[0].Attributes=extra_case==13?0x40:EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
    (*e)[0].AgentHandle=extra_case==12?H(99):H(8);
    (*e)[0].ControllerHandle=H(10);(*e)[0].OpenCount=extra_case==18?2:1;
    if(*n==2)(*e)[1]=(*e)[0];
  }
  if(bound && guid(g,&dg) && (extra_case==104 || extra_case==105)) {
    if(*e)free_pool(*e);
    *n=4;CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,4*sizeof(**e),(VOID**)e)));
    for(UINTN i=0;i<4;i++) {
      (*e)[i].Attributes=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
      (*e)[i].AgentHandle=H(8);(*e)[i].ControllerHandle=H(20+i);(*e)[i].OpenCount=1;
    }
  }
  if(app_mode==APP_OWNER_RACE && current_tpl<TPL_NOTIFY)owner_raced=TRUE;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
  CHECK(h==H(3) && current_tpl==TPL_NOTIFY);*n=2;
  CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,2*sizeof(**out),(VOID **)out)));
  (*out)[0]=&pg;(*out)[1]=&dg;
  if(late_pci_change && app_mode==APP_HANDOFF_LATE_MISSING){*n=1;(*out)[0]=&dg;}
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI owner_stop(EFI_DRIVER_BINDING_PROTOCOL *b,EFI_HANDLE h,UINTN n,EFI_HANDLE *c) {
  (void)b;(void)h;(void)n;(void)c;CHECK(0);return EFI_UNSUPPORTED; /* Direct Stop is forbidden. */
}
static EFI_STATUS EFIAPI stale_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *d) {
  (void)p;(void)w;(void)o;(void)n;(void)d;CHECK(0);return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI disconnect(EFI_HANDLE h,EFI_HANDLE agent,EFI_HANDLE child) {
  CHECK(windows_loaded && GuScreenDetached);
  CHECK(h==H(3) && agent==H(8) && agent!=owner_binding.ImageHandle && !child);
  CHECK(current_tpl==TPL_APPLICATION && (key_sent || app_mode==APP_TIMEOUT) && prompt && key_reset && !disconnect_calls && !exclusive);
  CHECK(GuAssets[4].firmware.data && !fake.mmio_writes && !fake.flrs && !fake.maps);
  CHECK(strstr(report_text,"release RTX firmware driver"));disconnect_calls++;
  if(app_mode==APP_HANDOFF_FAIL){config[4]|=4;return EFI_DEVICE_ERROR;}
  if(app_mode==APP_HANDOFF_NOOP || app_mode==APP_HANDOFF_REBIND)return EFI_SUCCESS;
  bound=FALSE;
  modes[0].FrameBufferBase=0;modes[0].FrameBufferSize=0;
  if(app_mode==APP_HANDOFF_CONFIG)config[4]&=(UINT8)~2U;
  if(app_mode>=APP_DECODE_BOTH && app_mode<=APP_DECODE_ENABLE_ERROR_MATCH) {
    UINT8 remove=app_mode==APP_DECODE_IO?1:app_mode==APP_DECODE_BOTH || app_mode==APP_DECODE_PARTIAL?3:2;
    config[4]&=(UINT8)~remove;
    if(app_mode==APP_DECODE_OTHER)config[0x10]^=0x10;
    if(app_mode==APP_DECODE_BME)config[4]|=4;
  }
  fake.pci_attributes=(config[4]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|
    (config[4]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0)|(config[4]&4?EFI_PCI_IO_ATTRIBUTE_BUS_MASTER:0);
  if(app_mode==APP_HANDOFF_PROM)registers[0x300000/4]^=1;
  if(app_mode==APP_HANDOFF_MISSING_PCI)pci.Pci.Read=stale_read;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_protocol(EFI_HANDLE h,EFI_GUID *g,VOID **p,EFI_HANDLE agent,EFI_HANDLE controller,UINT32 attributes) {
  CHECK(h==H(3) && guid(g,&pg) && agent==H(1) && !controller);
  CHECK(attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE && app_mode!=APP_OWNER && current_tpl>=TPL_CALLBACK);
  CHECK(!owner_raced && !bound); /* EXCLUSIVE must never disconnect an owner. */
  exclusive=TRUE;exclusive_calls++;*p=late_pci_change && app_mode==APP_HANDOFF_LATE_REPLACED?&replacement_pci:&pci;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_protocol(EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE agent,EFI_HANDLE controller) {
  CHECK(h==H(3) && guid(g,&pg) && agent==H(1) && !controller && exclusive);
  CHECK(GuSafeToReturn() && !Gu.allocations);exclusive=FALSE;close_protocol_calls++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f) {
  CHECK(p==&pci || p==&replacement_pci);*s=*d=*f=0;*b=GU_BOARD_BUS;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI replacement_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *d) {
  CHECK(p==&replacement_pci);return read_config(&pci,w,o,n,d);
}
static EFI_STATUS EFIAPI replacement_write(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *d) {
  CHECK(p==&replacement_pci);return write_config(&pci,w,o,n,d);
}
static EFI_STATUS EFIAPI app_read_config(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *d) {
  if(app_mode==APP_DECODE_READ_FAIL && attribute_enables && o==0x10)return EFI_DEVICE_ERROR;
  return read_config(p,w,o,n,d);
}
static EFI_STATUS EFIAPI attributes(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
  CHECK(current_tpl<=TPL_NOTIFY);
  if((mask&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) || fake.maps || Gu.master_attributes_pending) {
    CHECK(Gu.owned && Gu.armed && Gu.display_ok && exclusive);
    if(app_mode==APP_MASTER_DISABLE_ERROR && op==EfiPciIoAttributeOperationDisable) {
      fake.master_disables++;return EFI_DEVICE_ERROR;
    }
    EFI_STATUS result=master_attributes(p,op,mask,out);
    if(app_mode==APP_MASTER_ENABLE_ERROR_CLEAR_BAD && op==EfiPciIoAttributeOperationDisable)config[4]&=(UINT8)~2U;
    return (app_mode==APP_MASTER_ENABLE_ERROR || app_mode==APP_MASTER_ENABLE_ERROR_CLEAR_BAD) &&
      op==EfiPciIoAttributeOperationEnable?EFI_DEVICE_ERROR:result;
  }
  CHECK(p==&pci && disconnect_calls==1 && exclusive && Gu.owned && Gu.armed && Gu.display_ok);
  CHECK(!Gu.started && !fake.mmio_writes && !fake.flrs && !fake.maps);
  if(op==EfiPciIoAttributeOperationSupported) {
    CHECK(out && !mask);*out=app_mode==APP_DECODE_UNSUPPORTED?0:
      EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
    return app_mode==APP_DECODE_SUPPORT_ERROR?EFI_DEVICE_ERROR:EFI_SUCCESS;
  }
  if(op==EfiPciIoAttributeOperationGet) {
    CHECK(out && !mask);*out=fake.pci_attributes;
    return app_mode==APP_DECODE_GET_ERROR?EFI_DEVICE_ERROR:EFI_SUCCESS;
  }
  CHECK(op==EfiPciIoAttributeOperationEnable && !out && mask &&
    !(mask & ~(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY)));
  CHECK(!(config[4]&4) && strstr(report_text,"decode restore intent"));
  CHECK(!attribute_enables);attribute_enables++;enabled_attributes=mask;
  if(app_mode==APP_DECODE_NOOP)return EFI_SUCCESS;
  if((mask&EFI_PCI_IO_ATTRIBUTE_IO) && app_mode!=APP_DECODE_PARTIAL)config[4]|=1;
  if(mask&EFI_PCI_IO_ATTRIBUTE_MEMORY)config[4]|=2;
  if(app_mode==APP_DECODE_EXTRA_BME || app_mode==APP_DECODE_ENABLE_ERROR)config[4]|=4;
  fake.pci_attributes=(config[4]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|
    (config[4]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0)|(config[4]&4?EFI_PCI_IO_ATTRIBUTE_BUS_MASTER:0);
  return app_mode==APP_DECODE_ENABLE_ERROR || app_mode==APP_DECODE_ENABLE_ERROR_MATCH?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bar_info(EFI_PCI_IO_PROTOCOL *p,UINT8 bar,UINT64 *supports,VOID **resources) {
  CHECK(p==&pci && !bar);if(supports)*supports=0;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r;
  CHECK(!EFI_ERROR(allocate_pool(EfiLoaderData,sizeof(*r)+2,(VOID **)&r)));
  r->Desc=ACPI_ADDRESS_SPACE_DESCRIPTOR;r->Len=sizeof(*r)-3;r->ResType=0;r->AddrSpaceGranularity=32;
  r->AddrRangeMin=0x80000000;r->AddrRangeMax=0x80ffffff;r->AddrLen=app_mode==APP_BAD_BAR?4096:0x1000000;
  if(extra_case==106){r->AddrRangeMin=0xd5000000;r->AddrRangeMax=0xd5ffffff;}
  if(app_mode==APP_BAR_MAX)r->AddrRangeMax=0x83000000;
  /* Pinned EDK2 PciIo.c returns PciBar.Alignment, not the inclusive end. */
  if(app_mode==APP_EDK2_BAR)r->AddrRangeMax=0x00ffffff;
  if(app_mode==APP_BAD_ALIGNMENT)r->AddrRangeMax=0x01ffffff;
  ((UINT8 *)(r+1))[0]=ACPI_END_TAG_DESCRIPTOR;
  if(app_mode==APP_BAR_CHECKSUM)((UINT8 *)(r+1))[1]=1;
  *resources=r;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_close(EFI_FILE_PROTOCOL *f){
  FILE_FAKE *v=(FILE_FAKE *)f;CHECK(v->open && !Gu.retained);v->open=FALSE;
  if(Gu.started)file_closes++;
  return (app_mode==APP_CLOSE || app_mode==APP_HANDOFF_CLOSE) && v->kind==3?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_open(EFI_FILE_PROTOCOL *f,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attributes) {
  FILE_FAKE *v=(FILE_FAKE *)f;char path[128];narrow(path,sizeof(path),name);CHECK(v->open);*out=NULL;
  int target=-1;
  if(v->kind==9 && !strcmp(path,"\\EFI\\Microsoft\\Boot\\bootmgfw.efi")) {
    if(extra_case==2)return EFI_NOT_FOUND;target=10;
  }
  else if(v->kind==0 && !strcmp(path,"\\RTX4090-UNLOCK")) {
    /* ESP_NO_ASSETS/RENUM/MULTI model the hardware layout: the ESP holds only
       the loader; the work tree lives on the payload volume, so the ESP root
       must answer NOT_FOUND and force the adopt-by-GUID fallback. */
    if(app_mode==APP_ESP_NO_ASSETS||app_mode==APP_PAYLOAD_RENUM||app_mode==APP_PAYLOAD_MULTI)return EFI_NOT_FOUND;
    target=1;
  }
  else if((v->kind==9||v->kind==12) && !strcmp(path,"\\RTX4090-UNLOCK"))target=1;
  else if(v->kind==1 && !strncmp(path,"RUN",3)) {
    CHECK(!strcmp(path,"RUN00") || !strcmp(path,"RUN01") || !strcmp(path,"RUN02"));
    if(strcmp(path,"RUN02"))target=2;
    else if(mode&EFI_FILE_MODE_CREATE){CHECK(attributes==EFI_FILE_DIRECTORY && !run_created);run_created=TRUE;target=2;}
    else return EFI_NOT_FOUND;
  } else if(v->kind==2 && !strcmp(path,"REPORT.TXT")) {
    if(!(mode&EFI_FILE_MODE_CREATE))return EFI_NOT_FOUND;
    CHECK(run_created && !report_bytes);target=3;
  } else if(v->kind==2 && !strcmp(path,"PROM.BIN")) {
    if(!(mode&EFI_FILE_MODE_CREATE))return EFI_NOT_FOUND;
    CHECK(!prom_dump_calls);target=11;
  } else if(v->kind==1) {
    for(int i=0;i<5;i++) {
      const char *base=strrchr(originals[i].name,'/')+1;
      if(!strncmp(path,"ASSETS\\",7) && !strcmp(path+7,base))target=4+i;
    }
  }
  CHECK(target>=0 && !files[target].open);files[target].open=TRUE;files[target].position=0;*out=&files[target].protocol;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_info(EFI_FILE_PROTOCOL *f,EFI_GUID *g,UINTN *size,VOID *data) {
  FILE_FAKE *v=(FILE_FAKE *)f;CHECK(guid(g,&ig) && v->open && *size>=sizeof(EFI_FILE_INFO));
  memset(data,0,sizeof(EFI_FILE_INFO));EFI_FILE_INFO *info=data;info->Size=sizeof(*info);
  if(v->kind<3)info->Attribute=EFI_FILE_DIRECTORY;
  if(v->kind>=4)info->FileSize=originals[v->kind-4].firmware.size+(app_mode==APP_BAD_SIZE?1:0);
  *size=sizeof(*info);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_read(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *data) {
  FILE_FAKE *v=(FILE_FAKE *)f;CHECK(v->open && v->kind>=4);
  if(v->kind==10){CHECK(*n==2);memcpy(data,"MZ",2);return EFI_SUCCESS;}
  const struct firmware *asset=&originals[v->kind-4].firmware;UINTN remain=asset->size-v->position;
  if(*n>remain)*n=remain;memcpy(data,asset->data+v->position,*n);
  if(app_mode==APP_BAD_HASH && !v->position && *n)((UINT8 *)data)[0]^=1;
  v->position+=*n;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_write(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *data) {
  CHECK(current_tpl==TPL_APPLICATION);
  FILE_FAKE *v=(FILE_FAKE *)f;CHECK(v->open && data);
  if(v->kind==11) {
    CHECK(*n==1048576 && !Gu.started && !fake.gsp_starts && !fake.sec2_starts);
    prom_dump_calls++;
    if(extra_case==27){CHECK(Gu.fault);for(UINTN i=0;i<*n;i++)CHECK(!((UINT8 *)data)[i]);}
    else CHECK(!memcmp(data,(UINT8 *)registers+0x300000,*n));
    if(extra_case==25)return EFI_DEVICE_ERROR;
    if(extra_case==26)(*n)--;
    return EFI_SUCCESS;
  }
  CHECK(v->kind==3);
  if(app_mode==APP_SHORT_WRITE && *n)(*n)--;
  if(app_mode==APP_LOG_DIRTY && Gu.started)return EFI_DEVICE_ERROR;
  if(app_mode==APP_FWSEC_DIAG_LOG && !strncmp(data,"FWSEC snapshot: phase=pre-start",30))return EFI_DEVICE_ERROR;
  if(app_mode==APP_HANDOFF_LOG && disconnect_calls)return EFI_DEVICE_ERROR;
  if(app_mode==APP_DECODE_LOG_FAIL && !strncmp(data,"decode restore intent",21))return EFI_DEVICE_ERROR;
  if(app_mode==APP_FINAL_LOG && !strncmp(data,"FINAL ",6))return EFI_DEVICE_ERROR;
  CHECK(report_bytes+*n<sizeof(report_text));memcpy(report_text+report_bytes,data,*n);
  if((app_mode==APP_HANDOFF_LATE_MISSING || app_mode==APP_HANDOFF_LATE_REPLACED) && !strncmp(data,"driver release:",15)) {
    late_pci_change=TRUE;pci.Pci.Read=stale_read;
    if(app_mode==APP_HANDOFF_LATE_REPLACED)config[4]|=4;
  }
  report_bytes+=*n;report_text[report_bytes]=0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_flush(EFI_FILE_PROTOCOL *f) {
  CHECK(((FILE_FAKE *)f)->open);return app_mode==APP_FLUSH?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *p,EFI_FILE_PROTOCOL **root) {
  CHECK(p==&fs || p==&windows_fs || p==&esp_only_fs || p==&payload2_fs);
  /* kind 0 = USB/ESP-boot root with the tree; kind 9 = Windows ESP (bootmgfw);
     kind 12 = payload/extra FAT32 volumes (their own root so the adopted
     payload root and the preload ESP root can be open at the same time). */
  int k=p==&fs?0:(p==&esp_only_fs||p==&payload2_fs)?12:9;files[k].open=TRUE;*root=&files[k].protocol;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate_path(EFI_GUID *g,EFI_DEVICE_PATH_PROTOCOL **p,EFI_HANDLE *h) {
  CHECK(guid(g,&pg) && p && *p);*h=*p==(VOID*)intel_path?H(99):H(3);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI load_image(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *path,VOID *source,UINTN size,EFI_HANDLE *out) {
  CHECK(!policy && parent==H(1) && path && !source && !size && !disconnect_calls && !fake.mmio_writes);
  if(extra_case==3)return EFI_LOAD_ERROR;
  windows_loaded=1;*out=H(14);windows_loaded_image.DeviceHandle=H(12);windows_loaded_image.ImageCodeType=EfiLoaderCode;
  if(extra_case==4)windows_loaded_image.DeviceHandle=H(99);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI unload_image(EFI_HANDLE h) {CHECK(h==H(14));windows_loaded=0;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI connect(EFI_HANDLE h,EFI_HANDLE *drivers,EFI_DEVICE_PATH_PROTOCOL *path,BOOLEAN recursive) {
  CHECK(h==H(3) && drivers && drivers[0]==H(8) && !drivers[1] && !path && recursive);
  CHECK(windows_loaded && !exclusive && Gu.quiesced && !Gu.allocations && fake.flrs==1 && GuScreenDetached);
  CHECK(registers[SS0/4]!=0xffffffff && registers[SS1/4]!=0xffffffff);connect_calls++;
  if(extra_case==5)return EFI_DEVICE_ERROR;
  bound=TRUE;modes[0].FrameBufferBase=0x4000000000ULL;modes[0].FrameBufferSize=0x300000;
  if(extra_case==6)registers[SS1/4]=7;
  if(extra_case==7)config[4]|=4;
  if(extra_case==8)modes[0].FrameBufferBase=modes[0].FrameBufferSize=0;
  if(extra_case==9)config[0x10]^=0x10;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI start_image(EFI_HANDLE h,UINTN *bytes,CHAR16 **data) {
  CHECK(h==H(14) && bytes && data && windows_loaded && !exclusive && !GuScreenDetached && bound && connect_calls==1);
  CHECK(!Gu.allocations && !fake.live_maps && !fake.live_buffers && Gu.quiesced && fake.flrs==1);
  CHECK(!(config[4]&4));
  CHECK(strstr(report_text,"POST-GOP SS0="));
  CHECK(strstr(report_text,"stage=before_StartImage") && !files[3].open);
  windows_started=1;longjmp(escape_halt,2);
}
static EFI_STATUS EFIAPI app_read_memory(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,
  UINT8 bar,UINT64 offset,UINTN count,VOID *value) {
  if(extra_case==27 && offset>=0x300000 && offset<0x400000)return EFI_DEVICE_ERROR;
  return read_memory(p,w,bar,offset,count,value);
}
static void app_setup(enum app_case mode) {
  setup(mode==APP_BME || mode==APP_HANDOFF_BME?BME_READBACK_FAIL:mode==APP_DIRTY?FWSEC_TIMEOUT:mode==APP_UNMAP?UNMAP_FAIL:mode==APP_SELECTOR?SELECTOR_LOST:
    mode==APP_FWSEC_NO_WPR?PIO_UNAVAILABLE_NO_WPR:mode==APP_FWSEC_PIO_UNAVAILABLE?PIO_UNAVAILABLE:
    extra_case>=31 && extra_case<100?(enum scenario)(SEC2_DELAYED_START+extra_case-31):SUCCESS);
  app_mode=mode;memcpy(config,captured_config,sizeof(config));
  *(UINT32*)config=GU_BOARD_ID;*(UINT32*)(config+0x2c)=GU_BOARD_SUBSYSTEM;
  memset(&local_esp_path,0,sizeof(local_esp_path));
  local_esp_path.hd.Header.Type=MEDIA_DEVICE_PATH;local_esp_path.hd.Header.SubType=MEDIA_HARDDRIVE_DP;
  local_esp_path.hd.Header.Length[0]=sizeof(HARDDRIVE_DEVICE_PATH);
  local_esp_path.hd.PartitionNumber=1;local_esp_path.hd.MBRType=2;local_esp_path.hd.SignatureType=2;
  EFI_GUID esp={KOH_WINDOWS_ESP_GUID};
  memcpy(local_esp_path.hd.Signature,&esp,sizeof(esp));
  local_esp_path.end.Type=END_DEVICE_PATH_TYPE;local_esp_path.end.SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;local_esp_path.end.Length[0]=4;
  memcpy(&wrong_esp_path,&local_esp_path,sizeof(wrong_esp_path));wrong_esp_path.hd.Signature[0]^=0xff;
  memcpy(&payload_esp_path,&local_esp_path,sizeof(payload_esp_path));
  EFI_GUID payload={KOH_PAYLOAD_ESP_GUID};
  memcpy(payload_esp_path.hd.Signature,&payload,sizeof(payload));
  payload_esp_path.hd.PartitionNumber=4;
  memcpy(&esp_only_path,&payload_esp_path,sizeof(esp_only_path));
  if(app_mode==APP_PAYLOAD_RENUM)esp_only_path.hd.PartitionNumber=3;
  memcpy(&payload2_path,&payload_esp_path,sizeof(payload2_path));payload2_path.hd.PartitionNumber=6;
  memset(&st,0,sizeof(st));memset(&rt,0,sizeof(rt));memset(files,0,sizeof(files));memset(&loaded,0,sizeof(loaded));
  exclusive_calls=close_protocol_calls=key_calls=watchdog_calls=report_bytes=file_closes=0;
  query_calls=0;query_allocation=NULL;prom_dump_calls=0;
  pci.Mem.Read=app_read_memory;
  disconnect_calls=0;bound=TRUE;
  late_pci_change=FALSE;attribute_enables=0;enabled_attributes=0;
  memset(&owner_binding,0,sizeof(owner_binding));memset(&owner_image,0,sizeof(owner_image));
  owner_binding.DriverBindingHandle=mode==APP_HANDOFF_BINDING?H(11):H(8);
  owner_binding.ImageHandle=H(9);owner_binding.Stop=owner_stop;owner_binding.Version=0x1234;
  owner_image.ImageBase=mode==APP_HANDOFF_IMAGE?NULL:(VOID *)(UINTN)0x800000;
  owner_image.ImageSize=0x20000;owner_image.ImageCodeType=EfiBootServicesCode;owner_image.ImageDataType=EfiBootServicesData;owner_image.DeviceHandle=H(3);
  memset(report_text,0,sizeof(report_text));
  exclusive=halted=key_sent=key_reset=prompt=run_created=owner_raced=FALSE;current_tpl=TPL_APPLICATION;
  st.BootServices=&bs;st.RuntimeServices=&rt;st.ConOut=&conout;st.ConIn=&conin;
  st.ConsoleOutHandle=mode==APP_QUERY_OTHER_CONSOLE?H(7):H(6);
  rt.GetVariable=get_variable;conout.OutputString=app_output;conin.Reset=input_reset;conin.ReadKeyStroke=read_key;
  bs.Stall=app_stall;bs.SetWatchdogTimer=watchdog;bs.HandleProtocol=handle;bs.LocateHandleBuffer=locate;
  bs.FreePool=app_free_pool;
  bs.OpenProtocolInformation=open_info;bs.OpenProtocol=open_protocol;bs.CloseProtocol=close_protocol;
  bs.ProtocolsPerHandle=protocols;bs.DisconnectController=disconnect;
  bs.LocateDevicePath=locate_path;bs.LoadImage=load_image;bs.UnloadImage=unload_image;
  bs.StartImage=start_image;bs.ConnectController=connect;
  windows_loaded=windows_started=connect_calls=0;
  bs.RaiseTPL=raise_tpl;bs.RestoreTPL=restore_tpl;pci.GetLocation=location;pci.GetBarAttributes=bar_info;
  pci.Pci.Read=app_read_config;pci.Attributes=mode==APP_DECODE_NO_API?NULL:attributes;
  replacement_pci=pci;replacement_pci.Pci.Read=replacement_read;replacement_pci.Pci.Write=replacement_write;
  loaded.DeviceHandle=H(2);fs.OpenVolume=volume;windows_fs.OpenVolume=volume;esp_only_fs.OpenVolume=volume;payload2_fs.OpenVolume=volume;
  pci.RomImage=prom;pci.RomSize=0x24a00;
  for(int i=0;i<13;i++) {
    files[i].kind=i;files[i].protocol.Open=file_open;files[i].protocol.Close=file_close;
    files[i].protocol.GetInfo=file_info;files[i].protocol.Read=file_read;
    files[i].protocol.Write=file_write;files[i].protocol.Flush=file_flush;
  }
  for(int i=0;i<2;i++) {
    memset(&modes[i],0,sizeof(modes[i]));memset(&infos[i],0,sizeof(infos[i]));
    memset(&gops[i],0,sizeof(gops[i]));gops[i].Mode=&modes[i];gops[i].QueryMode=query_mode;
    modes[i].Mode=3;modes[i].MaxMode=4;modes[i].Info=&infos[i];modes[i].SizeOfInfo=sizeof(infos[i]);
    infos[i].HorizontalResolution=infos[i].PixelsPerScanLine=1024;infos[i].VerticalResolution=768;
    infos[i].PixelFormat=i?PixelBltOnly:PixelBlueGreenRedReserved8BitPerColor;
  }
  modes[0].FrameBufferBase=0x4000000000ULL;modes[0].FrameBufferSize=0x300000;
  if(mode>=APP_ZERO_INFO) {
    modes[1].SizeOfInfo=0;
    /* Unsized Info must never be dereferenced; use the QueryMode-owned copy. */
    modes[1].Info=(VOID *)(UINTN)1;
  }
  if(mode==APP_ZERO_PHYSICAL) {
    modes[0].SizeOfInfo=0;modes[0].Info=(VOID *)(UINTN)1;
    modes[1].SizeOfInfo=sizeof(infos[1]);modes[1].Info=&infos[1];
  }
  if(mode==APP_SHORT_INFO)modes[1].SizeOfInfo=sizeof(infos[1])-1;
  if(mode==APP_QUERY_MISSING)gops[1].QueryMode=NULL;
  if(mode==APP_CACHED_RGB) {
    modes[1].SizeOfInfo=sizeof(infos[1]);modes[1].Info=&infos[1];
    infos[1].PixelFormat=PixelBlueGreenRedReserved8BitPerColor;
  }
  if(mode==APP_BAD_GOP)modes[1].FrameBufferBase=0x6000000000ULL;
  if(mode==APP_BAD_PROM)registers[0x300000/4]^=1;
  if(mode==APP_CONFIG)config[0xc8]^=1;
}

int main(int argc,char **argv) {
  CHECK(argc==4);size_t n;UINT8 *data=read_file(argv[1],&n);CHECK(n==sizeof(prom));memcpy(prom,data,sizeof(prom));free(data);
  const char *names[]={"booter_load_ad102.bin","v67-ad102-610.43.03-four-plm.bin","gsp_ga10x.bin","gsp_boot_ad102.bin","gsp_boot_desc_ad102.bin"};
  for(int i=0;i<5;i++) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",argv[2],names[i]);
    static char full_names[5][128];snprintf(full_names[i],128,"gpu-unlock/610.43.03/%s",names[i]);
    originals[i].name=full_names[i];originals[i].firmware.data=read_file(path,&originals[i].firmware.size);
  }
  data=read_file(argv[3],&n);CHECK(n==256);memcpy(captured_config,data,n);free(data);
  const int selected[]={APP_HANDOFF,APP_ESCAPE,APP_TIMEOUT,APP_SECUREBOOT,APP_DUPLICATE,APP_BAD_BAR,APP_BAD_GOP,
    APP_BAD_HASH,APP_BAD_PROM,APP_HANDOFF_MULTI,APP_HANDOFF_CHANGED,APP_HANDOFF_FAIL,APP_HANDOFF_NOOP,
    APP_HANDOFF_CONFIG,APP_HANDOFF_PROM,APP_HANDOFF_MISSING_PCI,APP_HANDOFF_REPLACED_PCI,APP_DIRTY,
    APP_SELECTOR,APP_UNMAP,APP_LOG_DIRTY,APP_FWSEC_NO_WPR,APP_MASTER_ENABLE_ERROR,APP_MASTER_DISABLE_ERROR,
    APP_HANDOFF_LOG,APP_CLOSE,APP_HANDOFF_CHILD,APP_HANDOFF_BAD_CHILD,APP_HANDOFF_OTHER_CHILD,
    APP_ESP_BOOT,APP_ESP_WRONG_GUID,APP_PAYLOAD_BOOT,APP_ESP_NO_ASSETS,APP_PAYLOAD_RENUM,APP_PAYLOAD_MULTI};
  const int injected[]={SUCCESS,SEC2_STUCK_HALT,SEC2_RUN_HALT,SEC2_RUN_TIMEOUT,
   SEC2_UNAVAILABLE,SEC2_DIAG_FAIL,SEC2_PROTECTED_DIAG,SEC2_STATUS_ERROR_PLM_OPEN,
   PLM_SECOND_TARGET_TIMEOUT,PLM_PRIOR_RELOCK,PLM_WRONG_WPR_CFG_MASK,
   RESET_CHANGES_WPR_FIRST,
   RESET_CORRUPTS_PAYLOAD_FIRST,
   AD_MARKER_MISSING,AD_MARKER_WRONG,AD_MARKER_NO_OPEN,AD_THIRD_FAIL,AD_FEAT_FAIL,
   AD_SS0_REJECT,AD_SS1_REJECT,AD_FBPA_REJECT};
  unsigned basic_count=sizeof(selected)/sizeof(*selected), extra_count=sizeof(injected)/sizeof(*injected);
  for(unsigned i=0;i<basic_count+extra_count;i++) {
    BOOLEAN injected_case=i>=basic_count;
    extra_case=injected_case?10:0;
    enum app_case mode=injected_case?APP_HANDOFF:(enum app_case)selected[i];
    app_setup(mode);
    if(injected_case)fake.mode=(enum scenario)injected[i-basic_count];
    volatile EFI_STATUS result=EFI_ABORTED;
    int jumped=setjmp(escape_halt);if(!jumped)result=EfiMain(H(1),&st);
    BOOLEAN success=(mode==APP_HANDOFF || mode==APP_HANDOFF_CONFIG || mode==APP_HANDOFF_CHILD ||
      mode==APP_ESP_BOOT || mode==APP_PAYLOAD_BOOT || mode==APP_ESP_NO_ASSETS || mode==APP_PAYLOAD_RENUM ||
      mode==APP_TIMEOUT) &&
      (!injected_case || ad_success(injected[i-basic_count]));
    if(success) {
      CHECK(windows_started && jumped==2 && !halted && fake.flrs==1 && connect_calls==1);
      /* Arm polls ConIn once per 100 ms GuStall; the auto-attempt window is
       * KOH_AUTOCONFIRM_MS, so the poll count pins the wall-clock window
       * (the simulated suite has no real time: a wrong divisor here would
       * pass every other assertion and hang the board for minutes). */
      if(mode==APP_TIMEOUT)CHECK(key_calls==KOH_AUTOCONFIRM_MS/100);
      CHECK(fake.sec2_starts==1);
      CHECK(registers[SS0/4]==(fake.mode==AD_SS0_REJECT?0x31322012:0x88888888));
      CHECK(registers[SS1/4]==((fake.mode==AD_SS1_REJECT || fake.mode==SELECTOR_LOST)?7:8));
      CHECK(strstr(report_text,"required_expected=1") && strstr(report_text,"marker_ok=1"));
    } else {
      CHECK(!windows_started && jumped!=2);
      if(disconnect_calls)CHECK(halted && jumped==1 && Gu.retained);
      else CHECK(EFI_ERROR(result) && !fake.mmio_writes && !fake.flrs && !fake.maps && !windows_loaded);
      if(injected_case && fake.mode!=AD_SS0_REJECT && fake.mode!=AD_SS1_REJECT)
        CHECK(!fake.flrs && registers[SS0/4]==0x31322012 && registers[SS1/4]==7);
    }
    CHECK(fake.gsp_starts<=1 && fake.sec2_starts<=1 && fake.flrs<=1 && !(config[4]&4));
    printf("AD102 APP %s injected=%s disconnect=%llu Windows=%d halt=%u PASS\n",app_names[mode],injected_case?case_names[fake.mode]:"none",(unsigned long long)disconnect_calls,windows_started,halted);
    if(!success && !halted)CHECK(!Gu.allocations && !fake.pool_live);
    if(!success && halted && !fake.flrs)CHECK(!connect_calls);
    dispose_host_fixture();
  }

  for(volatile int sb=100;sb<=106;sb++) {
    extra_case=sb;app_setup(APP_HANDOFF);
    if(sb==106) {
      /* RUN02 workstation PCI snapshot: command, BARs and PCIe controls. */
      *(UINT32*)(config+4)=0x00100143;
      *(UINT32*)(config+0x10)=0xd5000000;*(UINT32*)(config+0x14)=0x0000000c;
      *(UINT32*)(config+0x18)=0x00001e70;*(UINT32*)(config+0x1c)=0x0000000c;
      *(UINT32*)(config+0x20)=0x00001e78;*(UINT32*)(config+0x24)=0x0000c001;
      *(UINT32*)(config+0x30)=0xd6000000;*(UINT32*)(config+0x7c)=0x10008de1;
      *(UINT32*)(config+0x80)=0x00005937;
    }
    volatile EFI_STATUS result=EFI_ABORTED;
    int jumped=setjmp(escape_halt);if(!jumped)result=EfiMain(H(1),&st);
    if(sb==100 || sb==102 || sb==103 || sb==104 || sb==106)CHECK(windows_started && jumped==2 && !halted);
    else CHECK(!windows_started && !disconnect_calls && !fake.mmio_writes && EFI_ERROR(result));
    if(sb==105)CHECK(strstr(report_text,"handle=0x17") && strstr(report_text,"child ancestry:") && strstr(report_text,"stage=prom") && strstr(report_text,"stage=assets") && strstr(report_text,"stage=windows-preload"));
    printf("Policy/topology case %d PASS\n",sb);dispose_host_fixture();
  }
  for(int i=0;i<5;i++)free((VOID*)originals[i].firmware.data);
  printf("%u AD102 app + core scenarios passed; firmware/PCI/MMIO/Windows simulated.\n",basic_count+extra_count);return 0;
}
