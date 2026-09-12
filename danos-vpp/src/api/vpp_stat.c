/*
 * DANOS-Open VPP Backend: Stat Segment Client (v0.3)
 *
 * Real implementation of the VPP stat segment access protocol
 * (FDio VPP src/vpp-api/client/stat_client.c, stat segment v2):
 *
 *   1. Connect to the stat segment socket (default /run/vpp/stats.sock)
 *      as AF_UNIX SOCK_SEQPACKET. The client sends nothing.
 *   2. VPP immediately replies with one SCM_RIGHTS ancillary message
 *      carrying the file descriptor of the shared stats memory segment.
 *   3. mmap() the segment read-only. The shared header holds the
 *      version, epoch, in_progress counter and the directory offset.
 *   4. Walk the directory (offset-linked entries) to resolve counter
 *      names; read values under epoch/in_progress consistency guard.
 *
 * Directory entry layout (v2 segment, natural alignment, 32 bytes):
 *   u32 type; u8[4 pad]; u64 value/offset union; u64 offset_vector;
 *   u64 name (byte offset from segment base).
 *
 * Counter types: SCALAR=1 (value inline), SIMPLE_COUNTER=2 (per-worker
 * vector of counter arrays, summed), COMBINED_COUNTER=3 (packets+bytes,
 * packets summed).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
/* own interface */
int  danos_vpp_stat_connect(void);
void danos_vpp_stat_disconnect(void);
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define STAT_SOCK_PATH_DEFAULT "/run/vpp/stats.sock"
#define STAT_DIR_MAX 65536

/* stat_dir_type_t */
#define STAT_DIR_TYPE_ILLEGAL          0
#define STAT_DIR_TYPE_SCALAR           1
#define STAT_DIR_TYPE_SIMPLE_COUNTER   2
#define STAT_DIR_TYPE_COMBINED_COUNTER 3
#define STAT_DIR_TYPE_NAME_VECTOR      4
#define STAT_DIR_TYPE_EMPTY            5

/* stat_segment_shared_header_t, natural alignment (40 bytes) */
typedef struct {
    uint32_t version;
    uint32_t little_endian;
    uint64_t epoch;
    uint64_t in_progress;
    uint64_t directory_offset;
    uint64_t error_offset;
} stat_shared_header_t;

/* stat_segment_directory_entry_t as stored in the segment (offsets,
 * not pointers), natural alignment (32 bytes) */
typedef struct {
    uint32_t type;
    uint32_t _pad;
    uint64_t value;          /* scalar value / index */
    uint64_t offset_vector;  /* byte offset from base */
    uint64_t name;           /* byte offset from base */
} stat_dir_entry_t;

static struct {
    int fd;             /* stats socket (closed after fd recv) */
    int mem_fd;         /* shared memory fd */
    void *base;
    size_t size;
    bool connected;
    char sock_path[256];
} g_stat;

/* External hook set by vpp_api.c */
const char *danos_vpp_stat_sock_path(void);
void danos_vpp_api_set_stat_sock_path(const char *path);

static const char *stat_sock_path(void)
{
    return g_stat.sock_path[0] ? g_stat.sock_path : STAT_SOCK_PATH_DEFAULT;
}

void danos_vpp_stat_set_sock_path(const char *path)
{
    if (path) snprintf(g_stat.sock_path, sizeof(g_stat.sock_path), "%s", path);
}

static void *seg_ptr(uint64_t offset, size_t need)
{
    if (offset == 0 || offset + need > g_stat.size) return NULL;
    return (char *)g_stat.base + offset;
}

/* =========================================================================
 * Connect / disconnect
 * ========================================================================= */

int danos_vpp_stat_connect(void)
{
    /* reset state but keep the configured socket path */
    g_stat.fd = -1;
    g_stat.mem_fd = -1;
    g_stat.base = NULL;
    g_stat.size = 0;
    g_stat.connected = false;

    int s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (s < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char *path = stat_sock_path();
    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(s);
        return -1;
    }
    memcpy(addr.sun_path, path, strlen(path) + 1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s);
        return -1;
    }
    g_stat.fd = s;

    /* Receive the segment fd via SCM_RIGHTS. The server sends it
     * unprompted immediately after accept(). */
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    char rx = 0;
    struct iovec iov = { .iov_base = &rx, .iov_len = 1 };
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct timeval tv = { .tv_sec = 5 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recvmsg(s, &msg, 0);
    if (n < 0) {
        close(s);
        g_stat.fd = -1;
        return -1;
    }

    int mem_fd = -1;
    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            memcpy(&mem_fd, CMSG_DATA(cmsg), sizeof(int));
            break;
        }
    }
    close(s);
    g_stat.fd = -1;
    if (mem_fd < 0) return -1;
    g_stat.mem_fd = mem_fd;

    struct stat st;
    if (fstat(mem_fd, &st) < 0 || st.st_size < (off_t)sizeof(stat_shared_header_t)) {
        close(mem_fd);
        g_stat.mem_fd = -1;
        return -1;
    }

    void *base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, mem_fd, 0);
    if (base == MAP_FAILED) {
        close(mem_fd);
        g_stat.mem_fd = -1;
        return -1;
    }

    g_stat.base = base;
    g_stat.size = (size_t)st.st_size;
    g_stat.connected = true;

    const stat_shared_header_t *hdr = base;
    if (hdr->version < 2 || hdr->version > 0xFFFF) {
        /* unsupported segment version */
        danos_vpp_stat_disconnect();
        return -1;
    }
    return 0;
}

