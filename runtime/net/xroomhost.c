// A room of the yuzu family's kind hosted by this app (the menu's Room: "This device", ini [Multiplayer] RoomHost), so
// players of Eden, Citron, Sudachi, yuzu... and of this app join it at this device's address, as they would join a room
// made with an emulator's Create Room. Written from the protocol (xroom.c has it), sharing no code with the emulators:
// the join's checks (full, password, nickname, address, protocol version), an address for each member (192.168.x.y),
// the room's information to everyone when it changes, and each member's game datagrams, local wireless messages and
// chat passed on as they came (to the destination's member, or to everyone else). This app joins it too, at 127.0.0.1.
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <enet/enet.h>

#include "../config.h"
#include "../hle.h"
#include "../platform.h"
#include "net.h"

enum { M_JOIN = 1, M_JOINED, M_ROOM_INFO, M_GAME_INFO, M_PROXY, M_LDN, M_CHAT, M_NAME_TAKEN, M_IP_TAKEN, M_VERSION,
       M_PASSWORD, M_CLOSED, M_FULL, M_STATUS };
enum { ST_JOINED = 1, ST_LEFT };
#define MAX_PEERS 32

typedef struct {
    ENetPeer *peer;
    bool in;  // joined (a connection that hasn't asked yet is not a member)
    char nick[24], game[96], version[40];
    uint64_t game_id;
    uint32_t ip;
} Member;

static struct {
    pthread_mutex_t mu;
    pthread_t th;
    bool has_thread;
    volatile bool run;
    ENetHost *host;
    uint16_t port;
    int slots;
    char password[64], name[96];
    Member m[MAX_PEERS];
    int last_error;  // (for the menu: 0 fine, 1 the port is taken)
} H = {.mu = PTHREAD_MUTEX_INITIALIZER};

// ---- packets (the same encoding as xroom.c's)
typedef struct { uint8_t *b; size_t n, cap; } Pk;
static void put(Pk *p, const void *d, size_t n) {
    if (p->n + n > p->cap) {
        size_t c = p->cap ? p->cap * 2 : 256;
        while (c < p->n + n) c *= 2;
        uint8_t *nb = realloc(p->b, c);
        if (!nb) return;
        p->b = nb, p->cap = c;
    }
    memcpy(p->b + p->n, d, n);
    p->n += n;
}
static void u8_(Pk *p, uint8_t v) { put(p, &v, 1); }
static void u16_(Pk *p, uint16_t v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; put(p, b, 2); }
static void u32_(Pk *p, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    put(p, b, 4);
}
static void u64_(Pk *p, uint64_t v) { u32_(p, (uint32_t)(v >> 32)); u32_(p, (uint32_t)v); }
static void str_(Pk *p, const char *s) { size_t n = strlen(s); u32_(p, (uint32_t)n); put(p, s, n); }

typedef struct { const uint8_t *b; size_t n, pos; bool bad; } Rd;
static bool need(Rd *r, size_t n) { if (r->bad || r->pos + n > r->n) { r->bad = true; return false; } return true; }
static uint32_t rd32(Rd *r) {
    if (!need(r, 4)) return 0;
    const uint8_t *b = r->b + r->pos;
    r->pos += 4;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static uint64_t rd64(Rd *r) { uint64_t h = rd32(r); return h << 32 | rd32(r); }
static void rdstr(Rd *r, char *out, size_t size) {
    uint32_t n = rd32(r);
    if (!need(r, n)) { *out = 0; return; }
    size_t k = n < size - 1 ? n : size - 1;
    memcpy(out, r->b + r->pos, k);
    out[k] = 0;
    r->pos += n;
}

static void send_to(ENetPeer *peer, const void *d, size_t n) {
    ENetPacket *e = enet_packet_create(d, n, ENET_PACKET_FLAG_RELIABLE);
    if (e && enet_peer_send(peer, 0, e) < 0) enet_packet_destroy(e);
}
static void send_pk(ENetPeer *peer, Pk *p) { if (p->b) send_to(peer, p->b, p->n); free(p->b); p->b = NULL; }
static void to_all(const void *d, size_t n, ENetPeer *except) {
    for (int i = 0; i < MAX_PEERS; i++)
        if (H.m[i].in && H.m[i].peer != except) send_to(H.m[i].peer, d, n);
}
static void answer(ENetPeer *peer, uint8_t id) { send_to(peer, &id, 1); }

static Member *member_of(ENetPeer *peer) {
    for (int i = 0; i < MAX_PEERS; i++)
        if (H.m[i].peer == peer) return &H.m[i];
    return NULL;
}
static int members(void) {
    int n = 0;
    for (int i = 0; i < MAX_PEERS; i++) n += H.m[i].in;
    return n;
}

static void room_info(void) {
    Pk p = {0};
    u8_(&p, M_ROOM_INFO);
    str_(&p, H.name);
    str_(&p, "");  // description
    u32_(&p, (uint32_t)H.slots);
    u16_(&p, H.port);
    str_(&p, XPLAY_GAME_NAME);  // preferred game
    str_(&p, "");  // host's user name
    u32_(&p, (uint32_t)members());
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!H.m[i].in) continue;
        str_(&p, H.m[i].nick);
        u32_(&p, H.m[i].ip);
        str_(&p, H.m[i].game);
        u64_(&p, H.m[i].game_id);
        str_(&p, H.m[i].version);
        str_(&p, ""), str_(&p, ""), str_(&p, "");  // user name, display name, avatar
    }
    if (p.b) to_all(p.b, p.n, NULL);
    free(p.b);
}
static void status(uint8_t kind, const char *nick) {
    Pk p = {0};
    u8_(&p, M_STATUS);
    u8_(&p, kind);
    str_(&p, nick);
    str_(&p, "");
    if (p.b) to_all(p.b, p.n, NULL);
    free(p.b);
}

