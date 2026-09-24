/* Minimal FRR 10.3 zclient wire probe; deliberately has no DANOS/VPP deps. */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int send_msg(int fd, uint16_t cmd, const void *payload, size_t n)
{
    uint8_t b[64] = {0}; uint16_t len = htons((uint16_t)(10 + n));
    memcpy(b, &len, 2); b[2] = 254; b[3] = 6; uint16_t c = htons(cmd);
    memcpy(b + 8, &c, 2); if (n) memcpy(b + 10, payload, n);
    return send(fd, b, 10 + n, MSG_NOSIGNAL) == (ssize_t)(10 + n) ? 0 : -1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *ep = argc > 1 ? argv[1] : "tcp://127.0.0.1:2600";
    int fd = -1;
    if (!strncmp(ep, "tcp://", 6)) {
        char host[128], *colon; unsigned long port;
        snprintf(host, sizeof(host), "%s", ep + 6); colon = strrchr(host, ':');
        if (!colon) return 2;
        *colon++ = 0; port = strtoul(colon, NULL, 10);
        char service[16]; snprintf(service, sizeof(service), "%lu", port);
        struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *res = NULL;
        if (getaddrinfo(host, service, &hints, &res) != 0) return 3;
        for (struct addrinfo *p = res; p; p = p->ai_next) {
            fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
            if (fd >= 0) close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
    }
    if (fd < 0) { fprintf(stderr, "probe connect failed: %s\n", ep); return 1; }
    uint8_t hello[8] = {3, 0, 0, 0, 0, 0, 0, 0};
    if (send_msg(fd, 19, hello, sizeof(hello)) || send_msg(fd, 16, "\0\1", 2) ||
        send_msg(fd, 0, NULL, 0)) return 1;
    const char *route_type = getenv("DANOS_PROBE_ROUTE_TYPE");
    if (route_type && route_type[0]) {
        uint8_t redist[4] = {1, (uint8_t)strtoul(route_type, NULL, 10), 0, 0};
        if (send_msg(fd, 12, redist, sizeof(redist))) return 1;
        fprintf(stderr, "probe redistribution sent: type=%u\n", redist[1]);
        fprintf(stderr, "probe tx frame: 00 0e fe 06 00 00 00 00 00 0c %02x %02x 00 00\n",
                redist[0], redist[1]);
    }
    fprintf(stderr, "probe registration sent: %s\n", ep);
    uint8_t h[10];
    for (;;) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int r = poll(&p, 1, 5000); if (r == 0) { puts("probe alive"); continue; }
        if (r < 0) {
            fprintf(stderr, "probe poll error errno=%d (%s)\n", errno, strerror(errno));
            break;
        }
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            fprintf(stderr, "probe poll events=0x%x\n", p.revents);
            break;
        }
        ssize_t n = recv(fd, h, sizeof(h), MSG_WAITALL);
        if (n == 0) {
            fprintf(stderr, "probe EOF from peer\n");
            break;
        }
        if (n != 10) {
            fprintf(stderr, "probe short header=%zd errno=%d (%s)\n", n, errno, strerror(errno));
            break;
        }
        uint16_t len; memcpy(&len, h, 2); len = ntohs(len);
        if (len < 10) { fprintf(stderr, "probe invalid length=%u\n", len); break; }
        uint8_t *body = malloc(len - 10); if (len > 10 && recv(fd, body, len - 10, MSG_WAITALL) != (ssize_t)(len - 10)) { fprintf(stderr, "probe short body command=%u\n", ntohs(*(uint16_t *)(h + 8))); free(body); break; }
        printf("probe rx command=%u length=%u\n", ntohs(*(uint16_t *)(h + 8)), len); free(body);
    }
    fprintf(stderr, "probe EOF/connection error errno=%d (%s)\n", errno, strerror(errno)); close(fd); return 1;
}