void danos_vpp_stat_disconnect(void)
{
    if (g_stat.base) munmap(g_stat.base, g_stat.size);
    if (g_stat.mem_fd >= 0) close(g_stat.mem_fd);
    if (g_stat.fd >= 0) close(g_stat.fd);
    g_stat.base = NULL;
    g_stat.mem_fd = -1;
    g_stat.fd = -1;
    g_stat.connected = false;
}

/* =========================================================================
 * Directory walk + value read
 * ========================================================================= */

static const stat_dir_entry_t *dir_first(uint32_t *count)
{
    const stat_shared_header_t *hdr = g_stat.base;
    const stat_dir_entry_t *dir = seg_ptr(hdr->directory_offset, 0);
    if (!dir) return NULL;
    /* the directory is a clib vector: [u32 len][entries...] */
    uint32_t len;
    memcpy(&len, (const char *)dir - 4, 4);  /* header directly precedes data */
    if (len == 0 || len > STAT_DIR_MAX) return NULL;
    if (!seg_ptr(hdr->directory_offset + (uint64_t)len * sizeof(stat_dir_entry_t), 0))
        return NULL;
    *count = len;
    return dir;
}

static const char *entry_name(const stat_dir_entry_t *e)
{
    if (e->name == 0) return NULL;
    const char *n = seg_ptr(e->name, 1);
    if (!n) return NULL;
    /* bounds: name must be NUL-terminated within the segment */
    const char *end = (const char *)g_stat.base + g_stat.size;
    const char *p;
    for (p = n; p < end && (p - n) < 256; p++) {
        if (*p == '\0') return n;
    }
    return NULL;
}

/* Sum a simple counter across workers. offset_vector points to a
 * clib vector of per-worker byte offsets; each offset is a counter_t
 * array indexed by entry->value (index). */
static bool read_simple_counter(const stat_dir_entry_t *e, uint64_t *out)
{
    if (e->offset_vector == 0) return false;
    const char *vec = seg_ptr(e->offset_vector, 4);
    if (!vec) return false;
    uint32_t n;
    memcpy(&n, vec - 4, 4);
    if (n > 4096) return false;

    const uint64_t *offsets = (const uint64_t *)vec;
    if (!seg_ptr(e->offset_vector + (uint64_t)n * 8, 0)) return false;

    uint64_t idx = e->value;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (offsets[i] == 0) continue;
        const uint64_t *cnt = seg_ptr(offsets[i], (size_t)(idx + 1) * 8);
        if (!cnt) continue;
        sum += cnt[idx];
    }
    *out = sum;
    return true;
}

/* Read a counter by exact name. Returns false if not found or the
 * segment is torn (in_progress never settles). */
static bool stat_read(const char *name, uint64_t *out)
{
    if (!g_stat.connected || !g_stat.base) return false;
    const stat_shared_header_t *hdr = g_stat.base;

    for (int spin = 0; spin < 1000; spin++) {
        if (hdr->in_progress != 0) continue;  /* writer active: retry */
        uint64_t epoch = hdr->epoch;

        uint32_t count = 0;
        const stat_dir_entry_t *dir = dir_first(&count);
        bool found = false;
        if (dir) {
            for (uint32_t i = 0; i < count; i++) {
                const stat_dir_entry_t *e = &dir[i];
                if (e->type != STAT_DIR_TYPE_SCALAR &&
                    e->type != STAT_DIR_TYPE_SIMPLE_COUNTER &&
                    e->type != STAT_DIR_TYPE_COMBINED_COUNTER)
                    continue;
                const char *n = entry_name(e);
                if (!n || strcmp(n, name) != 0) continue;
                found = true;
                if (e->type == STAT_DIR_TYPE_SCALAR) {
                    *out = e->value;
                } else if (!read_simple_counter(e, out)) {
                    found = false;
                }
                break;
            }
        }

        /* verify the read was consistent (no writer touched the segment) */
        if (hdr->in_progress == 0 && hdr->epoch == epoch) {
            return found;
        }
    }
    return false;
}

uint64_t danos_vpp_stat_query_real(const char *name)
{
    uint64_t v = 0;
    if (!stat_read(name, &v)) return 0;
    return v;
}

int danos_vpp_stat_list_real(const char **names, uint64_t *values, int max_entries)
{
    if (!g_stat.connected || !g_stat.base) return -1;
    const stat_shared_header_t *hdr = g_stat.base;
    if (hdr->in_progress != 0) return -1;

    uint32_t count = 0;
    const stat_dir_entry_t *dir = dir_first(&count);
    if (!dir) return -1;

    int n = 0;
    for (uint32_t i = 0; i < count && n < max_entries; i++) {
        const stat_dir_entry_t *e = &dir[i];
        const char *nm = entry_name(e);
        if (!nm) continue;
        uint64_t v = 0;
        if (e->type == STAT_DIR_TYPE_SCALAR) {
            v = e->value;
        } else if (e->type == STAT_DIR_TYPE_SIMPLE_COUNTER ||
                   e->type == STAT_DIR_TYPE_COMBINED_COUNTER) {
            if (!read_simple_counter(e, &v)) continue;
        } else {
            continue;
        }
        if (names)  names[n] = nm;
        if (values) values[n] = v;
        n++;
    }
    return n;
}
