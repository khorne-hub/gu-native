// Crossplay with Ryujinx's LDN servers (ini [Multiplayer] Crossplay = Ryujinx, net.h): Ryujinx (and Ryubing) play local
// wireless games over the internet through an LDN server, which keeps each game's network and relays its packets. This
// file is a client of such a server, written from the protocol as those servers speak it (it shares no code with them).
// No server is set by default: the player types one ([Multiplayer] RyujinxServer). The official server's rules are the
// player's to read (the menu's help says what they are); a self-hosted server (the public ldn-server) works for both.
//
// The protocol: TCP (port 30456 unless the address says another), frames of a 12-byte header (u32 'RLDN', u8 type, u8
// version 1, 2 bytes, s32 size) and their payload; every number little endian; an IPv4 address in a u32 is the number
// (10.114.0.1 = 0x0A720001), in a u8[16] its bytes in network order. The structures are the SDK's (NetworkInfo 0x480,
// NodeInfo 0x40, SecurityConfig 0x44, UserConfig 0x30, ScanFilter 0x60), with the server's own:
//   0 initialize {u8 id[16], u8 mac[6]} both ways (the server answers our MAC); 1 passphrase {u8[128]} ("" or
//   "Ryujinx-" and 8 lower-case hex digits: private games, seen only with the same one)
//   2 create {SecurityConfig, UserConfig, NetworkConfig 0x20, game version u8[16] + 0x18 of P2P fields (0: none)} +
//   advertise data; 7 sync {NetworkInfo}; 8 reject {u32 node, u32 reason}; 10 scan {ScanFilter}; 11 scan reply
//   {NetworkInfo}; 12 scan end; 13 connect {SecurityConfig, UserConfig, u32 version, u32 option, 4 bytes, NetworkInfo}
//   (0x500); 15 connected {NetworkInfo}; 16 disconnect {u32}; 17 proxy config {u32 address, u32 mask}; 20 game data
//   {u32 source, u16 port, u32 destination, u16 port, s32 protocol (17 UDP), u32 size} + data; 22 accept policy {u8};
//   23 advertise data; 254 ping {u8 requester, u8 id} (echoed); 255 error {s32}
//   P2P (a Ryujinx host with UPnP): the server sends a joining player 4 {u8 ip[16], s32 family, u16 port, u8 token[16]}:
//   it connects to the host there, sends the same 4, and the host answers 17 and carries the game's packets.
// The server gives every game an address in 10.114.0.0/16 (the host .0.1); a broadcast goes to 10.114.255.255.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"
#include "../overlay.h"
#include "../platform.h"
#include "net.h"

#ifdef _WIN32
typedef SOCKET rsock;
#define RBAD INVALID_SOCKET
#define rclose closesocket
#else
typedef int rsock;
#define RBAD (-1)
#define rclose close
#endif

#define RYU_PORT 30456
#define MAGIC 0x4E444C52u  // "RLDN"
#define HDR 12
#define FRAME_MAX 131072
#define NETINFO_SIZE 0x480
#define NODE_SIZE 0x40
#define NI_ACCEPT_POLICY 0x62
#define NI_NODE_COUNT 0x67
#define NI_NODES 0x68
#define NI_ADV_SIZE 0x26A
#define NI_ADV 0x26C
#define NODE_LCV 0x2E
#define NODE_NAME 0x0C
#define MAX_FOUND 16
enum { P_INIT = 0, P_PASSPHRASE, P_CREATE, P_CREATE_PRIVATE, P_EXT_PROXY, P_EXT_TOKEN, P_EXT_STATE, P_SYNC, P_REJECT,
       P_REJECT_REPLY, P_SCAN, P_SCAN_REPLY, P_SCAN_END, P_CONNECT, P_CONNECT_PRIVATE, P_CONNECTED, P_DISCONNECT,
       P_PROXY_CONFIG, P_PROXY_CONNECT, P_PROXY_CONNECT_REPLY, P_PROXY_DATA, P_PROXY_DISCONNECT, P_ACCEPT_POLICY,
       P_ADVERTISE, P_PING = 254, P_ERROR = 255 };
