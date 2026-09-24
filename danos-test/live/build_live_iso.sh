#!/bin/bash
# DANOS-Open live ISO builder (v0.15)
#
# Produces a bootable ISO:
#   Debian kernel (trixie) + initramfs (busybox + mgrd + gnmic) +
#   isolinux bootloader. On boot: mgrd starts (real netlink, VM root),
#   and a built-in demo provisions a route via gNMI and prints the
#   kernel FIB on the serial console.
#
# Requirements: docker (debian:trixie-slim), host xorriso, gnmic binary
# Usage: bash danos-test/live/build_live_iso.sh [output.iso]

set -eu
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_ISO="${1:-$PROJECT_ROOT/build/danos-open-live.iso}"
case "$OUT_ISO" in
  /*) ;;
  *) OUT_ISO="$PROJECT_ROOT/$OUT_ISO" ;;
esac
GNMIC_SRC="${GNMIC:-/tmp/gnmic-bin}"
WORK=/tmp/danos-iso-work
APT_MIRROR="${APT_MIRROR:-http://repo.huaweicloud.com/debian}"
VPP_IMAGE="${VPP_IMAGE:-}"
VPP_DPDK_ENABLE="${VPP_DPDK_ENABLE:-1}"
VPP_DPDK_DEVICE="${VPP_DPDK_DEVICE:-vmxnet3}"
VPP_DPDK_PORTS="${VPP_DPDK_PORTS:-0000:00:02.0}"
VPP_DPDK_TRAFFIC_TEST="${VPP_DPDK_TRAFFIC_TEST:-0}"
DANOS_FIB_BRIDGE_ENABLE="${DANOS_FIB_BRIDGE_ENABLE:-0}"
DANOS_ZEBRA_ENDPOINT="${DANOS_ZEBRA_ENDPOINT:-}"

mkdir -p "$WORK" "$PROJECT_ROOT/build"

# --- 1. trixie build container: kernel + busybox + isolinux + mgrd ------
docker rm -f danos-iso-build >/dev/null 2>&1 || true
trap 'docker rm -f danos-iso-build >/dev/null 2>&1 || true' EXIT
docker run -d --name danos-iso-build -v "$PROJECT_ROOT:/src" \
    -w /src debian:trixie-slim sh -c "
cat > /etc/apt/sources.list.d/danos-mirror.sources <<EOF
Types: deb
URIs: $APT_MIRROR
Suites: trixie trixie-updates
Components: main contrib non-free non-free-firmware
EOF
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq build-essential cmake busybox-static \
    linux-image-amd64 isolinux syslinux-common >/dev/null 2>&1
cmake -B /tmp/b -S /src -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
cmake --build /tmp/b -j\$(nproc) --target danos-mgrd >/dev/null 2>&1
echo ISO-DEPS-OK
sleep 600"
# wait for deps to be ready (log marker), max ~6 min
for i in $(seq 1 180); do
    docker logs danos-iso-build 2>&1 | grep -q ISO-DEPS-OK && break
    sleep 2
done
docker logs danos-iso-build 2>&1 | grep -q ISO-DEPS-OK \
    || { echo "ERROR: ISO deps build failed"; exit 1; }

# --- 2. extract pieces ---------------------------------------------------
mkdir -p "$WORK"
VKERNEL=$(docker exec danos-iso-build sh -c 'ls /boot/vmlinuz-* | head -1')
docker cp "danos-iso-build:$VKERNEL" "$WORK/vmlinuz"
docker cp danos-iso-build:/bin/busybox "$WORK/busybox"
docker cp danos-iso-build:/usr/lib/ISOLINUX/isolinux.bin "$WORK/isolinux.bin"
docker cp danos-iso-build:/usr/lib/syslinux/modules/bios/ldlinux.c32 "$WORK/ldlinux.c32"
docker cp danos-iso-build:/usr/lib/x86_64-linux-gnu/libc.so.6 "$WORK/libc.so.6"
docker cp danos-iso-build:/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$WORK/ld-linux.so.2"
MODULE_ROOTS='/lib/modules /usr/lib/modules'
E1000=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "e1000.ko*" 2>/dev/null | head -1')
docker cp "danos-iso-build:$E1000" "$WORK/e1000.ko.raw"
VIRTIO_NET=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "virtio_net.ko*" 2>/dev/null | head -1')
docker cp "danos-iso-build:$VIRTIO_NET" "$WORK/virtio_net.ko.raw"
NET_FAILOVER=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "net_failover.ko*" 2>/dev/null | head -1')
FAILOVER=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "failover.ko*" 2>/dev/null | head -1')
VIRTIO_PCI=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "virtio_pci.ko*" 2>/dev/null | head -1')
UIO=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "uio.ko*" 2>/dev/null | head -1')
UIO_PCI_GENERIC=$(docker exec danos-iso-build sh -c \
    'find /lib/modules /usr/lib/modules -name "uio_pci_generic.ko*" 2>/dev/null | head -1')
for module in failover net_failover virtio_pci uio uio_pci_generic; do
  eval "source=\$${module^^}"
  if test -n "$source"; then
    docker cp "danos-iso-build:$source" "$WORK/${module}.ko.raw"
  fi
done
if test -x "$GNMIC_SRC"; then
    cp "$GNMIC_SRC" "$WORK/gnmic"
else
    echo "INFO: gnmic not provided; ISO will skip the optional gNMI demo"
fi

# --- 3. initramfs tree ---------------------------------------------------
rm -rf "$WORK/initramfs"
mkdir -p "$WORK/initramfs"/{bin,dev,proc,sys,tmp,lib,lib64,modules}
if test -n "$VPP_IMAGE"; then
  # Keep the image container alive so readlink can resolve SONAME targets.
  VPP_CID=$(docker create "$VPP_IMAGE" sleep infinity)
  docker start "$VPP_CID" >/dev/null
  trap 'docker rm -f danos-iso-build "$VPP_CID" >/dev/null 2>&1 || true' EXIT
  mkdir -p "$WORK/initramfs"/{usr/bin,usr/lib/x86_64-linux-gnu/vpp_plugins,lib/x86_64-linux-gnu,etc/vpp,run/vpp,var/log/vpp}
  docker cp "$VPP_CID:/usr/bin/vpp" "$WORK/initramfs/usr/bin/vpp"
  docker cp "$VPP_CID:/usr/bin/vppctl" "$WORK/initramfs/usr/bin/vppctl"
  for etc_file in passwd group nsswitch.conf hosts; do
    docker cp "$VPP_CID:/etc/$etc_file" "$WORK/initramfs/etc/$etc_file" 2>/dev/null || true
  done
  # Import the complete trixie runtime directory so transitive glibc,
  # crypto, compression and loader objects cannot be mixed with the ISO
  # builder image.  The guest profile is explicitly an integration image.
  docker cp "$VPP_CID:/lib/x86_64-linux-gnu/." \
    "$WORK/initramfs/lib/x86_64-linux-gnu/"
  docker cp "$VPP_CID:/usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so" \
    "$WORK/initramfs/usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so"
  # Keep glibc and math/loader objects from the same Debian trixie VPP
  # runtime.  docker cp of a SONAME symlink can otherwise copy only the
  # link, leaving a mixed host/build-container ABI in the initramfs.
  copy_vpp_real() {
    local name="$1" path
    path=$(docker exec "$VPP_CID" readlink -f "/lib/x86_64-linux-gnu/$name") || return 0
    test -n "$path" || return 0
    docker cp "$VPP_CID:$path" "$WORK/initramfs/lib/x86_64-linux-gnu/$name"
  }
  copy_vpp_real libc.so.6
  copy_vpp_real libm.so.6
  copy_vpp_real ld-linux-x86-64.so.2
  for lib in libvnet.so.26.10 libvlibmemory.so.26.10 libvlibapi.so.26.10 \
      libsvm.so.26.10 libvlib.so.26.10 libvppinfra.so.26.10 \
      libnuma.so.1 libcrypto.so.3 libz.so.1 libzstd.so.1 libm.so.6; do
    copy_vpp_real "$lib"
  done
  if test "$VPP_DPDK_ENABLE" = 1; then
    VPP_PLUGIN_LINE='  plugin dpdk_plugin.so { enable }'
    if test "$VPP_DPDK_PORTS" = "0000:00:02.0 0000:00:03.0"; then
      VPP_DPDK_BLOCK='dpdk {
  dev 0000:00:02.0
  dev 0000:00:03.0
}'
    else
      VPP_DPDK_BLOCK='dpdk {
  dev 0000:00:02.0
}'
    fi
  else
    VPP_PLUGIN_LINE='  plugin dpdk_plugin.so { disable }'
    VPP_DPDK_BLOCK=''
  fi
  cat > "$WORK/initramfs/etc/vpp/startup.conf" <<EOF
unix {
  nodaemon
  log /var/log/vpp/vpp.log
  cli-listen /run/vpp/cli.sock
}
plugins {
${VPP_PLUGIN_LINE}
}
api-segment {
  prefix vpp
}
socksvr {
  default
}
statseg {
  socket-name /run/vpp/stats.sock
}
${VPP_DPDK_BLOCK}
EOF
  touch "$WORK/initramfs/vpp-dpdk.enabled"
  test "$VPP_DPDK_DEVICE" = e1000 && touch "$WORK/initramfs/vpp-dpdk-e1000.enabled"
  test "$VPP_DPDK_PORTS" = "0000:00:02.0 0000:00:03.0" && touch "$WORK/initramfs/vpp-dpdk-e1000-2port.enabled"
  test "$VPP_DPDK_TRAFFIC_TEST" = 1 && touch "$WORK/initramfs/vpp-dpdk-traffic-test.enabled"
  chmod +x "$WORK/initramfs/usr/bin/vpp" "$WORK/initramfs/usr/bin/vppctl"
  docker rm -f "$VPP_CID" >/dev/null
fi
cp "$WORK/busybox" "$WORK/initramfs/bin/busybox"
cp "$WORK/libc.so.6" "$WORK/initramfs/lib/libc.so.6"
cp "$WORK/ld-linux.so.2" "$WORK/initramfs/lib/ld-linux-x86-64.so.2"
cp "$WORK/ld-linux.so.2" "$WORK/initramfs/lib64/ld-linux-x86-64.so.2"
cp "$WORK/libc.so.6" "$WORK/initramfs/lib64/libc.so.6"
if test -n "$VPP_IMAGE"; then
    # The ELF interpreter is selected before LD_LIBRARY_PATH.  Use the same
    # trixie glibc pair for the VPP executable and the base initramfs paths.
    cp "$WORK/initramfs/lib/x86_64-linux-gnu/libc.so.6" \
       "$WORK/initramfs/lib/libc.so.6"
    cp "$WORK/initramfs/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
       "$WORK/initramfs/lib/ld-linux-x86-64.so.2"
    cp "$WORK/initramfs/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
       "$WORK/initramfs/lib64/ld-linux-x86-64.so.2"
    cp "$WORK/initramfs/lib/x86_64-linux-gnu/libc.so.6" \
       "$WORK/initramfs/lib64/libc.so.6"
fi
# mgrd built in step 1 lives in the container; fetch fresh copy
docker cp danos-iso-build:/tmp/b/danos-mgrd/danos-mgrd "$WORK/initramfs/bin/mgrd"
chmod +x "$WORK/initramfs/bin/mgrd"
if test -x "$WORK/../build/danos-test/fib_live_bridge"; then
  cp "$WORK/../build/danos-test/fib_live_bridge" "$WORK/initramfs/bin/fib_live_bridge"
  chmod +x "$WORK/initramfs/bin/fib_live_bridge"
elif test -x "$PROJECT_ROOT/build/danos-test/fib_live_bridge"; then
  cp "$PROJECT_ROOT/build/danos-test/fib_live_bridge" "$WORK/initramfs/bin/fib_live_bridge"
  chmod +x "$WORK/initramfs/bin/fib_live_bridge"
else
  echo "INFO: fib_live_bridge not built; ISO will omit optional FRR bridge"
fi
test "$DANOS_FIB_BRIDGE_ENABLE" = 1 && touch "$WORK/initramfs/danos-fib-bridge.enabled"
if test "$DANOS_FIB_BRIDGE_ENABLE" = 1 && test -n "$DANOS_ZEBRA_ENDPOINT"; then
  mkdir -p "$WORK/initramfs/etc/danos"
  printf 'DANOS_ZEBRA_ENDPOINT=%q\n' "$DANOS_ZEBRA_ENDPOINT" > "$WORK/initramfs/etc/danos/bridge.env"
  printf 'DANOS_ZAPI_ROUTE_TYPES=%q\n' "${DANOS_ZAPI_ROUTE_TYPES:-9}" >> "$WORK/initramfs/etc/danos/bridge.env"
fi
cp "$PROJECT_ROOT/danos-test/live/init" "$WORK/initramfs/init"
chmod +x "$WORK/initramfs/init"
if test -x "$WORK/gnmic"; then
    cp "$WORK/gnmic" "$WORK/initramfs/bin/gnmic"
    chmod +x "$WORK/initramfs/bin/mgrd" "$WORK/initramfs/bin/gnmic"
else
    chmod +x "$WORK/initramfs/bin/mgrd"
fi
# e1000 module: pre-decompress (busybox insmod can't read .xz)
case "$E1000" in
  *.xz) xz -q -d -c "$WORK/e1000.ko.raw" > "$WORK/initramfs/modules/e1000.ko" ;;
  *)    cp "$WORK/e1000.ko.raw" "$WORK/initramfs/modules/e1000.ko" ;;
esac
case "$VIRTIO_NET" in
  *.xz) xz -q -d -c "$WORK/virtio_net.ko.raw" > "$WORK/initramfs/modules/virtio_net.ko" ;;
  *)    cp "$WORK/virtio_net.ko.raw" "$WORK/initramfs/modules/virtio_net.ko" ;;
esac
for module in failover net_failover virtio_pci uio uio_pci_generic; do
  eval "source=\$${module^^}"
  test -f "$WORK/${module}.ko.raw" || continue
  case "$source" in
    *.xz) xz -q -d -c "$WORK/${module}.ko.raw" > "$WORK/initramfs/modules/${module}.ko" ;;
    *)    cp "$WORK/${module}.ko.raw" "$WORK/initramfs/modules/${module}.ko" ;;
  esac
done

# --- 4. pack initramfs ----------------------------------------------------
cd "$WORK/initramfs"
find . | cpio -o -H newc --quiet | gzip -9 > "$WORK/initramfs.cpio.gz"

# --- 5. ISO ---------------------------------------------------------------
rm -rf "$WORK/isoroot"; mkdir -p "$WORK/isoroot/isolinux"
cp "$WORK/vmlinuz" "$WORK/initramfs.cpio.gz" "$WORK/isoroot/"
cp "$WORK/isolinux.bin" "$WORK/ldlinux.c32" "$WORK/isoroot/isolinux/"
cat > "$WORK/isoroot/isolinux/isolinux.cfg" <<'EOF'
SERIAL 0 115200
DEFAULT danos
PROMPT 0
TIMEOUT 20
LABEL danos
  KERNEL /vmlinuz
  # Keep serial diagnostics for QEMU, but make the VMware/PC VGA console
  # the primary console.  With only ttyS0, VMware's display remains black
  # after ISOLINUX has loaded the kernel and initramfs.
  APPEND initrd=/initramfs.cpio.gz console=ttyS0,115200 console=tty0
EOF
xorriso -as mkisofs -o "$OUT_ISO" -b isolinux/isolinux.bin \
    -c isolinux/boot.cat -no-emul-boot -boot-load-size 4 \
    -boot-info-table -J -R "$WORK/isoroot" 2>&1 | tail -1

echo "=== live ISO: $OUT_ISO ==="
ls -la "$OUT_ISO"
