"""Run simulated EFI transactions with supplied private fixtures; CPU only."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--prom',required=True,type=Path);p.add_argument('--assets',required=True,type=Path);p.add_argument('--pci-config',type=Path);p.add_argument('--cc',default='gcc');a=p.parse_args()
root=Path(__file__).resolve().parent/'source';prom=a.prom.resolve();assets=a.assets.resolve()
if prom.stat().st_size!=1048576:p.error('PROM fixture must be 1MiB')
if a.pci_config and a.pci_config.stat().st_size!=256:p.error('PCI fixture must be 256 bytes')
out=root/'test-build';out.mkdir(exist_ok=True)
flags=['-std=c11','-fshort-wchar','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-misleading-indentation','-Wno-format','-D__KERNEL__','-DGU_UEFI_PORT','-DKOH_ENABLE_EXPERIMENT=1','-DGU_TARGET_PCI_ID=0x268410deU','-Iunlock','-Iunlock/compat','-Ideps/edk2/MdePkg/Include','-Ideps/edk2/MdePkg/Include/X64']
import sys
if sys.platform.startswith('win'):
  # Windows/UCRT host: _CRT_SECURE_NO_WARNINGS for fopen; the test shim uses
  # native _aligned_malloc/_aligned_free pairs on Windows (test_efi_transaction.c).
  flags+=['-D_CRT_SECURE_NO_WARNINGS','-include','stdlib.h','-Wno-macro-redefined',
          '-Wno-unused-but-set-global']
core=['port','compat','vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval']
for name,test,sources in [('core','test_ad102_transaction',core),('app','test_ad103_app',['app','app_io','ownership','format','sha256','checks']+core)]:
 if name=='app' and not a.pci_config:print('SKIP app: supply --pci-config');continue
 exe=out/name
 subprocess.run([a.cc,*flags,'unlock/tests/'+test+'.c',*['unlock/'+s+'.c' for s in sources],'-o',str(exe)],cwd=root,check=True)
 cmd=[str(exe),str(prom),str(assets)]
 if name=='app':cmd.append(str(a.pci_config.resolve()))
 subprocess.run(cmd,cwd=root,check=True)