enum { E_NONE, E_PORT, E_TOO_MANY, E_TOO_LOW, E_TOO_HIGH, E_FAILURE, E_NOT_FOUND, E_TIMEOUT, E_REJECTED, E_REJECT_FAILED };
#define LDN_RESULT(d) ((203u) | ((uint32_t)(d) << 9))
#define R_INVALID_STATE LDN_RESULT(32)
#define R_CONNECT_FAILURE LDN_RESULT(64)
#define R_CONNECT_NOT_FOUND LDN_RESULT(65)
#define R_CONNECT_TIMEOUT LDN_RESULT(66)
#define R_CONNECT_REJECTED LDN_RESULT(67)
#define R_VERSION_TOO_LOW LDN_RESULT(113)
#define R_VERSION_TOO_HIGH LDN_RESULT(114)
#define R_TOO_MANY_PLAYERS LDN_RESULT(144)
enum { DR_DESTROYED_BY_USER = 3, DR_SIGNAL_LOST = 6 };

static const char *xt(const char *en, const char *es) { return config_language() == 1 ? es : en; }
static void w16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void w32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint16_t r16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t r32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t r64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

typedef struct {
    rsock s;
    pthread_mutex_t wmu;
    pthread_t th;
    bool has_thread;
} Conn;

static struct {
    pthread_mutex_t mu;
    int state;  // XS_*
    char error[200];
    char server[256];      // as typed (and connected to)
    char passphrase[64];
    Conn master, p2p;      // the server; a Ryujinx host's own proxy (P2P), when it gave one
    volatile bool run;
    uint32_t gen;
    uint8_t id[16], mac[6];
    bool initialized;
    uint32_t ip, mask;     // ours in the game's network (from proxy config)
    // the network
    bool hosting, joined;
    uint8_t info[NETINFO_SIZE];
    uint8_t adv[0x180];
    uint32_t adv_size;
    int policy;
    // answers waited for
    bool connected_seen;   // a "connected" came (create / connect)
    int error_code;        // the last error message (-1 none)
    bool scanning, scan_done;
    uint8_t found[MAX_FOUND][NETINFO_SIZE];
    int nfound;
} R = {.mu = PTHREAD_MUTEX_INITIALIZER, .master = {.s = RBAD, .wmu = PTHREAD_MUTEX_INITIALIZER},
       .p2p = {.s = RBAD, .wmu = PTHREAD_MUTEX_INITIALIZER}, .error_code = -1};
static uint8_t g_saved_id[16];  // (the server's id for us, for this run's reconnections: as Ryujinx keeps it)

static void net_init(void) {
#ifdef _WIN32
    static int done;
    if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = 1; }
#endif
}

static bool send_all(rsock s, const void *b, size_t n) {
    const char *p = b;
    while (n) {
        int k = send(s, p, (int)n, 0);
        if (k <= 0) return false;
        p += k, n -= (size_t)k;
    }
    return true;
}
static bool recv_all(rsock s, void *b, size_t n) {
    char *p = b;
    while (n) {
        int k = recv(s, p, (int)n, 0);
        if (k <= 0) return false;
        p += k, n -= (size_t)k;
    }
    return true;
}
static bool frame_send(Conn *c, uint8_t type, const void *a, uint32_t alen, const void *b, uint32_t blen) {
    uint8_t h[HDR] = {0};
    w32(h, MAGIC);
    h[4] = type, h[5] = 1;
    w32(h + 8, alen + blen);
    pthread_mutex_lock(&c->wmu);
    bool ok = c->s != RBAD && send_all(c->s, h, HDR) && (!alen || send_all(c->s, a, alen)) && (!blen || send_all(c->s, b, blen));
    pthread_mutex_unlock(&c->wmu);
    return ok;
}
// A frame into buf (FRAME_MAX): its size, -1 on a closed or broken stream
static int frame_recv(rsock s, uint8_t *type, uint8_t *buf) {
    uint8_t h[HDR];
    if (!recv_all(s, h, HDR) || r32(h) != MAGIC || h[5] != 1) return -1;
    int32_t n = (int32_t)r32(h + 8);
    if (n < 0 || n > FRAME_MAX - HDR) return -1;
    *type = h[4];
    return n && !recv_all(s, buf, (size_t)n) ? -1 : n;
}

