/*
 * DANOS-Open Observability: /metrics HTTP server (v0.6)
 *
 * Minimal HTTP/1.1 server: accepts GET (any path), answers 200 with
 * the Prometheus text exposition body. One accept loop on a detached
 * thread; per-connection handling is inline (metrics scraping is
 * low-rate).
 */

#include <danos/observability/prometheus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static struct {
    int listen_fd;
    uint16_t port;
    bool running;
} g_prom_srv;

static void *prom_conn(void *arg)
{
    int fd = (int)(long)arg;

    char req[1024];
    ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(fd);
        return NULL;
    }
    req[n] = '\0';

    if (strstr(req, "/metrics")) {
        char body[16384];
        int blen = danos_prom_render(body, sizeof(body) - 1);
        body[blen] = '\0';

        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/plain; version=0.0.4\r\n"
                            "Content-Length: %d\r\n"
                            "Connection: close\r\n\r\n", blen);
        send(fd, hdr, hlen, 0);
        if (blen > 0) send(fd, body, blen, 0);
    } else {
        const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                         "Connection: close\r\n\r\n";
        send(fd, nf, strlen(nf), 0);
    }
    close(fd);
    return NULL;
}

static void *prom_accept_loop(void *arg)
{
    (void)arg;
    while (g_prom_srv.running) {
        int fd = accept(g_prom_srv.listen_fd, NULL, NULL);
        if (fd < 0) break;
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, prom_conn, (void *)(long)fd) != 0)
            close(fd);
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

int danos_prom_start_server(uint16_t port)
{
    if (g_prom_srv.running) return 0;
    signal(SIGPIPE, SIG_IGN);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    g_prom_srv.listen_fd = fd;
    g_prom_srv.port = port;
    g_prom_srv.running = true;

    pthread_t tid;
    if (pthread_create(&tid, NULL, prom_accept_loop, NULL) != 0) {
        close(fd);
        g_prom_srv.running = false;
        return -1;
    }
    pthread_detach(tid);
    return 0;
}

void danos_prom_stop_server(void)
{
    if (!g_prom_srv.running) return;
    g_prom_srv.running = false;
    shutdown(g_prom_srv.listen_fd, SHUT_RDWR);
    close(g_prom_srv.listen_fd);
    g_prom_srv.listen_fd = -1;
}
