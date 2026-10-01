#!/bin/bash
# Launch the three-VM QEMU FRR/DANOS topology.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-5}"
ISO="${DANOS_ISO:-$ROOT/build/danos-vpp-dpdk-e1000-2port-traffic.iso}"
LAN1_PORT="${DANOS_LAN1_PORT:-21001}"
LAN2_PORT="${DANOS_LAN2_PORT:-21002}"
PEER_PORT="${FRR_PEER_PORT:-22001}"
ZAPI_PORT="${FRR_ZAPI_PORT:-23001}"
MGMT_PORT="${FRR_MGMT_PORT:-24001}"
FRR_TRAFFIC_READY_PORT="${FRR_TRAFFIC_READY_PORT:-2603}"
FRR_SNAPSHOT="${FRR_QEMU_SNAPSHOT:-off}"
QEMU_CAPTURE="${QEMU_CAPTURE:-0}"
QEMU_MONITOR="${QEMU_MONITOR:-0}"
QEMU_HMP="${QEMU_HMP:-0}"
FRR_DRIVE_EXTRA=""
QEMU_DATAPLANE_MODEL="${QEMU_DATAPLANE_MODEL:-e1000}"
QEMU_MACHINE="${QEMU_MACHINE:-pc}"
if test "$FRR_SNAPSHOT" = on; then FRR_DRIVE_EXTRA=',snapshot=on'; fi
test -f "$ISO" || { echo "[BLOCKED] DANOS ISO missing: $ISO"; exit 2; }
for f in frr-1.qcow2 frr-2.qcow2 r1-seed.iso r2-seed.iso; do
    test -f "$T/$f" || { echo "[BLOCKED] topology artifact missing: $T/$f"; exit 2; }
done

