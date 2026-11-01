#!/bin/sh
. /lib/dracut-lib.sh 2>/dev/null || true
mode=$(getarg tu102_go= 2>/dev/null)
[ -n "$mode" ] || exit 0
act="${mode%%:*}"; sel="${mode##*:}"
do_exec() {
    tgt="0000:0$1:00.0"
    insmod /opt/tu102/tu102_execute.ko target="$tgt" execute=1 >/dev/kmsg 2>&1 || echo "tu102 gate: $tgt exec insmod rc=$?" >/dev/kmsg
    s=$(cat /sys/module/tu102_execute/parameters/handoff_safe 2>/dev/null)
    r=$(cat /sys/module/tu102_execute/parameters/result 2>/dev/null)
    echo "tu102 gate: $tgt result=$r handoff_safe=$s" >/dev/kmsg
    if [ "$s" = Y ]; then rmmod tu102_execute 2>/dev/kmsg; else return 1; fi
}
do_diag() {
    insmod /opt/tu102/tu102_diag.ko target="0000:0$1:00.0" >/dev/kmsg 2>&1 || echo "tu102 gate: diag$1 rc=$?" >/dev/kmsg
    rmmod tu102_diag 2>/dev/kmsg
}
if [ "$act" = diag ]; then
    if [ "$sel" = all ]; then do_diag 1; do_diag 2; else do_diag $sel; fi
elif [ "$act" = exec ]; then
    if [ "$sel" = all ]; then for b in 1 2; do do_exec $b || break; done; else do_exec $sel; fi
fi
exit 0