static rsock tcp_connect(const char *host, uint16_t port, int timeout_ms, const uint8_t *ip_bytes) {
    net_init();
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (ip_bytes) memcpy(&a.sin_addr, ip_bytes, 4);
    else {
        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, NULL, &hints, &res) || !res) return RBAD;
        a.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    rsock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == RBAD) return RBAD;
    // (a connect with a timeout: non-blocking, then select)
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
    connect(s, (struct sockaddr *)&a, sizeof a);
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    int err = 0;
    socklen_t el = sizeof err;
    if (select((int)s + 1, NULL, &w, NULL, &tv) <= 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el) || err) {
        rclose(s);
        return RBAD;
    }
#ifdef _WIN32
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fl);
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    return s;
}

static bool parse_server(const char *text, char *host, size_t size, uint16_t *port) {
    while (*text == ' ') text++;
    snprintf(host, size, "%s", text);
    size_t n = strlen(host);
    while (n && host[n - 1] == ' ') host[--n] = 0;
    *port = RYU_PORT;
    char *colon = strrchr(host, ':');
    if (colon) {
        int p = atoi(colon + 1);
        if (p < 1 || p > 65535) return false;
        *port = (uint16_t)p;
        *colon = 0;
    }
    return *host;
}

static const char *error_text(int code) {
    switch (code) {
    case E_TOO_MANY: return xt("That hub is full.", "Esa sala está llena.");
    case E_TOO_LOW:
    case E_TOO_HIGH:
        return xt("That hub's game is another version: only " XPLAY_GAME_VERSION " plays with this app. They can update the "
                  "game to " XPLAY_GAME_VERSION " in Ryujinx.",
                  "El juego de esa sala es otra versión: solo la " XPLAY_GAME_VERSION " juega con esta app. Puede actualizar "
                  "el juego a la " XPLAY_GAME_VERSION " en Ryujinx.");
    case E_NOT_FOUND: return xt("That hub is gone.", "Esa sala ya no está.");
    case E_REJECTED: return xt("The hub's host turned you away.", "El anfitrión de la sala no te dejó entrar.");
    default: return xt("The server refused the request.", "El servidor rechazó la petición.");
    }
}

static void fail_locked(const char *why) {
    R.state = XS_FAILED;
    snprintf(R.error, sizeof R.error, "%s", why);
}

// A game packet from the server or a host's proxy
static void on_data(const uint8_t *b, int n) {
    if (n < 0x14) return;
    uint32_t src = r32(b), dst = r32(b + 6);
    uint16_t sport = r16(b + 4), dport = r16(b + 10);
    int32_t proto = (int32_t)r32(b + 12);
    if (proto != 17 || src == R.ip) return;  // (UDP only; our own broadcast comes back from the server: already ours)
    uint32_t mask = R.mask ? R.mask : 0xFFFF0000u;
    bool bcast = (dst | mask) == 0xFFFFFFFFu || dst == 0xFFFFFFFFu || dst == 0xC0A800FFu;
    VPacket p = {src, bcast ? 0xFFFFFFFFu : dst, sport, dport, VPROTO_UDP, 0, 0};
    ldn_x_packet(&p, b + 0x14, (uint32_t)(n - 0x14));
}