# Persist the exact inputs used by this run so a passing serial log is
# reproducible and cannot be confused with a different ISO/topology.
MANIFEST="$T/run-manifest.env"
{
    printf 'git_commit=%q\n' "$(git -C "$ROOT" rev-parse HEAD)"
    printf 'danos_iso=%q\n' "$(realpath "$ISO")"
    printf 'danos_iso_sha256=%q\n' "$(sha256sum "$ISO" | awk '{print $1}')"
    printf 'topology_dir=%q\n' "$(realpath "$T")"
    printf 'lan1_port=%q\nlan2_port=%q\npeer_port=%q\nzapi_port=%q\nmgmt_port=%q\ntraffic_ready_port=%q\n' \
        "$LAN1_PORT" "$LAN2_PORT" "$PEER_PORT" "$ZAPI_PORT" "$MGMT_PORT" \
        "$FRR_TRAFFIC_READY_PORT"
    printf 'qemu_machine=%q\nqemu_dataplane_model=%q\n' "$QEMU_MACHINE" "$QEMU_DATAPLANE_MODEL"
    printf 'qemu_capture=%q\nqemu_monitor=%q\nqemu_hmp=%q\n' "$QEMU_CAPTURE" "$QEMU_MONITOR" "$QEMU_HMP"
    printf 'created_utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$MANIFEST"

DANOS_CAPTURE_ARGS=()
FRR1_CAPTURE_ARGS=()
FRR2_CAPTURE_ARGS=()
MONITOR_ARGS=()
if test "$QEMU_MONITOR" = 1 && test "$QEMU_HMP" = 1; then
    MONITOR_ARGS=(-qmp "unix:$T/danos.qmp,server=on,wait=off"
                  -monitor "unix:$T/danos.monitor,server=on,wait=off")
elif test "$QEMU_MONITOR" = 1; then
    MONITOR_ARGS=(-monitor "unix:$T/danos.monitor,server=on,wait=off")
else
    MONITOR_ARGS=(-monitor none)
fi
if test "$QEMU_CAPTURE" = 1; then
    DANOS_CAPTURE_ARGS=(-object "filter-dump,id=cap-lan1,netdev=lan1,file=$T/danos-lan1.pcap"
                        -object "filter-dump,id=cap-lan2,netdev=lan2,file=$T/danos-lan2.pcap")
    FRR1_CAPTURE_ARGS=(-object "filter-dump,id=cap-frr1-dp,netdev=dp,file=$T/frr-1-dp.pcap"
                       -object "filter-dump,id=cap-frr1-peer,netdev=peer,file=$T/frr-1-peer.pcap"
                       -object "filter-dump,id=cap-frr1-internet,netdev=internet,file=$T/frr-1-internet.pcap")
    FRR2_CAPTURE_ARGS=(-object "filter-dump,id=cap-frr2-dp,netdev=dp,file=$T/frr-2-dp.pcap"
                       -object "filter-dump,id=cap-frr2-peer,netdev=peer,file=$T/frr-2-peer.pcap"
                       -object "filter-dump,id=cap-frr2-internet,netdev=internet2,file=$T/frr-2-internet.pcap")
fi

qemu-system-x86_64 -enable-kvm -machine "$QEMU_MACHINE" -cpu host -m 2048 -smp 2 -cdrom "$ISO" \
  -vga none -device VGA,addr=0x4 \
  -netdev socket,id=lan1,listen=127.0.0.1:$LAN1_PORT \
  -device "$QEMU_DATAPLANE_MODEL",netdev=lan1,addr=0x2,mac=52:54:00:10:01:01 \
  -netdev socket,id=lan2,listen=127.0.0.1:$LAN2_PORT \
  -device "$QEMU_DATAPLANE_MODEL",netdev=lan2,addr=0x3,mac=52:54:00:10:02:01 \
  -netdev socket,id=mgmt,listen=127.0.0.1:$MGMT_PORT \
  -device virtio-net-pci,netdev=mgmt,addr=0x5,mac=52:54:00:10:03:01 \
  "${DANOS_CAPTURE_ARGS[@]}" \
  -display none -serial file:"$T/danos.serial.log" "${MONITOR_ARGS[@]}" \
  -daemonize -pidfile "$T/danos.pid"

qemu-system-x86_64 -enable-kvm -machine "$QEMU_MACHINE" -cpu host -m 1024 -smp 1 \
  -boot order=c \
  -drive file="$T/frr-1.qcow2",if=virtio,format=qcow2${FRR_DRIVE_EXTRA} \
  -cdrom "$T/r1-seed.iso" \
  -netdev user,id=internet,hostfwd=tcp:127.0.0.1:$ZAPI_PORT-:2600 \
  -device e1000,netdev=internet,addr=0x5,mac=52:54:00:11:01:00 \
  -netdev socket,id=mgmt,connect=127.0.0.1:$MGMT_PORT \
  -device e1000,netdev=mgmt,addr=0x6,mac=52:54:00:11:01:01 \
  -netdev socket,id=dp,connect=127.0.0.1:$LAN1_PORT \
  -device e1000,netdev=dp,addr=0x3,mac=52:54:00:11:01:02 \
  -netdev socket,id=peer,listen=127.0.0.1:$PEER_PORT \
  -device e1000,netdev=peer,addr=0x4,mac=52:54:00:11:01:03 \
  "${FRR1_CAPTURE_ARGS[@]}" \
  -display none -serial file:"$T/frr-1.serial.log" -monitor none \
  -daemonize -pidfile "$T/frr-1.pid"

qemu-system-x86_64 -enable-kvm -machine "$QEMU_MACHINE" -cpu host -m 1024 -smp 1 \
  -boot order=c \
  -drive file="$T/frr-2.qcow2",if=virtio,format=qcow2${FRR_DRIVE_EXTRA} \
  -cdrom "$T/r2-seed.iso" \
  -netdev user,id=internet2 \
  -device e1000,netdev=internet2,addr=0x5,mac=52:54:00:12:01:00 \
  -netdev socket,id=dp,connect=127.0.0.1:$LAN2_PORT \
  -device e1000,netdev=dp,addr=0x3,mac=52:54:00:12:01:02 \
  -netdev socket,id=peer,connect=127.0.0.1:$PEER_PORT \
  -device e1000,netdev=peer,addr=0x4,mac=52:54:00:12:01:03 \
  "${FRR2_CAPTURE_ARGS[@]}" \
  -display none -serial file:"$T/frr-2.serial.log" -monitor none \
  -daemonize -pidfile "$T/frr-2.pid"

echo "[PASS] QEMU topology launched; logs, manifest and pidfiles are under $T"
