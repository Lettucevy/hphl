/* M_RV1 G7: socket API cross-platform - winsock2.h already included via runtime.h before windows.h */
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int wsa_init = 0;
static void ensure_wsa(void) {
#ifdef _WIN32
    if (!wsa_init) {
        WSADATA d; WSAStartup(MAKEWORD(2,2), &d);
        wsa_init = 1;
    }
#endif
}

int64_t hphl_socket(int64_t af, int64_t type, int64_t proto) {
    ensure_wsa();
    int s = socket((int)af, (int)type, (int)proto);
    return s < 0 ? -1 : s;
}
int64_t hphl_connect(int64_t s, const char* host, int64_t port) {
    if (!host) return -1;
    ensure_wsa();
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((int)port);
    // try inet_pton first, then gethostbyname
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(host);
        if (!he) return -1;
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    return connect((int)s, (struct sockaddr*)&addr, sizeof(addr)) == 0 ? 0 : -1;
}
/* M_RV1 NET: bind em 0.0.0.0:porta com SO_REUSEADDR (sem ele, restart
 * rápido do servidor falha com "address in use"). Devolve 0 ok / -1 erro. */
int64_t hphl_bind(int64_t s, int64_t port) {
    if (port <= 0 || port > 65535) return -1;
    ensure_wsa();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((int)port);
#ifdef _WIN32
    {
        BOOL re = 1;
        setsockopt((SOCKET)s, SOL_SOCKET, SO_REUSEADDR, (const char*)&re,
                   sizeof(re));
    }
    if (bind((SOCKET)s, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;
#else
    {
        int re = 1;
        setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(re));
    }
    if (bind((int)s, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;
#endif
    return 0;
}
int64_t hphl_listen(int64_t s, int64_t backlog) {
    return listen((int)s, (int)backlog) == 0 ? 0 : -1;
}
int64_t hphl_accept(int64_t s) {
    int c = accept((int)s, NULL, NULL);
    return c < 0 ? -1 : c;
}
int64_t hphl_send(int64_t s, const char* buf) {
    if (!buf) return -1;
#ifdef _WIN32
    return send((SOCKET)s, buf, (int)strlen(buf), 0);
#else
    return send((int)s, buf, strlen(buf), 0);
#endif
}
char* hphl_recv(int64_t s, int64_t len) {
    if (len <= 0) len = 4096;
    char* out = (char*)hphl_str_alloc((size_t)len + 1);
    if (!out) return NULL;
#ifdef _WIN32
    int n = recv((SOCKET)s, out, (int)len, 0);
#else
    ssize_t n = recv((int)s, out, (size_t)len, 0);
#endif
    if (n < 0) n = 0;
    out[n] = 0;
    return out;
}
int64_t hphl_close_socket(int64_t s) {
#ifdef _WIN32
    return closesocket((SOCKET)s) == 0 ? 0 : -1;
#else
    return close((int)s) == 0 ? 0 : -1;
#endif
}