static void p2p_open(const uint8_t *cfg, int n);

static void *conn_thread(void *arg) {
    Conn *c = arg;
    bool master = c == &R.master;
    plat_thread_name(master ? "ryuldn" : "ryuldn-p2p");
    uint8_t *buf = malloc(FRAME_MAX);
    uint8_t type;
    int n;
    while (buf && R.run && (n = frame_recv(c->s, &type, buf)) >= 0) {
        switch (type) {
        case P_INIT:
            if (n >= 0x16) {
                pthread_mutex_lock(&R.mu);
                memcpy(R.id, buf, 16), memcpy(g_saved_id, buf, 16), memcpy(R.mac, buf + 16, 6);
                R.initialized = true;
                pthread_mutex_unlock(&R.mu);
            }
            break;
        case P_PING:
            if (n >= 2 && buf[0] == 0) frame_send(c, P_PING, buf, 2, NULL, 0);
            break;
        case P_PROXY_CONFIG:
            if (n >= 8) {
                pthread_mutex_lock(&R.mu);
                R.ip = r32(buf), R.mask = r32(buf + 4);
                pthread_mutex_unlock(&R.mu);
            }
            break;
        case P_CONNECTED:
        case P_SYNC:
            if (n >= NETINFO_SIZE) {
                pthread_mutex_lock(&R.mu);
                memcpy(R.info, buf, NETINFO_SIZE);
                if (type == P_CONNECTED) R.connected_seen = true;
                bool up = R.hosting || R.joined;
                pthread_mutex_unlock(&R.mu);
                if (up) ldn_x_info(buf);
            }
            break;
        case P_SCAN_REPLY:
            pthread_mutex_lock(&R.mu);
            if (R.scanning && n >= NETINFO_SIZE && R.nfound < MAX_FOUND) memcpy(R.found[R.nfound++], buf, NETINFO_SIZE);
            pthread_mutex_unlock(&R.mu);
            break;
        case P_SCAN_END:
            R.scan_done = true;
            break;
        case P_ERROR:
            if (n >= 4) {
                R.error_code = (int)r32(buf);
                LOG("[ryuldn] the server says: error %d\n", R.error_code);
            }
            break;
        case P_DISCONNECT: {  // (the hub we are in ended: its host left)
            pthread_mutex_lock(&R.mu);
            bool was = R.joined;
            R.joined = false;
            pthread_mutex_unlock(&R.mu);
            if (was) {
                LOG("[ryuldn] the hub's host ended it\n");
                ldn_x_lost(DR_DESTROYED_BY_USER);
            }
            break;
        }
        case P_EXT_PROXY:
            if (master) p2p_open(buf, n);
            break;
        case P_PROXY_DATA: on_data(buf, n); break;
        default: break;  // (TCP over the proxy: the game plays over UDP only)
        }
    }
    free(buf);
    if (R.run && master) {
        LOG("[ryuldn] the server closed the connection\n");
        pthread_mutex_lock(&R.mu);
        fail_locked(xt("Lost the connection to the Ryujinx server.", "Se perdió la conexión con el servidor de Ryujinx."));
        bool was = R.joined;
        R.joined = R.hosting = false;
        pthread_mutex_unlock(&R.mu);
        if (was) ldn_x_lost(DR_SIGNAL_LOST);
    }
    return NULL;
}

