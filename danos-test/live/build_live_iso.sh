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
GNMIC_SRC="${GNMIC:-/tmp/gnmic-bin}"
WORK=/tmp/danos-iso-work
APT_MIRROR="${APT_MIRROR:-https://repo.huaweicloud.com/debian}"

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
E1000=$(docker exec danos-iso-build sh -c \
    'find /lib/modules -name "e1000.ko*" | head -1')
docker cp "danos-iso-build:$E1000" "$WORK/e1000.ko.raw"
VIRTIO_NET=$(docker exec danos-iso-build sh -c \
    'find /lib/modules -name "virtio_net.ko*" | head -1')
docker cp "danos-iso-build:$VIRTIO_NET" "$WORK/virtio_net.ko.raw"
cp "$GNMIC_SRC" "$WORK/gnmic"

# --- 3. initramfs tree ---------------------------------------------------
rm -rf "$WORK/initramfs"
mkdir -p "$WORK/initramfs"/{bin,dev,proc,sys,tmp,lib,lib64,modules}
cp "$WORK/busybox" "$WORK/initramfs/bin/busybox"
cp "$WORK/libc.so.6" "$WORK/initramfs/lib/libc.so.6"
cp "$WORK/ld-linux.so.2" "$WORK/initramfs/lib/ld-linux-x86-64.so.2"
cp "$WORK/ld-linux.so.2" "$WORK/initramfs/lib64/ld-linux-x86-64.so.2"
cp "$WORK/libc.so.6" "$WORK/initramfs/lib64/libc.so.6"
# mgrd built in step 1 lives in the container; fetch fresh copy
docker cp danos-iso-build:/tmp/b/danos-mgrd/danos-mgrd "$WORK/initramfs/bin/mgrd"
cp "$WORK/gnmic" "$WORK/initramfs/bin/gnmic"
chmod +x "$WORK/initramfs/bin/mgrd" "$WORK/initramfs/bin/gnmic"
cp "$PROJECT_ROOT/danos-test/live/init" "$WORK/initramfs/init"
chmod +x "$WORK/initramfs/init"
# e1000 module: pre-decompress (busybox insmod can't read .xz)
case "$E1000" in
  *.xz) xz -q -d -c "$WORK/e1000.ko.raw" > "$WORK/initramfs/modules/e1000.ko" ;;
  *)    cp "$WORK/e1000.ko.raw" "$WORK/initramfs/modules/e1000.ko" ;;
esac
case "$VIRTIO_NET" in
  *.xz) xz -q -d -c "$WORK/virtio_net.ko.raw" > "$WORK/initramfs/modules/virtio_net.ko" ;;
  *)    cp "$WORK/virtio_net.ko.raw" "$WORK/initramfs/modules/virtio_net.ko" ;;
esac

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
  APPEND initrd=/initramfs.cpio.gz console=ttyS0,115200
EOF
xorriso -as mkisofs -o "$OUT_ISO" -b isolinux/isolinux.bin \
    -c isolinux/boot.cat -no-emul-boot -boot-load-size 4 \
    -boot-info-table -J -R "$WORK/isoroot" 2>&1 | tail -1

echo "=== live ISO: $OUT_ISO ==="
ls -la "$OUT_ISO"
