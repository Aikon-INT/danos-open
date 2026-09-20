*** Settings ***
Library           Process
Library           OperatingSystem
Library           String
Variables         variables.py

*** Keywords ***
Run Gnmic
    [Arguments]    ${args}
    ${rc}    ${out}    Run And Return Rc And Output    ${GNMIC} -a ${GNMI_ADDR} --insecure ${args}
    [Return]    ${rc}    ${out}

Get Capabilities
    ${rc}    ${out}    Run Gnmic    capabilities
    [Return]    ${rc}    ${out}

Get Path
    [Arguments]    ${path}
    ${rc}    ${out}    Run Gnmic    get --path ${path}
    [Return]    ${rc}    ${out}

Set Route
    [Arguments]    ${prefix}    ${gateway}
    ${rc}    ${out}    Run Gnmic    set --update /routes/route[prefix=${prefix}]:::json_ietf:::{"gateway":"${gateway}"}
    [Return]    ${rc}    ${out}

Delete Route
    [Arguments]    ${prefix}
    ${rc}    ${out}    Run Gnmic    set --delete /routes/route[prefix=${prefix}]
    [Return]    ${rc}    ${out}

Set Interface IPv4
    [Arguments]    ${ifname}    ${address}
    ${rc}    ${out}    Run Gnmic    set --update /interfaces/interface[name=${ifname}]/config/ipv4-address:::json_ietf:::"${address}"
    [Return]    ${rc}    ${out}

Delete Interface IPv4
    [Arguments]    ${ifname}
    ${rc}    ${out}    Run Gnmic    set --delete /interfaces/interface[name=${ifname}]/config/ipv4-address
    [Return]    ${rc}    ${out}

Metrics Should Contain
    [Arguments]    ${needle}
    ${rc}    ${out}    Run And Return Rc And Output    curl -s ${METRICS_URL}
    Should Contain    ${out}    ${needle}