// A Ryujinx host's own proxy (P2P): the server gave its address and a token
static void p2p_open(const uint8_t *cfg, int n) {
    if (n < 0x26) return;
    int32_t family = (int32_t)r32(cfg + 0x10);
    uint16_t port = r16(cfg + 0x14);
    if (family != 2) { LOG("[ryuldn] the host's proxy is IPv6 only: not supported\n"); return; }
    rsock s = tcp_connect(NULL, port, 3000, cfg);
    if (s == RBAD) {
        LOG("[ryuldn] can't reach the host's proxy at %u.%u.%u.%u:%u\n", cfg[0], cfg[1], cfg[2], cfg[3], port);
        return;
    }
    if (R.p2p.s != RBAD) rclose(R.p2p.s);
    R.p2p.s = s;
    frame_send(&R.p2p, P_EXT_PROXY, cfg, 0x26, NULL, 0);
    R.p2p.has_thread = pthread_create(&R.p2p.th, NULL, conn_thread, &R.p2p) == 0;
    LOG("[ryuldn] through the host's own proxy (P2P) at %u.%u.%u.%u:%u\n", cfg[0], cfg[1], cfg[2], cfg[3], port);
}

static void close_conn(Conn *c) {
    pthread_mutex_lock(&c->wmu);
    if (c->s != RBAD) {
#ifdef _WIN32
        shutdown(c->s, SD_BOTH);
#else
        shutdown(c->s, SHUT_RDWR);
#endif
        rclose(c->s);
        c->s = RBAD;
    }
    pthread_mutex_unlock(&c->wmu);
    if (c->has_thread) pthread_join(c->th, NULL), c->has_thread = false;
}

void ryuldn_leave(void) {
    R.run = false;
    close_conn(&R.p2p);
    close_conn(&R.master);
    pthread_mutex_lock(&R.mu);
    R.state = XS_OFF;
    R.hosting = R.joined = false;
    R.ip = 0;
    pthread_mutex_unlock(&R.mu);
}

// The connection's own thread: connect, passphrase, initialize, then read (conn_thread)
static void *start_thread(void *arg) {
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    char host[256];
    uint16_t port;
    pthread_mutex_lock(&R.mu);
    bool ok = parse_server(R.server, host, sizeof host, &port);
    char pass[64];
    snprintf(pass, sizeof pass, "%s", R.passphrase);
    pthread_mutex_unlock(&R.mu);
    rsock s = ok ? tcp_connect(host, port, 5000, NULL) : RBAD;
    if (s == RBAD) {
        LOG("[ryuldn] can't reach the server %s:%u\n", host, port);
        pthread_mutex_lock(&R.mu);
        if (gen == R.gen) fail_locked(xt("Can't reach the Ryujinx server: check its address, and that it is up.",
                                         "No se puede conectar con el servidor de Ryujinx: revisa su dirección, y que esté "
                                         "funcionando."));
        pthread_mutex_unlock(&R.mu);
        return NULL;
    }
    R.master.s = s;
    uint8_t pp[128] = {0}, init[0x16] = {0};
    // ("Ryujinx-" and 8 lower-case hex digits: the servers take that or nothing; anything else is public there)
    for (size_t i = 0; pass[i] && i < 127; i++) pp[i] = (uint8_t)(i >= 8 ? tolower((unsigned char)pass[i]) : pass[i]);
    memcpy(init, g_saved_id, 16);
    frame_send(&R.master, P_PASSPHRASE, pp, sizeof pp, NULL, 0);
    frame_send(&R.master, P_INIT, init, sizeof init, NULL, 0);
    pthread_mutex_lock(&R.mu);
    R.state = XS_IN;
    pthread_mutex_unlock(&R.mu);
    LOG("[ryuldn] connected to the Ryujinx LDN server %s:%u%s\n", host, port, *pass ? " (private games)" : "");
    conn_thread(&R.master);
    return NULL;
}

