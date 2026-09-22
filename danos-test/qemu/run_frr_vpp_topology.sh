#!/bin/bash
# Launch the three-VM QEMU FRR/DANOS topology.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-5}"
ISO="${DANOS_ISO:-$ROOT/build/danos-vpp-dpdk-e1000-2port-traffic.iso}"
test -f "$ISO" || { echo "[BLOCKED] DANOS ISO missing: $ISO"; exit 2; }
for f in frr-1.qcow2 frr-2.qcow2 r1-seed.iso r2-seed.iso; do
    test -f "$T/$f" || { echo "[BLOCKED] topology artifact missing: $T/$f"; exit 2; }
done

qemu-system-x86_64 -enable-kvm -cpu host -m 2048 -smp 2 -cdrom "$ISO" \
  -netdev socket,id=lan1,listen=127.0.0.1:21001 \
  -device e1000,netdev=lan1,mac=52:54:00:10:01:01 \
  -netdev socket,id=lan2,listen=127.0.0.1:21002 \
  -device e1000,netdev=lan2,mac=52:54:00:10:02:01 \
  -display none -serial file:"$T/danos.serial.log" -monitor none \
  -daemonize -pidfile "$T/danos.pid"

qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 1 \
  -drive file="$T/frr-1.qcow2",if=virtio,format=qcow2,snapshot=on \
  -cdrom "$T/r1-seed.iso" -nic user,model=e1000 \
  -netdev socket,id=dp,connect=127.0.0.1:21001 \
  -device e1000,netdev=dp,mac=52:54:00:11:01:02 \
  -netdev socket,id=peer,listen=127.0.0.1:22001 \
  -device e1000,netdev=peer,mac=52:54:00:11:01:03 \
  -display none -serial file:"$T/frr-1.serial.log" -monitor none \
  -daemonize -pidfile "$T/frr-1.pid"

qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 1 \
  -drive file="$T/frr-2.qcow2",if=virtio,format=qcow2,snapshot=on \
  -cdrom "$T/r2-seed.iso" -nic user,model=e1000 \
  -netdev socket,id=dp,connect=127.0.0.1:21002 \
  -device e1000,netdev=dp,mac=52:54:00:12:01:02 \
  -netdev socket,id=peer,connect=127.0.0.1:22001 \
  -device e1000,netdev=peer,mac=52:54:00:12:01:03 \
  -display none -serial file:"$T/frr-2.serial.log" -monitor none \
  -daemonize -pidfile "$T/frr-2.pid"

echo "[PASS] QEMU topology launched; logs and pidfiles are under $T"
