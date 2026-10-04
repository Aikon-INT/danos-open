#!/bin/sh
# Bind only explicitly selected, identity-checked I211 functions for the DPDK ISO.
set -eu

sysfs_root="${DANOS_DPDK_SYSFS_ROOT:-/sys}"
driver="${DANOS_BUILD_DPDK_BIND_DRIVER:-none}"
expected_id="${DANOS_BUILD_DPDK_EXPECTED_PCI_ID:-8086:1539}"
ports="${DANOS_BUILD_DPDK_PORTS:-}"

case "$driver" in
    none) echo 'DPDK-PCI-BIND SKIP driver=none'; exit 0 ;;
    uio_pci_generic) ;;
    *) echo "DPDK-PCI-BIND FAIL unsupported-driver=$driver"; exit 1 ;;
esac
test -n "$ports" || { echo 'DPDK-PCI-BIND FAIL no-BDFs-configured'; exit 1; }
case "$expected_id" in
    [[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]]:[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]]) ;;
    *) echo "DPDK-PCI-BIND FAIL invalid-expected-pci-id=$expected_id"; exit 1 ;;
esac

expected_vendor="0x$(printf '%s' "$expected_id" | cut -d: -f1 | tr '[:upper:]' '[:lower:]')"
expected_device="0x$(printf '%s' "$expected_id" | cut -d: -f2 | tr '[:upper:]' '[:lower:]')"
driver_dir="$sysfs_root/bus/pci/drivers/$driver"
test -d "$driver_dir" || { echo "DPDK-PCI-BIND FAIL driver-unavailable=$driver"; exit 1; }

report_bind_diagnostics() {
    bdf="$1"
    stage="$2"
    device_dir="$sysfs_root/bus/pci/devices/$bdf"
    current_driver=$(basename "$(readlink "$device_dir/driver" 2>/dev/null || echo unbound)")
    override=$(cat "$device_dir/driver_override" 2>/dev/null || echo unavailable)
    echo "DPDK-PCI-DIAG bdf=$bdf stage=$stage current_driver=$current_driver driver_override=$override"
    if command -v dmesg >/dev/null 2>&1; then
        dmesg 2>/dev/null | tail -n 12 | while IFS= read -r kernel_line; do
            echo "DPDK-PCI-KMSG $kernel_line"
        done
    fi
}

bound=0
for bdf in $ports; do
    printf '%s\n' "$bdf" | grep -Eq '^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$' || {
        echo "DPDK-PCI-BIND FAIL invalid-BDF=$bdf"; exit 1;
    }
    device_dir="$sysfs_root/bus/pci/devices/$bdf"
    test -d "$device_dir" || { echo "DPDK-PCI-BIND FAIL device-absent=$bdf"; exit 1; }
    vendor=$(cat "$device_dir/vendor")
    device=$(cat "$device_dir/device")
    if test "$vendor:$device" != "$expected_vendor:$expected_device"; then
        echo "DPDK-PCI-BIND FAIL unexpected-device=$bdf:$vendor:$device expected=$expected_vendor:$expected_device"
        exit 1
    fi

    current_driver=$(basename "$(readlink "$device_dir/driver" 2>/dev/null || echo unbound)")
    if test "$current_driver" != "$driver"; then
        # Pin the override before unbinding so the kernel cannot race-rebind a
        # native driver; new_id makes uio_pci_generic accept this exact PCI ID.
        printf '%s' "$driver" > "$device_dir/driver_override" || {
            echo "DPDK-PCI-BIND FAIL driver-override=$bdf"; exit 1;
        }
        if test "$current_driver" != unbound; then
            printf '%s' "$bdf" > "$sysfs_root/bus/pci/drivers/$current_driver/unbind" || {
                echo "DPDK-PCI-BIND FAIL unbind=$bdf driver=$current_driver"; exit 1;
            }
        fi
        printf '%s %s' "${expected_vendor#0x}" "${expected_device#0x}" \
            > "$driver_dir/new_id" 2>/dev/null || true
        if printf '%s' "$bdf" > "$driver_dir/bind"; then
            :
        else
            bind_rc=$?
            echo "DPDK-PCI-BIND FAIL bind=$bdf driver=$driver rc=$bind_rc"
            report_bind_diagnostics "$bdf" bind
            exit 1
        fi
        current_driver=$(basename "$(readlink "$device_dir/driver" 2>/dev/null || echo unbound)")
    fi
    test "$current_driver" = "$driver" || {
        echo "DPDK-PCI-BIND FAIL verify=$bdf driver=$current_driver"; exit 1;
    }
    echo "DPDK-PCI-BIND PASS bdf=$bdf pci_id=${vendor#0x}:${device#0x} driver=$driver"
    bound=$((bound + 1))
done

test "$bound" -gt 0 || { echo 'DPDK-PCI-BIND FAIL no-devices-bound'; exit 1; }