static bool rl_ready(int wait_ms) {
    XplayLive live;
    config_live_crossplay(&live);
    pthread_mutex_lock(&R.mu);
    char host[256];
    uint16_t port;
    if (!parse_server(live.ryu_server, host, sizeof host, &port)) {
        pthread_mutex_unlock(&R.mu);
        ryuldn_leave();
        pthread_mutex_lock(&R.mu);
        fail_locked(xt("No Ryujinx server set: type one under Multiplayer (the one the Ryujinx players use).",
                       "No hay servidor de Ryujinx: escribe uno en Multijugador (el que usan los jugadores de Ryujinx)."));
        pthread_mutex_unlock(&R.mu);
        return false;
    }
    bool same = !strcmp(live.ryu_server, R.server) && !strcmp(live.ryu_passphrase, R.passphrase);
    if (!same || R.state == XS_FAILED || R.state == XS_OFF) {
        pthread_mutex_unlock(&R.mu);
        ryuldn_leave();
        pthread_mutex_lock(&R.mu);
        snprintf(R.server, sizeof R.server, "%s", live.ryu_server);
        snprintf(R.passphrase, sizeof R.passphrase, "%s", live.ryu_passphrase);
        R.state = XS_CONNECTING;
        R.error[0] = 0;
        R.initialized = false;
        R.run = true;
        R.gen++;
        R.master.has_thread = pthread_create(&R.master.th, NULL, start_thread, (void *)(uintptr_t)R.gen) == 0;
        if (!R.master.has_thread) fail_locked(xt("The network could not start.", "La red no pudo iniciarse."));
    }
    uint64_t end = plat_now_ns() + (uint64_t)wait_ms * 1000000ull;
    // (in: connected, and the server's answer to initialize came - it gives our MAC address)
    while ((R.state == XS_CONNECTING || (R.state == XS_IN && !R.initialized)) && plat_now_ns() < end) {
        pthread_mutex_unlock(&R.mu);
        plat_sleep_until_ns(plat_now_ns() + 20000000ull);
        pthread_mutex_lock(&R.mu);
    }
    bool in = R.state == XS_IN && R.initialized;
    pthread_mutex_unlock(&R.mu);
    return in;
}

static uint32_t rl_address(void) { return R.ip; }
static uint32_t rl_netmask(void) { return R.mask ? R.mask : 0xFFFF0000u; }
static void rl_mac(uint8_t out[6]) { memcpy(out, R.mac, 6); }

// Waits for a "connected" (true) or an error (false, *code) for up to ms
static bool wait_connected(int ms, int *code) {
    for (uint64_t end = plat_now_ns() + (uint64_t)ms * 1000000ull; plat_now_ns() < end;) {
        if (R.connected_seen && R.ip) return true;
        if (R.error_code >= 0 && R.error_code != E_PORT) { *code = R.error_code; return false; }
        plat_sleep_until_ns(plat_now_ns() + 10000000ull);
    }
    *code = E_TIMEOUT;
    return false;
}
static uint32_t result_of(int code) {
    switch (code) {
    case E_TOO_MANY: return R_TOO_MANY_PLAYERS;
    case E_TOO_LOW: return R_VERSION_TOO_LOW;
    case E_TOO_HIGH: return R_VERSION_TOO_HIGH;
    case E_NOT_FOUND: return R_CONNECT_NOT_FOUND;
    case E_TIMEOUT: return R_CONNECT_TIMEOUT;
    case E_REJECTED: return R_CONNECT_REJECTED;
    default: return R_CONNECT_FAILURE;
    }
}

// The game's "this program" (-1) as the program's own id: what a Ryujinx server lists (Ryujinx does the same)
static void own_id(uint8_t *p) {
    if (r64(p) == ~0ull) {
        uint64_t id = XPLAY_TITLE_ID;
        memcpy(p, &id, 8);
    }
}

