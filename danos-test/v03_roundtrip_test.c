/*
 * v0.3 vertical roundtrip driver:
 *   gNMI Set -> DPA transaction -> WAL persistence
 *   simulated restart (store dropped) -> WAL recovery -> verify config
 *
 * argv[1]: WAL file path
 */

#include <danos/core/persist.h>
#include <danos/core/object_registry.h>
#include <danos/dpa.h>
#include "../danos-mgmt/src/gnmi/gnmi_proto.h"
#include "../danos-mgmt/src/gnmi/gnmi_grpc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char **argv)
{
    const char *wal = argc > 1 ? argv[1] : "/tmp/danos-v03-roundtrip.wal";

    /* --- boot 1: enable persistence, apply config via gNMI Set -------- */
    assert(danos_persist_enable(wal) == 0);

    uint8_t req[256];
    gnmi_pb_t w;
    gnmi_pb_init(&w, req, sizeof(req));
    gnmi_update_t u;
    memset(&u, 0, sizeof(u));
    assert(gnmi_path_from_str(&u.path, "interfaces/interface[name=wan9]"));
    u.val.kind = GNMI_VAL_JSON_IETF;
    strcpy(u.val.s, "{\"mtu\":9000,\"admin_up\":1}");
    size_t us = gnmi_pb_begin_nested(&w, 4);
    gnmi_encode_path(&w, 1, &u.path);
    gnmi_encode_typed_value(&w, 3, &u.val);
    gnmi_pb_end_nested(&w, us);

    uint8_t resp[4096];
    int rlen = gnmi_handle_set(NULL, req, w.len, resp, sizeof(resp));
    if (rlen < 0) {
        fprintf(stderr, "gNMI Set failed: %d\n", rlen);
        return 1;
    }

    /* --- simulated restart --------------------------------------------- */
    danos_persist_disable();
    g_default_store = NULL;

    /* --- boot 2: recover ------------------------------------------------ */
    assert(danos_persist_enable(wal) == 0);
    int applied = danos_persist_recover();
    if (applied < 1) {
        fprintf(stderr, "recovery applied nothing\n");
        return 1;
    }

    /* --- verify wan9 survived with the gNMI-applied values -------------- */
    danos_tx_t tx;
    assert(danos_tx_begin(&tx, "verify", NULL) == DANOS_OK);
    /* find wan9: ifindex was allocated as max+1; probe 1..64 */
    danos_status_t st = DANOS_ERR_NOT_FOUND;
    danos_iface_t ifc;
    for (unsigned i = 1; i <= 64; i++) {
        st = danos_iface_read(&tx, (danos_ifindex_t)i, &ifc);
        if (st == DANOS_OK && strcmp(ifc.name, "wan9") == 0) break;
    }
    danos_tx_abort(&tx);
    if (st != DANOS_OK || strcmp(ifc.name, "wan9") != 0 ||
        ifc.mtu != 9000 || !ifc.admin_up) {
        fprintf(stderr, "wan9 not recovered correctly\n");
        return 1;
    }

    printf("wan9 recovered: ifindex=%u mtu=%u admin_up=%d (applied=%d)\n",
           ifc.ifindex, ifc.mtu, ifc.admin_up, applied);

    danos_persist_disable();
    return 0;
}
