#!/bin/bash
check() { return 0; }
depends() { echo ""; }
install() {
    inst_simple /opt/tu102/tu102_execute.ko /opt/tu102/tu102_execute.ko
    inst_simple /opt/tu102/tu102_diag.ko /opt/tu102/tu102_diag.ko
    for f in tu102-1e04-fwsec bl_gsp_tu102 booter_ucode_prod v67_payload gsp_rm_boot_dbg; do
        inst_simple /usr/lib/firmware/tu102-lab/$f.bin /lib/firmware/tu102-lab/$f.bin
    done
    inst_hook pre-trigger 10 "$moddir/tu102-gate.sh"
}
