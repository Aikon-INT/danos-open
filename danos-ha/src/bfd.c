/*
 * DANOS-Open HA: BFD Multi-hop Implementation
 *
 * BFD session management with state machine and callbacks.
 */

#include <danos/ha/bfd.h>
#include <string.h>
#include <stdlib.h>

#define BFD_MAX_SESSIONS 256

typedef struct {
    danos_bfd_session_t sess;
    danos_bfd_state_t   state;
    bool                used;
} bfd_entry_t;

static bfd_entry_t g_sessions[BFD_MAX_SESSIONS];
static danos_bfd_cb_t g_callback = NULL;
static void *g_cb_user = NULL;
static bool g_initialized = false;

static bfd_entry_t *find_session(danos_bfd_id_t id)
{
    for (int i = 0; i < BFD_MAX_SESSIONS; i++) {
        if (g_sessions[i].used && g_sessions[i].sess.id == id) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

static bfd_entry_t *find_free(void)
{
    for (int i = 0; i < BFD_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) return &g_sessions[i];
    }
    return NULL;
}

int danos_bfd_init(void)
{
    memset(g_sessions, 0, sizeof(g_sessions));
    g_callback = NULL;
    g_cb_user = NULL;
    g_initialized = true;
    return 0;
}

int danos_bfd_create(const danos_bfd_session_t *sess)
{
    if (!sess) return -1;
    if (!g_initialized) danos_bfd_init();
    if (find_session(sess->id)) return -1; /* duplicate */

    bfd_entry_t *e = find_free();
    if (!e) return -1; /* table full */

    e->sess = *sess;
    e->state = DANOS_BFD_STATE_DOWN;
    e->used = true;
    return 0;
}

int danos_bfd_update(const danos_bfd_session_t *sess)
{
    if (!sess) return -1;
    bfd_entry_t *e = find_session(sess->id);
    if (!e) return -1;
    e->sess = *sess;
    return 0;
}

int danos_bfd_delete(danos_bfd_id_t id)
{
    bfd_entry_t *e = find_session(id);
    if (!e) return -1;
    e->used = false;
    return 0;
}

int danos_bfd_read(danos_bfd_id_t id, danos_bfd_session_t *out)
{
    if (!out) return -1;
    bfd_entry_t *e = find_session(id);
    if (!e) return -1;
    *out = e->sess;
    return 0;
}

danos_bfd_state_t danos_bfd_get_state(danos_bfd_id_t id)
{
    bfd_entry_t *e = find_session(id);
    if (!e) return DANOS_BFD_STATE_DOWN;
    return e->state;
}

int danos_bfd_register_callback(danos_bfd_cb_t cb, void *user)
{
    g_callback = cb;
    g_cb_user = user;
    return 0;
}

/* Internal: set state and notify callback (used by BFD packet processing) */
void danos_bfd_set_state(danos_bfd_id_t id, danos_bfd_state_t new_state)
{
    bfd_entry_t *e = find_session(id);
    if (!e) return;
    if (e->state == new_state) return;

    danos_bfd_state_t old = e->state;
    e->state = new_state;
    if (g_callback) {
        g_callback(id, old, new_state, g_cb_user);
    }
}