static uint32_t rl_create(uint8_t *info, const uint8_t *sec, const uint8_t *user) {
    if (R.state != XS_IN) return R_INVALID_STATE;
    uint8_t req[0xBC] = {0};
    memcpy(req, sec, 0x44);
    w16(req, 1);  // (the security mode as Ryujinx sends it: retail)
    memcpy(req + 0x44, user, 0x30);
    uint8_t *cfg = req + 0x74;  // NetworkConfig: intent id, channel 0, node count max, -, local communication version
    memcpy(cfg, info, 0x10);
    own_id(cfg);
    cfg[0x12] = info[0x66];
    w16(cfg + 0x14, r16(info + NI_NODES + NODE_LCV));
    memcpy(req + 0x94, XPLAY_GAME_VERSION, sizeof XPLAY_GAME_VERSION - 1);  // (the P2P fields: 0, the server relays)
    uint32_t adv = r16(info + NI_ADV_SIZE);
    if (adv > 0x180) adv = 0x180;
    pthread_mutex_lock(&R.mu);
    R.connected_seen = false, R.error_code = -1, R.ip = 0;
    R.hosting = true, R.joined = false;
    memcpy(R.adv, info + NI_ADV, adv);
    R.adv_size = adv;
    R.policy = info[NI_ACCEPT_POLICY];
    pthread_mutex_unlock(&R.mu);
    frame_send(&R.master, P_CREATE, req, sizeof req, info + NI_ADV, adv);
    int code = 0;
    if (!wait_connected(4000, &code)) {
        R.hosting = false;
        LOG("[ryuldn] the server didn't make the hub (error %d)\n", code);
        overlay_notice(xt("Crossplay: the Ryujinx server didn't make the hub", "Juego cruzado: el servidor de Ryujinx no creó la sala"),
                       error_text(code));
        return R_INVALID_STATE;
    }
    pthread_mutex_lock(&R.mu);
    memcpy(info, R.info, NETINFO_SIZE);
    pthread_mutex_unlock(&R.mu);
    LOG("[ryuldn] hosting a hub on the server as %u.%u.%u.%u\n", R.ip >> 24, (R.ip >> 16) & 255, (R.ip >> 8) & 255, R.ip & 255);
    return 0;
}

static void rl_update(const uint8_t *info) {
    pthread_mutex_lock(&R.mu);
    if (!R.hosting) { pthread_mutex_unlock(&R.mu); return; }
    uint32_t adv = r16(info + NI_ADV_SIZE);
    if (adv > 0x180) adv = 0x180;
    bool adv_changed = adv != R.adv_size || memcmp(R.adv, info + NI_ADV, adv);
    bool policy_changed = info[NI_ACCEPT_POLICY] != R.policy;
    if (adv_changed) memcpy(R.adv, info + NI_ADV, adv), R.adv_size = adv;
    R.policy = info[NI_ACCEPT_POLICY];
    uint8_t policy = (uint8_t)R.policy;
    pthread_mutex_unlock(&R.mu);
    if (adv_changed) frame_send(&R.master, P_ADVERTISE, info + NI_ADV, adv, NULL, 0);
    if (policy_changed) frame_send(&R.master, P_ACCEPT_POLICY, &policy, 1, NULL, 0);
}

static void rl_destroy(void) {
    uint8_t zero[4] = {0};
    if (R.hosting) frame_send(&R.master, P_DISCONNECT, zero, 4, NULL, 0);
    R.hosting = false;
}

static void rl_reject(uint32_t ip) {
    uint8_t req[8];
    pthread_mutex_lock(&R.mu);
    for (int i = 1; i < 8; i++)
        if (r32(R.info + NI_NODES + i * NODE_SIZE) == ip) {
            w32(req, (uint32_t)i), w32(req + 4, 5);
            pthread_mutex_unlock(&R.mu);
            frame_send(&R.master, P_REJECT, req, 8, NULL, 0);
            return;
        }
    pthread_mutex_unlock(&R.mu);
}

