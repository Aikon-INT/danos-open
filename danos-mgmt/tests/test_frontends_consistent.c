/*
 * Test: northbound consistency (v0.7, P1)
 *
 * The same leaf (interface mtu) mutated through all three front ends —
 * gNMI Set, CLI configure-mode, NETCONF edit-config — must produce the
 * identical store state and identical validation results.
 */

#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include "../src/gnmi/model_paths.h"
#include "../src/cli/cli.h"
#include "../src/netconf/netconf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static danos_iface_t lookup_iface(const char *name)
{
    danos_iface_t out;
    memset(&out, 0, sizeof(out));
    /* small direct scan over the default store via public read of all
     * ids is impractical; use a bounded probe by index */
    danos_tx_t tx;
    if (danos_tx_begin(&tx, "probe", NULL) != DANOS_OK) return out;
    for (unsigned i = 1; i <= 64; i++) {
        size_t sz = sizeof(out);
        if (danos_object_read(g_default_store, DANOS_OBJ_IFACE, i,
                              &out, &sz) == DANOS_OK &&
            strcmp(out.name, name) == 0) {
            danos_tx_abort(&tx);
            return out;
        }
    }
    danos_tx_abort(&tx);
    return out;
}

static void seed_iface(void)
{
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "seed", NULL) == DANOS_OK);
    danos_iface_t ifc;
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifindex = 1;
    strcpy(ifc.name, "eth0");
    ifc.mtu = 1500;
    ifc.admin_up = true;
    if (danos_iface_create(&tx, &ifc) != DANOS_OK) {
        danos_tx_abort(&tx);
        return;
    }
    danos_tx_prepare(&tx);
    danos_tx_validate(&tx);
    danos_tx_commit(&tx);
}

/* Apply one leaf via the model registry directly (the gNMI Set code
 * path uses exactly this function) */
static void gnmi_set_mtu(const char *name, unsigned mtu)
{
    danos_iface_t target = lookup_iface(name);
    assert(target.ifindex);
    gnmi_typed_value_t v = { .kind = GNMI_VAL_UINT, .u = mtu };
    assert(gnmi_model_apply_leaf(DANOS_OBJ_IFACE, GNMI_FIELD_MTU,
                                 &target, sizeof(target), &v) == DANOS_OK);
    assert(danos_object_update(g_default_store, DANOS_OBJ_IFACE,
                               target.ifindex, &target,
                               sizeof(target)) == DANOS_OK);
}

int main(void)
{
    seed_iface();

    /* 1. gNMI path sets mtu 2000 */
    gnmi_set_mtu("eth0", 2000);
    assert(lookup_iface("eth0").mtu == 2000);

    /* 2. CLI sets mtu 3000 (configure mode + set command) */
    danos_cli_ctx_t cli;
    danos_cli_init(&cli);
    assert(danos_cli_process(&cli, "configure") == 0);
    assert(danos_cli_process(&cli, "set interface eth0 mtu 3000") == 0);
    assert(lookup_iface("eth0").mtu == 3000);

    /* 2b. CLI validation: out-of-range mtu rejected, value unchanged */
    assert(danos_cli_process(&cli, "set interface eth0 mtu 50") != 0);
    assert(lookup_iface("eth0").mtu == 3000);

    /* 3. NETCONF edit-config sets mtu 4000 */
    netconf_ctx_t nc;
    netconf_init(&nc, 0);
    char *xml = strdup(
        "<rpc><edit-config><config>"
        "<interface><name>eth0</name><mtu>4000</mtu></interface>"
        "</config></edit-config></rpc>");
    char *resp = netconf_handle_rpc(&nc, xml);
    assert(resp && strstr(resp, "<ok/>"));
    free(resp);
    free(xml);
    assert(lookup_iface("eth0").mtu == 4000);

    /* 4. Interface L3 address is a model value and survives object update. */
    danos_iface_t addressed = lookup_iface("eth0");
    gnmi_typed_value_t ip = { .kind = GNMI_VAL_STRING };
    strcpy(ip.s, "192.0.2.1/24");
    assert(gnmi_model_apply_leaf(DANOS_OBJ_IFACE, GNMI_FIELD_IPV4_ADDRESS,
                                 &addressed, sizeof(addressed), &ip) == DANOS_OK);
    assert(addressed.ipv4_address.addr.af == DANOS_AF_IPV4);
    assert(addressed.ipv4_address.prefix_len == 24);
    assert(danos_object_update(g_default_store, DANOS_OBJ_IFACE,
                               addressed.ifindex, &addressed,
                               sizeof(addressed)) == DANOS_OK);
    gnmi_typed_value_t read_ip;
    assert(gnmi_model_read_leaf(DANOS_OBJ_IFACE, addressed.ifindex,
                                GNMI_FIELD_IPV4_ADDRESS, &addressed,
                                sizeof(addressed), &read_ip) == DANOS_OK);
    assert(strcmp(read_ip.s, "192.0.2.1/24") == 0);

    /* 3b. NETCONF validation: same out-of-range rejected with rpc-error */
    xml = strdup(
        "<rpc><edit-config><config>"
        "<interface><name>eth0</name><mtu>50</mtu></interface>"
        "</config></edit-config></rpc>");
    resp = netconf_handle_rpc(&nc, xml);
    assert(resp && strstr(resp, "rpc-error"));
    free(resp);
    free(xml);
    assert(lookup_iface("eth0").mtu == 4000);

    printf("=== frontends_consistent_test: ALL PASSED ===\n");
    return 0;
}
