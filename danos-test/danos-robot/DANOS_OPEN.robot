# * DANOS-Open adapter suite for the upstream i-danos/tests test cases.
# *
# * The upstream DANOS suites (danos_restapi.robot et al.) drive the
# * original DANOS REST API over SSH. This adapter implements the same
# * test-case names against DANOS-Open's model-driven gNMI interface,
# * so the upstream acceptance flow can be replayed on DANOS-Open.
# *
# * Mapping (upstream case -> DANOS-Open implementation):
# *   Prerequisite checks            -> gnmic capabilities
# *   Show device version            -> gNMI capabilities version string
# *   Show Interfaces                -> gNMI Get /interfaces
# *   Show Interface Counters        -> /metrics exposition
# *   Set Interface IP               -> NOT SUPPORTED (no addr model yet)
# *   Delete Interface IP            -> NOT SUPPORTED
# *   + DANOS-Open additions: composite route set/get/delete

*** Settings ***
Documentation     DANOS-Open adapter for the upstream DANOS REST API
...               acceptance suite. Same test-case names, gNMI transport.
Library           Process
Library           Collections
Resource          danos_open_keywords.robot

*** Variables ***
${ROUTE_PREFIX}    172.31.0.0/16
${ROUTE_GW}        10.0.2.2

*** Test Cases ***
Prerequisite checks
    ${rc}    ${out}    Get Capabilities
    Should Be Equal As Integers    ${rc}    0
    Should Contain    ${out}    DANOS-Open

Show device version information (GET and PUT operations)
    ${rc}    ${out}    Get Capabilities
    Should Be Equal As Integers    ${rc}    0
    Should Contain    ${out}    DANOS-Open DPA
    Should Contain    ${out}    supported models

Show Interfaces (GET and PUT operations)
    ${rc}    ${out}    Get Path    /interfaces
    Should Be Equal As Integers    ${rc}    0
    Should Contain    ${out}    eth0
    Should Contain    ${out}    mtu

Show Interface Counters (GET and PUT operations)
    Metrics Should Contain    danos_objects_interface
    Metrics Should Contain    danos_programming_attempted_total

Set Interface IP (GET, PUT, SET operations)
    ${rc}    ${out}    Set Interface IPv4    eth0    192.0.2.1/24
    Should Be Equal As Integers    ${rc}    0
    ${rc}    ${out}    Get Path    /interfaces/interface[name=eth0]/config/ipv4-address
    Should Be Equal As Integers    ${rc}    0
    Should Contain    ${out}    192.0.2.1/24

Delete Interface IP (GET, PUT, DELETE operations)
    ${rc}    ${out}    Delete Interface IPv4    eth0
    Should Be Equal As Integers    ${rc}    0

Set Route via gNMI (DANOS-Open composite write)
    ${rc}    ${out}    Set Route    ${ROUTE_PREFIX}    ${ROUTE_GW}
    Should Be Equal As Integers    ${rc}    0
    ${rc}    ${out}    Get Path    /routes/route[prefix=${ROUTE_PREFIX}]
    Should Be Equal As Integers    ${rc}    0
    Should Contain    ${out}    ${ROUTE_GW}

Delete Route via gNMI (DANOS-Open composite write)
    ${rc}    ${out}    Delete Route    ${ROUTE_PREFIX}
    Should Be Equal As Integers    ${rc}    0
    ${rc}    ${out}    Get Path    /routes/route[prefix=${ROUTE_PREFIX}]
    Should Be Equal As Integers    ${rc}    0
    Should Not Contain    ${out}    ${ROUTE_GW}