static int rl_scan(const uint8_t *filter, uint8_t (*infos)[NETINFO_SIZE], int max) {
    if (R.state != XS_IN) return 0;
    uint8_t f[0x60];
    memcpy(f, filter, 0x60);
    own_id(f);
    f[0x5D] = f[0x5E] = f[0x5F] = 0;
    pthread_mutex_lock(&R.mu);
    R.nfound = 0;
    R.scanning = true, R.scan_done = false;
    pthread_mutex_unlock(&R.mu);
    frame_send(&R.master, P_SCAN, f, sizeof f, NULL, 0);
    for (uint64_t end = plat_now_ns() + 1500000000ull; !R.scan_done && plat_now_ns() < end;)
        plat_sleep_until_ns(plat_now_ns() + 10000000ull);
    pthread_mutex_lock(&R.mu);
    R.scanning = false;
    int n = R.nfound < max ? R.nfound : max;
    memcpy(infos, R.found, (size_t)n * NETINFO_SIZE);
    pthread_mutex_unlock(&R.mu);
    return n;
}

static uint32_t rl_connect(const uint8_t *info, const uint8_t *node, uint8_t *out) {
    if (R.state != XS_IN) return R_CONNECT_FAILURE;
    uint8_t req[0x500] = {0};
    w16(req, 1);  // SecurityConfig: retail, no passphrase
    memcpy(req + 0x44, node + NODE_NAME, 0x21);  // UserConfig: the name
    w32(req + 0x74, r16(node + NODE_LCV));
    memcpy(req + 0x80, info, NETINFO_SIZE);
    pthread_mutex_lock(&R.mu);
    R.connected_seen = false, R.error_code = -1, R.ip = 0;
    R.joined = true, R.hosting = false;
    pthread_mutex_unlock(&R.mu);
    frame_send(&R.master, P_CONNECT, req, sizeof req, NULL, 0);
    int code = 0;
    if (!wait_connected(5000, &code)) {
        R.joined = false;
        LOG("[ryuldn] couldn't join the hub (error %d)\n", code);
        overlay_notice(xt("Crossplay: couldn't join the hub", "Juego cruzado: no se pudo entrar a la sala"), error_text(code));
        return result_of(code);
    }
    pthread_mutex_lock(&R.mu);
    memcpy(out, R.info, NETINFO_SIZE);
    pthread_mutex_unlock(&R.mu);
    LOG("[ryuldn] joined a hub as %u.%u.%u.%u\n", R.ip >> 24, (R.ip >> 16) & 255, (R.ip >> 8) & 255, R.ip & 255);
    return 0;
}

static void rl_disconnect(void) {
    uint8_t zero[4] = {0};
    if (R.joined) frame_send(&R.master, P_DISCONNECT, zero, 4, NULL, 0);
    R.joined = false;
    close_conn(&R.p2p);
}

static void rl_send(const VPacket *p, const void *data, uint32_t len) {
    if (p->proto != VPROTO_UDP || !R.ip || len > FRAME_MAX - HDR - 0x14) return;
    uint8_t h[0x14];
    uint32_t mask = rl_netmask();
    // (a broadcast of the game's goes to the network's broadcast address, which the server sends to everyone)
    uint32_t dst = p->dst_ip == 0xFFFFFFFFu ? (R.ip & mask) | ~mask : p->dst_ip;
    w32(h, R.ip), w16(h + 4, p->src_port), w32(h + 6, dst), w16(h + 10, p->dst_port), w32(h + 12, 17), w32(h + 16, len);
    Conn *c = R.p2p.s != RBAD ? &R.p2p : &R.master;
    frame_send(c, P_PROXY_DATA, h, sizeof h, data, len);
}

const XBackend g_ryuldn = {"Ryujinx LDN", rl_ready, rl_address, rl_netmask, rl_mac, rl_create, rl_update, rl_destroy,
                           rl_reject, rl_scan, rl_connect, rl_disconnect, rl_send};

void ryuldn_status(XStatus *o) {
    memset(o, 0, sizeof *o);
    pthread_mutex_lock(&R.mu);
    o->state = R.state;
    snprintf(o->error, sizeof o->error, "%s", R.error);
    snprintf(o->room, sizeof o->room, "%s", R.server);
    o->members = o->slots = -1;
    pthread_mutex_unlock(&R.mu);
}