static bool nick_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 4 || n > 20) return false;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && !strchr(" ._-", *s)) return false;
    return true;
}

static void on_join(ENetPeer *peer, Rd *r) {
    char nick[64], pass[96], token[16];
    rdstr(r, nick, sizeof nick);
    uint32_t want = rd32(r), version = rd32(r);
    rdstr(r, pass, sizeof pass);
    rdstr(r, token, sizeof token);
    Member *m = member_of(peer);
    if (r->bad || !m || m->in) return;
    if (members() >= H.slots) { answer(peer, M_FULL); return; }
    if (strcmp(pass, H.password)) { answer(peer, M_PASSWORD); return; }
    bool taken = !nick_ok(nick);
    for (int i = 0; i < MAX_PEERS; i++) taken |= H.m[i].in && !strcmp(H.m[i].nick, nick);
    if (taken) { answer(peer, M_NAME_TAKEN); return; }
    uint32_t ip = want;
    bool ip_taken = false;
    for (int i = 0; i < MAX_PEERS; i++) ip_taken |= H.m[i].in && H.m[i].ip == want;
    if (want != 0xFFFFFFFFu && ip_taken) { answer(peer, M_IP_TAKEN); return; }
    if (want == 0xFFFFFFFFu)
        for (bool free_ip = false; !free_ip;) {
            ip = 0xC0A80000u | (1 + arc4random() % 254) << 8 | (1 + arc4random() % 254);
            free_ip = true;
            for (int i = 0; i < MAX_PEERS; i++) free_ip &= !(H.m[i].in && H.m[i].ip == ip);
        }
    if (version != 1) { answer(peer, M_VERSION); return; }
    m->in = true;
    snprintf(m->nick, sizeof m->nick, "%s", nick);
    m->ip = ip;
    m->game[0] = m->version[0] = 0, m->game_id = 0;
    status(ST_JOINED, nick);
    room_info();
    Pk p = {0};
    u8_(&p, M_JOINED);
    u32_(&p, ip);
    send_pk(peer, &p);
    LOG("[xroomhost] %s joined (%d member(s))\n", nick, members());
}

// A datagram or a local wireless message: to its destination's member, or (broadcast) to every other one
static void relay(ENetPeer *from, const uint8_t *b, size_t n, size_t dst_at, size_t bcast_at) {
    Member *m = member_of(from);
    if (!m || !m->in || n <= bcast_at) return;
    uint32_t dst = (uint32_t)b[dst_at] << 24 | (uint32_t)b[dst_at + 1] << 16 | (uint32_t)b[dst_at + 2] << 8 | b[dst_at + 3];
    if (b[bcast_at]) { to_all(b, n, from); return; }
    for (int i = 0; i < MAX_PEERS; i++)
        if (H.m[i].in && H.m[i].ip == dst) { send_to(H.m[i].peer, b, n); return; }
}

static void on_receive(ENetPeer *peer, const uint8_t *b, size_t n) {
    Rd r = {b, n, 1, false};
    Member *m = member_of(peer);
    switch (b[0]) {
    case M_JOIN: on_join(peer, &r); break;
    case M_GAME_INFO:
        if (!m || !m->in) break;
        rdstr(&r, m->game, sizeof m->game);
        m->game_id = rd64(&r);
        rdstr(&r, m->version, sizeof m->version);
        room_info();
        break;
    // (proxy: type, family, ip, port, family, ip at 9, port, protocol, broadcast at 16; ldn: type, kind, ip, ip at 6,
    // broadcast at 10)
    case M_PROXY: relay(peer, b, n, 9, 16); break;
    case M_LDN: relay(peer, b, n, 6, 10); break;
    case M_CHAT: {
        char line[520];
        rdstr(&r, line, sizeof line);
        if (r.bad || !m || !m->in) break;
        if (strlen(line) > 500) line[500] = 0;
        Pk p = {0};
        u8_(&p, M_CHAT);
        str_(&p, m->nick);
        str_(&p, "");
        str_(&p, line);
        if (p.b) to_all(p.b, p.n, peer);
        free(p.b);
        break;
    }
    default: break;  // (moderation: none in this room)
    }
}

