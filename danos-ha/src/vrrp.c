/*
 * DANOS-Open HA: VRRP Implementation
 *
 * VRRPv3 session management.
 */

#include <danos/ha/vrrp.h>
#include <string.h>

#define VRRP_MAX_SESSIONS 64

typedef struct {
    danos_vrrp_session_t sess;
    danos_vrrp_state_t   state;
    bool                 used;
} vrrp_entry_t;

static vrrp_entry_t g_sessions[VRRP_MAX_SESSIONS];
static bool g_initialized = false;

static vrrp_entry_t *find_session(danos_vrrp_id_t vrid, uint32_t ifindex)
{
    for (int i = 0; i < VRRP_MAX_SESSIONS; i++) {
        if (g_sessions[i].used &&
            g_sessions[i].sess.vrid == vrid &&
            g_sessions[i].sess.ifindex == ifindex) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

static vrrp_entry_t *find_free(void)
{
    for (int i = 0; i < VRRP_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) return &g_sessions[i];
    }
    return NULL;
}

int danos_vrrp_init(void)
{
    memset(g_sessions, 0, sizeof(g_sessions));
    g_initialized = true;
    return 0;
}

int danos_vrrp_create(const danos_vrrp_session_t *sess)
{
    if (!sess) return -1;
    if (!g_initialized) danos_vrrp_init();
    if (find_session(sess->vrid, sess->ifindex)) return -1;

    vrrp_entry_t *e = find_free();
    if (!e) return -1;

    e->sess = *sess;
    e->state = DANOS_VRRP_STATE_INIT;
    e->used = true;
    return 0;
}

int danos_vrrp_update(const danos_vrrp_session_t *sess)
{
    if (!sess) return -1;
    vrrp_entry_t *e = find_session(sess->vrid, sess->ifindex);
    if (!e) return -1;
    e->sess = *sess;
    return 0;
}

int danos_vrrp_delete(danos_vrrp_id_t vrid, uint32_t ifindex)
{
    vrrp_entry_t *e = find_session(vrid, ifindex);
    if (!e) return -1;
    e->used = false;
    return 0;
}

int danos_vrrp_read(danos_vrrp_id_t vrid, uint32_t ifindex,
                    danos_vrrp_session_t *out)
{
    if (!out) return -1;
    vrrp_entry_t *e = find_session(vrid, ifindex);
    if (!e) return -1;
    *out = e->sess;
    return 0;
}

danos_vrrp_state_t danos_vrrp_get_state(danos_vrrp_id_t vrid, uint32_t ifindex)
{
    vrrp_entry_t *e = find_session(vrid, ifindex);
    if (!e) return DANOS_VRRP_STATE_INIT;
    return e->state;
}
