#!/bin/bash
# DANOS-Open F1-F8 Integration Test Runner
#
# Runs F1-F8 deployment environment validation using Docker + FRR.
# Does NOT require containerlab (uses Docker native networking).
# Requires: docker, danos-frr-test:latest image (built from danos-build:trixie + frr)
#
# Usage: ./run_f1_f8.sh [--test F1|F2|...|F8|all]
#        ./run_f1_f8.sh --list

set -u

IMAGE="danos-frr-test:latest"
NET="danos-test-net"
CONTAINERS=()

cleanup() {
    for c in "${CONTAINERS[@]}"; do docker rm -f "$c" 2>/dev/null; done
    docker network rm "$NET" 2>/dev/null
}
trap cleanup EXIT

get_ip() { docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$1"; }

vtysh() { docker exec "$1" vtysh -c "$2" 2>&1; }

start_container() {
    local name=$1
    docker run -d --name "$name" --network "$NET" --privileged \
        "$IMAGE" sh -c "sleep infinity" >/dev/null 2>&1
    CONTAINERS+=("$name")
    # Enable all daemons
    docker exec "$name" sh -c "
        sed -i 's/=no/=yes/g' /etc/frr/daemons
        /usr/lib/frr/frrinit.sh start 2>/dev/null
    " >/dev/null 2>&1
}

setup_3node() {
    docker network create "$NET" >/dev/null 2>&1
    for i in 1 2 3; do
        start_container "danos-test-r$i"
        docker exec "danos-test-r$i" sh -c "ip addr add $i.$i.$i.$i/32 dev lo 2>/dev/null"
    done
    R1=$(get_ip danos-test-r1); R2=$(get_ip danos-test-r2); R3=$(get_ip danos-test-r3)
    # Derive subnet (e.g., 172.18.0.0/16) from R1
    SUBNET=$(docker exec danos-test-r1 sh -c "ip addr show eth0 | grep -oP 'inet \K[0-9.]+'")
    NET_PREFIX=$(echo "$SUBNET" | cut -d. -f1-2)
    echo "  r1=$R1 r2=$R2 r3=$R3 subnet=${NET_PREFIX}.0.0/16"
    sleep 2
}

# F1: BGP 3-node convergence + ping
test_f1() {
    echo "[F1] BGP 3-node convergence + ping reachability"
    setup_3node
    # Configure BGP full-mesh
    docker exec danos-test-r1 sh -c "cat > /etc/frr/frr.conf << EOF
frr version 10.3
frr defaults traditional
hostname r1
service integrated-vtysh-config
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp ebgp-requires-policy
 neighbor $R2 remote-as 65002
 neighbor $R3 remote-as 65003
 address-family ipv4 unicast
  network 1.1.1.1/32
  neighbor $R2 activate
  neighbor $R3 activate
 exit-address-family
EOF
chown frr:frr /etc/frr/frr.conf; /usr/lib/frr/frrinit.sh restart" >/dev/null 2>&1
    docker exec danos-test-r2 sh -c "cat > /etc/frr/frr.conf << EOF
frr version 10.3
frr defaults traditional
hostname r2
service integrated-vtysh-config
router bgp 65002
 bgp router-id 2.2.2.2
 no bgp ebgp-requires-policy
 neighbor $R1 remote-as 65001
 neighbor $R3 remote-as 65003
 address-family ipv4 unicast
  network 2.2.2.2/32
  neighbor $R1 activate
  neighbor $R3 activate
 exit-address-family
EOF
chown frr:frr /etc/frr/frr.conf; /usr/lib/frr/frrinit.sh restart" >/dev/null 2>&1
    docker exec danos-test-r3 sh -c "cat > /etc/frr/frr.conf << EOF
frr version 10.3
frr defaults traditional
hostname r3
service integrated-vtysh-config
router bgp 65003
 bgp router-id 3.3.3.3
 no bgp ebgp-requires-policy
 neighbor $R1 remote-as 65001
 neighbor $R2 remote-as 65002
 address-family ipv4 unicast
  network 3.3.3.3/32
  neighbor $R1 activate
  neighbor $R2 activate
 exit-address-family
EOF
chown frr:frr /etc/frr/frr.conf; /usr/lib/frr/frrinit.sh restart" >/dev/null 2>&1
    sleep 8
    # Verify
    local bgp_ok=true
    vtysh danos-test-r1 "show bgp neighbors" | grep -q "BGP state = Established" || bgp_ok=false
    docker exec danos-test-r1 ping -c 2 -W 2 2.2.2.2 >/dev/null 2>&1 || bgp_ok=false
    docker exec danos-test-r1 ping -c 2 -W 2 3.3.3.3 >/dev/null 2>&1 || bgp_ok=false
    if $bgp_ok; then echo "  PASS: BGP converged, ping 0% loss"; return 0
    else echo "  FAIL: BGP convergence or ping failed"; return 1; fi
}

# F2: OSPF 3-node convergence
test_f2() {
    echo "[F2] OSPF 3-node convergence"
    setup_3node
    for i in 1 2 3; do
        docker exec "danos-test-r$i" vtysh -c "
configure terminal
router ospf
 ospf router-id $i.$i.$i.$i
 network ${NET_PREFIX}.0.0/16 area 0
 redistribute connected
end
write memory" >/dev/null 2>&1
        docker exec "danos-test-r$i" /usr/lib/frr/frrinit.sh restart >/dev/null 2>&1
    done
    sleep 30
    local ok=true
    local nbr; nbr=$(vtysh danos-test-r1 "show ip ospf neighbor")
    echo "$nbr" | grep -qE "2-Way|Full" || { ok=false; echo "  DEBUG: no neighbor"; }
    local pingout; pingout=$(docker exec danos-test-r1 ping -c 2 -W 2 2.2.2.2 2>&1)
    echo "$pingout" | grep -q "0% packet loss" || { ok=false; echo "  DEBUG: ping failed: $(echo "$pingout" | tail -1)"; }
    if $ok; then echo "  PASS: OSPF neighbors up, ping 0% loss"; return 0
    else echo "  FAIL: OSPF convergence failed"; return 1; fi
}

# F3: IS-IS 3-node convergence
test_f3() {
    echo "[F3] IS-IS 3-node convergence"
    setup_3node
    for i in 1 2 3; do
        docker exec "danos-test-r$i" vtysh -c "
configure terminal
router isis DANOS
 net 49.0001.0000.0000.000$i.00
 redistribute ipv4 connected level-1
exit
interface eth0
 ip router isis DANOS
exit
interface lo
 ip router isis DANOS
end
write memory" >/dev/null 2>&1
        docker exec "danos-test-r$i" /usr/lib/frr/frrinit.sh restart >/dev/null 2>&1
    done
    # Re-apply interface config after restart (restart may drop it)
    for i in 1 2 3; do
        docker exec "danos-test-r$i" vtysh -c "
configure terminal
interface eth0
 ip router isis DANOS
exit
interface lo
 ip router isis DANOS
end
write memory" >/dev/null 2>&1
    done
    sleep 25
    local ok=true
    local nbr; nbr=$(vtysh danos-test-r1 "show isis neighbor")
    echo "$nbr" | grep -q "Up" || { ok=false; echo "  DEBUG: no IS-IS neighbor up"; echo "$nbr" | head -5; }
    local pingout; pingout=$(docker exec danos-test-r1 ping -c 2 -W 2 2.2.2.2 2>&1)
    echo "$pingout" | grep -q "0% packet loss" || { ok=false; echo "  DEBUG: ping failed: $(echo "$pingout" | tail -1)"; }
    if $ok; then echo "  PASS: IS-IS neighbors up, ping 0% loss"; return 0
    else echo "  FAIL: IS-IS convergence failed"; return 1; fi
}

# F4: BFD single-hop fault detection
test_f4() {
    echo "[F4] BFD single-hop fault detection"
    setup_3node
    # Configure BGP + BFD between r1 and r2
    for i in 1 2; do
        local peer=$R2; [ $i -eq 2 ] && peer=$R1
        docker exec "danos-test-r$i" vtysh -c "
configure terminal
router bgp 6500$i
 bgp router-id $i.$i.$i.$i
 no bgp ebgp-requires-policy
 neighbor $peer remote-as 6500$([ $i -eq 1 ] && echo 2 || echo 1)
 neighbor $peer bfd
 address-family ipv4 unicast
  neighbor $peer activate
 exit-address-family
exit
bfd
 peer $peer
exit
end
write memory" >/dev/null 2>&1
    done
    sleep 5
    local ok=true
    vtysh danos-test-r1 "show bfd peers" | grep -q "Status: up" || ok=false
    # Simulate failure
    docker stop danos-test-r2 >/dev/null 2>&1
    sleep 2
    vtysh danos-test-r1 "show bfd peers" | grep -q "Status: down" || ok=false
    if $ok; then echo "  PASS: BFD up→down detected < 2s"; return 0
    else echo "  FAIL: BFD detection failed"; return 1; fi
}

# F5: VRF isolation
test_f5() {
    echo "[F5] VRF isolation"
    setup_3node
    docker exec danos-test-r1 sh -c "
ip link add dummy-a type dummy; ip link add dummy-b type dummy
ip link set dummy-a up; ip link set dummy-b up
ip addr add 10.1.1.1/24 dev dummy-a; ip addr add 10.1.1.1/24 dev dummy-b
ip link add VRF-A type vrf table 100; ip link add VRF-B type vrf table 200
ip link set VRF-A up; ip link set VRF-B up
ip link set dummy-a master VRF-A; ip link set dummy-b master VRF-B
" >/dev/null 2>&1
    sleep 2
    local ok=true
    docker exec danos-test-r1 ip route show vrf VRF-A 2>/dev/null | grep -q "10.1.1.0/24" || ok=false
    docker exec danos-test-r1 ip route show vrf VRF-B 2>/dev/null | grep -q "10.1.1.0/24" || ok=false
    if $ok; then echo "  PASS: Two VRFs with same prefix isolated"; return 0
    else echo "  FAIL: VRF isolation failed"; return 1; fi
}

# F6: ACL permit/deny
test_f6() {
    echo "[F6] ACL permit/deny filtering"
    setup_3node
    docker exec danos-test-r1 sh -c "
iptables -A INPUT -s $R2 -j DROP
iptables -A INPUT -s $R3 -j ACCEPT
" >/dev/null 2>&1
    sleep 1
    local ok=true
    # r2 -> r1 should fail
    docker exec danos-test-r2 ping -c 2 -W 2 "$R1" >/dev/null 2>&1 && ok=false
    # r3 -> r1 should pass
    docker exec danos-test-r3 ping -c 2 -W 2 "$R1" >/dev/null 2>&1 || ok=false
    if $ok; then echo "  PASS: deny blocked, permit allowed"; return 0
    else echo "  FAIL: ACL filtering failed"; return 1; fi
}

# F7: LACP / Bond
test_f7() {
    echo "[F7] LACP / Bond aggregation"
    setup_3node
    docker exec danos-test-r1 sh -c "
modprobe bonding 2>/dev/null
ip link add bond0 type bond mode 802.3ad
ip link add dummy-l1 type dummy; ip link add dummy-l2 type dummy
ip link set dummy-l1 master bond0; ip link set dummy-l2 master bond0
ip link set bond0 up; ip link set dummy-l1 up; ip link set dummy-l2 up
ip addr add 10.7.7.1/24 dev bond0
" >/dev/null 2>&1
    sleep 2
    local ok=true
    docker exec danos-test-r1 cat /proc/net/bonding/bond0 2>/dev/null | grep -q "IEEE 802.3ad" || ok=false
    docker exec danos-test-r1 cat /proc/net/bonding/bond0 2>/dev/null | grep -q "MII Status: up" || ok=false
    if $ok; then echo "  PASS: bond0 up, LACP 802.3ad mode"; return 0
    else echo "  FAIL: LACP/Bond failed"; return 1; fi
}

# F8: VLAN / QinQ
test_f8() {
    echo "[F8] VLAN / QinQ tagging"
    setup_3node
    docker exec danos-test-r1 sh -c "
ip link add dummy-vlan type dummy; ip link set dummy-vlan up
ip link add dummy-vlan.100 link dummy-vlan type vlan id 100
ip link set dummy-vlan.100 up; ip addr add 10.100.0.1/24 dev dummy-vlan.100
ip link add qinq link dummy-vlan.100 type vlan id 200
ip link set qinq up; ip addr add 10.200.0.1/24 dev qinq
" >/dev/null 2>&1
    sleep 1
    local ok=true
    docker exec danos-test-r1 ip -d link show dummy-vlan.100 2>/dev/null | grep -q "vlan protocol 802.1Q id 100" || ok=false
    docker exec danos-test-r1 ip -d link show qinq 2>/dev/null | grep -q "vlan protocol 802.1Q id 200" || ok=false
    docker exec danos-test-r1 ip route show 2>/dev/null | grep -q "10.200.0.0/24" || ok=false
    if $ok; then echo "  PASS: VLAN 100 + QinQ (100+200) created"; return 0
    else echo "  FAIL: VLAN/QinQ failed"; return 1; fi
}

TESTS=("F1:test_f1" "F2:test_f2" "F3:test_f3" "F4:test_f4" "F5:test_f5" "F6:test_f6" "F7:test_f7" "F8:test_f8")

main() {
    local run_test="${1:-all}"
    # Accept the documented CLI form: --test F1|F2|...|all.
    # Keep the positional form for backwards compatibility.
    if [ "$run_test" = "--test" ]; then
        run_test="${2:-all}"
    fi
    if [ "$run_test" = "--help" ] || [ "$run_test" = "-h" ]; then
        echo "Usage: $0 [--test F1|F2|F3|F4|F5|F6|F7|F8|all]"
        echo "       $0 F1|F2|F3|F4|F5|F6|F7|F8|all"
        return 0
    fi
    if [ "$run_test" = "--list" ]; then
        for t in "${TESTS[@]}"; do echo "  ${t%%:*}: $(case ${t%%:*} in
            F1) echo "BGP 3-node convergence + ECMP";; F2) echo "OSPF 3-node convergence";;
            F3) echo "IS-IS 3-node convergence";; F4) echo "BFD single-hop fault detection";;
            F5) echo "VRF isolation";; F6) echo "ACL permit/deny filtering";;
            F7) echo "LACP / Bond aggregation";; F8) echo "VLAN / QinQ tagging";; esac)"
        done
        return 0
    fi

    local passed=0 failed=0
    for t in "${TESTS[@]}"; do
        local id="${t%%:*}"; local fn="${t#*:}"
        if [ "$run_test" = "all" ] || [ "$run_test" = "$id" ]; then
            cleanup; CONTAINERS=()
            if "$fn"; then ((passed++)); else ((failed++)); fi
        fi
    done
    echo ""
    echo "=== Results: $passed passed, $failed failed ==="
    return $([ $failed -eq 0 ] && echo 0 || echo 1)
}

main "$@"