static void *host_thread(void *arg) {
    (void)arg;
    plat_thread_name("xroomhost");
    ENetEvent ev;
    while (H.run) {
        if (enet_host_service(H.host, &ev, 20) <= 0) continue;
        pthread_mutex_lock(&H.mu);
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Member *m = member_of(NULL);
            if (m) memset(m, 0, sizeof *m), m->peer = ev.peer;
            else enet_peer_disconnect(ev.peer, 0);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            if (ev.packet->dataLength) on_receive(ev.peer, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT: {
            Member *m = member_of(ev.peer);
            if (m) {
                bool was = m->in;
                char nick[24];
                snprintf(nick, sizeof nick, "%s", m->nick);
                memset(m, 0, sizeof *m);
                if (was) {
                    status(ST_LEFT, nick);
                    room_info();
                    LOG("[xroomhost] %s left (%d member(s))\n", nick, members());
                }
            }
            break;
        }
        default: break;
        }
        pthread_mutex_unlock(&H.mu);
    }
    // closing: everyone is told, then the connections go
    pthread_mutex_lock(&H.mu);
    uint8_t closed = M_CLOSED;
    to_all(&closed, 1, NULL);
    enet_host_flush(H.host);
    for (int i = 0; i < MAX_PEERS; i++)
        if (H.m[i].peer) enet_peer_disconnect_later(H.m[i].peer, 0);
    enet_host_flush(H.host);
    pthread_mutex_unlock(&H.mu);
    plat_sleep_until_ns(plat_now_ns() + 300000000ull);
    pthread_mutex_lock(&H.mu);
    enet_host_destroy(H.host);
    H.host = NULL;
    memset(H.m, 0, sizeof H.m);
    pthread_mutex_unlock(&H.mu);
    return NULL;
}

void xroomhost_stop(void) {
    if (!H.has_thread) return;
    H.run = false;
    pthread_join(H.th, NULL);
    H.has_thread = false;
    LOG("[xroomhost] the room on port %u is closed\n", H.port);
}

bool xroomhost_start(uint16_t port, int slots, const char *password) {
    if (H.has_thread && H.port == port && H.slots == slots && !strcmp(H.password, password)) return true;
    xroomhost_stop();
    static bool inited;
    if (!inited) inited = enet_initialize() == 0;
    ENetAddress a = {0};
    a.host = ENET_HOST_ANY;
    a.port = port;
    const char *bind_to = getenv("MHGU_ROOM_BIND");  // (tests: 127.0.0.1, so a desktop's firewall asks nothing)
    if (bind_to && *bind_to) enet_address_set_host_ip(&a, bind_to);
    H.host = inited ? enet_host_create(&a, MAX_PEERS, 1, 0, 0) : NULL;
    if (!H.host) {
        H.last_error = 1;
        LOG("[xroomhost] can't open a room on UDP port %u (in use?)\n", port);
        return false;
    }
    H.last_error = 0;
    H.port = port;
    H.slots = slots < 2 ? 2 : slots > MAX_PEERS ? MAX_PEERS : slots;
    snprintf(H.password, sizeof H.password, "%s", password);
    snprintf(H.name, sizeof H.name, "MHGU");
    memset(H.m, 0, sizeof H.m);
    H.run = true;
    H.has_thread = pthread_create(&H.th, NULL, host_thread, NULL) == 0;
    if (!H.has_thread) {
        enet_host_destroy(H.host);
        H.host = NULL;
        return false;
    }
    LOG("[xroomhost] a room on UDP port %u, %d places%s\n", port, H.slots, *password ? ", with a password" : "");
    return true;
}

bool xroomhost_running(uint16_t *port, int *n) {
    pthread_mutex_lock(&H.mu);
    bool up = H.has_thread && H.host;
    if (port) *port = H.port;
    if (n) *n = up ? members() : 0;
    pthread_mutex_unlock(&H.mu);
    return up;
}

// This device's IPv4 addresses (not the loopback): where others join the room it hosts
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
int xroomhost_addresses(char (*out)[48], int max) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    INTERFACE_INFO list[32];
    DWORD bytes = 0;
    int n = 0;
    if (s != INVALID_SOCKET && WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0, list, sizeof list, &bytes, NULL, NULL) == 0)
        for (DWORD i = 0; i < bytes / sizeof list[0] && n < max; i++) {
            const INTERFACE_INFO *f = &list[i];
            if (!(f->iiFlags & IFF_UP) || (f->iiFlags & IFF_LOOPBACK) || f->iiAddress.Address.sa_family != AF_INET) continue;
            inet_ntop(AF_INET, &f->iiAddress.AddressIn.sin_addr, out[n++], 48);
        }
    if (s != INVALID_SOCKET) closesocket(s);
    return n;
}
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
int xroomhost_addresses(char (*out)[48], int max) {
    struct ifaddrs *all = NULL;
    int n = 0;
    if (getifaddrs(&all)) return 0;
    for (int pass = 0; pass < 2; pass++)  // (Wi-Fi first)
        for (struct ifaddrs *i = all; i && n < max; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP))
                continue;
            bool wifi = !strncmp(i->ifa_name, "wlan", 4);
            if (wifi != (pass == 0)) continue;
            inet_ntop(AF_INET, &((struct sockaddr_in *)(void *)i->ifa_addr)->sin_addr, out[n++], 48);
        }
    freeifaddrs(all);
    return n;
}
#endif
