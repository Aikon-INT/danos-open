/*
 * DANOS-Open Core: Reconciler Anti-Flap Implementation (B9)
 */

#include <danos/core/antiflap.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

uint64_t antiflap_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static antiflap_entry_t *find_entry(antiflap_ctx_t *ctx, uint64_t obj_key)
{
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        if (ctx->entries[i].obj_key == obj_key) {
            return &ctx->entries[i];
        }
    }
    return NULL;
}

static antiflap_entry_t *create_entry(antiflap_ctx_t *ctx, uint64_t obj_key)
{
    if (ctx->entry_count >= 256) return NULL;
    antiflap_entry_t *e = &ctx->entries[ctx->entry_count++];
    memset(e, 0, sizeof(*e));
    e->obj_key = obj_key;
    return e;
}

int antiflap_init(antiflap_ctx_t *ctx, const antiflap_config_t *config)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = config ? *config : ANTIFLAP_DEFAULT;
    if (ctx->config.window_ms == 0) ctx->config.window_ms = 5000;
    if (ctx->config.max_count == 0) ctx->config.max_count = 3;
    pthread_mutex_init(&ctx->lock, NULL);
    return 0;
}

void antiflap_fini(antiflap_ctx_t *ctx)
{
    pthread_mutex_destroy(&ctx->lock);
}

bool antiflap_check_and_record(antiflap_ctx_t *ctx, uint64_t obj_key)
{
    if (!ctx) return true;
    pthread_mutex_lock(&ctx->lock);

    antiflap_entry_t *e = find_entry(ctx, obj_key);
    if (!e) {
        e = create_entry(ctx, obj_key);
        if (!e) {
            pthread_mutex_unlock(&ctx->lock);
            return true;
        }
    }

    uint64_t now = antiflap_now_ns();
    uint64_t window_ns = (uint64_t)ctx->config.window_ms * 1000000ULL;

    if (e->flapping) {
        e->suppressed++;
        ctx->total_suppressed++;
        pthread_mutex_unlock(&ctx->lock);
        return false;
    }

    if (e->count == 0 || (now - e->window_start) > window_ns) {
        e->window_start = now;
        e->count = 1;
    } else {
        e->count++;
    }

    e->total_repairs++;

    if (e->count > ctx->config.max_count) {
        e->flapping = true;
        e->suppressed++;
        ctx->total_suppressed++;
        pthread_mutex_unlock(&ctx->lock);
        return false;
    }

    pthread_mutex_unlock(&ctx->lock);
    return true;
}

bool antiflap_is_flapping(antiflap_ctx_t *ctx, uint64_t obj_key)
{
    if (!ctx) return false;
    pthread_mutex_lock(&ctx->lock);
    antiflap_entry_t *e = find_entry(ctx, obj_key);
    bool result = e ? e->flapping : false;
    pthread_mutex_unlock(&ctx->lock);
    return result;
}

int antiflap_clear(antiflap_ctx_t *ctx, uint64_t obj_key)
{
    if (!ctx) return -1;
    pthread_mutex_lock(&ctx->lock);
    antiflap_entry_t *e = find_entry(ctx, obj_key);
    if (e) {
        e->flapping = false;
        e->count = 0;
        e->window_start = 0;
    }
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

void antiflap_get_stats(antiflap_ctx_t *ctx, antiflap_stats_t *out)
{
    if (!ctx || !out) return;
    pthread_mutex_lock(&ctx->lock);
    out->tracked_objects = ctx->entry_count;
    out->flapping_objects = 0;
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        if (ctx->entries[i].flapping) out->flapping_objects++;
    }
    out->total_suppressed = ctx->total_suppressed;
    pthread_mutex_unlock(&ctx->lock);
}
