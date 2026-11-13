// Crossplay with the multiplayer rooms of the yuzu family of emulators (Eden, Citron, Sudachi, Suyu, yuzu): ini
// [Multiplayer] Crossplay = Rooms (net.h). Those emulators carry the game's local wireless play through a "room": a
// server that relays packets between its members, over ENet (reliable UDP; port 24872 unless the room says another).
// This file is one member of such a room, written from the protocol as those emulators speak it (it shares no code
// with them): the room's own messages, the local wireless network's messages between the games in it (scan, scan
// reply, connect, network sync, disconnect, destroy) and the games' datagrams (their data in a zstd frame).
//
// The room's messages: the first byte is the message's type; numbers are big endian, a string or a byte vector is a
// u32 length and its bytes, an IPv4 address its 4 bytes in network order, a bool one byte.
//   1 join request: nickname, preferred address (255.255.255.255: any), u32 protocol version (1), password, token
//   2 / 24 joined (24: as a moderator): our address in the room (192.168.x.y, made up by the room)
//   3 room information: name, description, u32 slots, u16 port, preferred game, host's user name, u32 members, then each
//     member: nickname, address, game name, u64 game id, game version, user name, display name, avatar URL
//   4 our game: name, u64 id, version (the version is the game's own text, "1.4.0")
//   5 a game datagram: u8 family (1), source address, u16 source port, u8 family, destination address, u16 port,
//     u8 protocol (0, or UDP: 3, 4 in Eden 0.2.1 and later - see peer_udp), bool broadcast, vector data (a zstd frame);
//     the room passes it to the destination's member, or with broadcast to everyone else
//   6 a local wireless message: u8 type, source address, destination address, bool broadcast, vector data (a raw
//     NetworkInfo or NodeInfo of the SDK, as ldn.c keeps them)
//   7 chat: (to the room) the line; (from the room) nickname, user name, line
//   8 name taken, 9 address taken, 10 another protocol version, 11 wrong password, 12 the room closed, 13 full,
//   14 status (u8 kind: 1 joined, 2 left, 3 kicked, 4 banned; nickname, user name), 15 kicked, 16 banned
// The local wireless messages (6): 0 scan (broadcast), 1 scan reply (the host's NetworkInfo, to the one who asked),
// 2 connect (the joining game's NodeInfo, to the host), 3 network sync (the host's NetworkInfo, to each player in it),
// 4 disconnect (the leaving game's NodeInfo, to the host), 5 destroy (the host ends the network, to each player).
// A game's MAC address there is 02:00 and its room address; its address in NodeInfo is the room's, as a host-order u32.
//
// Threads: one runs the room's connection (xr_thread); the game's threads call the XBackend functions, which only queue
// packets and wait for answers. ENet is used under enet_mu only.
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <enet/enet.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "../config.h"
#include "../hle.h"
#include "../overlay.h"
#include "../platform.h"
#include "net.h"

#define ROOM_PORT 24872
#define ROOM_PROTOCOL 1
#define NETINFO_SIZE 0x480
#define NODE_SIZE 0x40
#define NI_BSSID 0x20
#define NI_ACCEPT_POLICY 0x62
#define NI_NODE_MAX 0x66
#define NI_NODE_COUNT 0x67
#define NI_NODES 0x68
#define NODE_IP 0x00
#define NODE_MAC 0x04
#define NODE_ID 0x0A
#define NODE_CONNECTED 0x0B
#define NODE_LCV 0x2E
#define MAX_NODES 8
#define MAX_MEMBERS 64
#define MAX_FOUND 16
#define DATA_MAX 0x10000

enum { M_JOIN = 1, M_JOINED, M_ROOM_INFO, M_GAME_INFO, M_PROXY, M_LDN, M_CHAT, M_NAME_TAKEN, M_IP_TAKEN, M_VERSION,
       M_PASSWORD, M_CLOSED, M_FULL, M_STATUS, M_KICKED, M_BANNED, M_JOINED_MOD = 24 };
enum { L_SCAN, L_SCAN_REPLY, L_CONNECT, L_SYNC, L_DISCONNECT, L_DESTROY };
enum { R_HOST = 1, R_STATION };
enum { DR_DESTROYED_BY_USER = 3, DR_REJECTED = 5, DR_SIGNAL_LOST = 6 };
#define LDN_RESULT(d) ((203u) | ((uint32_t)(d) << 9))
#define R_INVALID_STATE LDN_RESULT(32)
#define R_CONNECT_FAILURE LDN_RESULT(64)
#define R_CONNECT_TIMEOUT LDN_RESULT(66)
#define R_CONNECT_REJECTED LDN_RESULT(67)
#define R_VERSION_TOO_LOW LDN_RESULT(113)
#define R_VERSION_TOO_HIGH LDN_RESULT(114)

// Texts the player sees: English and Spanish side by side (the menu's language)
static const char *xt(const char *en, const char *es) { return config_language() == 1 ? es : en; }

// ---- packets
typedef struct { uint8_t *b; size_t n, cap; } Pk;
static void pk_put(Pk *p, const void *d, size_t n) {
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
static void pk_u8(Pk *p, uint8_t v) { pk_put(p, &v, 1); }
static void pk_u16(Pk *p, uint16_t v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; pk_put(p, b, 2); }
static void pk_u32(Pk *p, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    pk_put(p, b, 4);
}
static void pk_u64(Pk *p, uint64_t v) { pk_u32(p, (uint32_t)(v >> 32)); pk_u32(p, (uint32_t)v); }
static void pk_ip(Pk *p, uint32_t ip) { pk_u32(p, ip); }  // (network order: the host-order value's bytes, high first)
static void pk_str(Pk *p, const char *s) { size_t n = strlen(s); pk_u32(p, (uint32_t)n); pk_put(p, s, n); }
static void pk_vec(Pk *p, const void *d, size_t n) { pk_u32(p, (uint32_t)n); pk_put(p, d, n); }

typedef struct { const uint8_t *b; size_t n, pos; bool bad; } Rd;
static bool rd_need(Rd *r, size_t n) {
    if (r->bad || r->pos + n > r->n) { r->bad = true; return false; }
    return true;
}
static uint8_t rd_u8(Rd *r) { return rd_need(r, 1) ? r->b[r->pos++] : 0; }
static uint16_t rd_u16(Rd *r) {
    if (!rd_need(r, 2)) return 0;
    uint16_t v = (uint16_t)(r->b[r->pos] << 8 | r->b[r->pos + 1]);
    r->pos += 2;
    return v;
}
static uint32_t rd_u32(Rd *r) {
    if (!rd_need(r, 4)) return 0;
    const uint8_t *b = r->b + r->pos;
    r->pos += 4;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static uint64_t rd_u64(Rd *r) { uint64_t h = rd_u32(r); return h << 32 | rd_u32(r); }
static void rd_str(Rd *r, char *out, size_t size) {
    uint32_t n = rd_u32(r);
    if (!rd_need(r, n)) { if (size) *out = 0; return; }
    size_t k = n < size - 1 ? n : size - 1;
    memcpy(out, r->b + r->pos, k);
    out[k] = 0;
    r->pos += n;
}
static const uint8_t *rd_vec(Rd *r, uint32_t *len) {
    uint32_t n = rd_u32(r);
    if (!rd_need(r, n)) { *len = 0; return NULL; }
    const uint8_t *p = r->b + r->pos;
    r->pos += n;
    *len = n;
    return p;
}

static const char *ipstr(uint32_t ip) {
    static __thread char b[2][20];
    static __thread int k;
    char *s = b[k++ & 1];
    snprintf(s, 20, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return s;
}
static uint32_t rd32le(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16le(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

// ---- zstd: the rooms' game datagrams are zstd frames. Ours are frames of raw blocks (every zstd decoder reads them,
// and they cost nothing to make), with the content size in the header, which the emulators' decoder needs.
static size_t zframe(uint8_t *out, const void *data, size_t n) {
    uint8_t *o = out;
    *o++ = 0x28, *o++ = 0xB5, *o++ = 0x2F, *o++ = 0xFD;  // magic
    if (n < 256) {          // single segment, a 1-byte content size
        *o++ = 0x20;
        *o++ = (uint8_t)n;
    } else if (n < 65792) { // 2 bytes: the size - 256
        *o++ = 0x60;
        uint16_t v = (uint16_t)(n - 256);
        *o++ = (uint8_t)v, *o++ = (uint8_t)(v >> 8);
    } else {
        *o++ = 0xA0;
        uint32_t v = (uint32_t)n;
        memcpy(o, &v, 4), o += 4;
    }
    const uint8_t *d = data;
    size_t left = n;
    do {  // raw blocks of up to 128 KB (here a datagram is one)
        size_t k = left < 0x20000 ? left : 0x1FFFF;
        uint32_t h = (uint32_t)(k << 3) | (left == k ? 1u : 0u);  // block size, raw (type 0), last
        *o++ = (uint8_t)h, *o++ = (uint8_t)(h >> 8), *o++ = (uint8_t)(h >> 16);
        memcpy(o, d, k);
        o += k, d += k, left -= k;
    } while (left);
    return (size_t)(o - out);
}
// A frame's content into out (up to cap); -1: not one, or too large
static long zread(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    static __thread ZSTD_DCtx *dc;
    if (!dc && !(dc = ZSTD_createDCtx())) return -1;
    unsigned long long size = ZSTD_getFrameContentSize(in, n);
    if (size == ZSTD_CONTENTSIZE_ERROR || (size != ZSTD_CONTENTSIZE_UNKNOWN && size > cap)) return -1;
    size_t got = ZSTD_decompressDCtx(dc, out, cap, in, n);
    return ZSTD_isError(got) ? -1 : (long)got;
}

// ---- state
typedef struct { char nick[40], game[96], version[40]; uint32_t ip; uint64_t id; } Member;
static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    // the room
    int state;               // XS_*
    char error[200];
    char host[128];          // the room joined (or being joined): its address, port, password and our nickname
    uint16_t port;
    char password[64], nick[24];
    uint32_t ip;             // ours in the room, 0 none yet
    char name[96];
    uint32_t slots;
    Member members[MAX_MEMBERS];
    int nmembers;
    bool run;
    pthread_t th;
    bool has_thread;
    uint32_t gen;            // each connection
    // the local wireless network
    int role;                // R_*, 0 none
    uint8_t info[NETINFO_SIZE];  // the host's: ours; a station's: the last sync
    uint32_t client[MAX_NODES];  // host: the room address of each node's game (0: none)
    uint32_t host_ip;        // station: the host's
    uint16_t lcv;            // our game's local communication version
    bool synced;             // station: the host's sync with us in it came
    bool scanning;
    uint8_t found[MAX_FOUND][NETINFO_SIZE];
    int nfound;
    // chat lines from the room, for xroom_chat_take
    char chat[16][600];
    int nchat;
} X = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};

static pthread_mutex_t enet_mu = PTHREAD_MUTEX_INITIALIZER;
static ENetHost *g_host;
static ENetPeer *g_peer;

static void send_pk(Pk *p) {  // (any thread; drops it when not connected)
    pthread_mutex_lock(&enet_mu);
    if (g_peer && p->b) {
        ENetPacket *e = enet_packet_create(p->b, p->n, ENET_PACKET_FLAG_RELIABLE);
        if (e && enet_peer_send(g_peer, 0, e) < 0) enet_packet_destroy(e);
        enet_host_flush(g_host);
    }
    pthread_mutex_unlock(&enet_mu);
    free(p->b);
    p->b = NULL;
}

static void mac_of(uint32_t ip, uint8_t out[6]) {
    out[0] = 0x02, out[1] = 0x00;
    out[2] = (uint8_t)(ip >> 24), out[3] = (uint8_t)(ip >> 16), out[4] = (uint8_t)(ip >> 8), out[5] = (uint8_t)ip;
}

static void send_ldn(uint8_t type, uint32_t to, bool broadcast, const void *data, size_t n) {
    Pk p = {0};
    pk_u8(&p, M_LDN);
    pk_u8(&p, type);
    pk_ip(&p, X.ip);
    pk_ip(&p, broadcast ? 0 : to);
    pk_u8(&p, broadcast);
    pk_vec(&p, data, n);
    send_pk(&p);
}

// The leading x.y.z of a version text ("1.4.0", "v1.4.0 (mods)"): false if it has none
static bool version_numbers(const char *v, int out[3]) {
    while (*v && !isdigit((unsigned char)*v)) v++;
    return *v && sscanf(v, "%d.%d.%d", &out[0], &out[1], &out[2]) >= 2;
}
// Whether a member's game is this one in another version (an unknown version is let through: the game checks too)
static bool other_version(const Member *m) {
    if (m->id != XPLAY_TITLE_ID || !*m->version) return false;
    int a[3] = {0}, b[3] = {0};
    version_numbers(XPLAY_GAME_VERSION, b);
    return version_numbers(m->version, a) && (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]);
}
static const Member *member_at(uint32_t ip) {  // (X.mu held)
    for (int i = 0; i < X.nmembers; i++)
        if (X.members[i].ip == ip) return &X.members[i];
    return NULL;
}

static void wrong_version_notice(const char *nick, const char *version, bool joining) {
    char title[160], text[400];
    snprintf(title, sizeof title, xt("%s plays version %s of the game", "%s juega la versión %s del juego"), nick,
             version);
    snprintf(text, sizeof text,
             joining ? xt("This app plays only with version " XPLAY_GAME_VERSION ": you can't join that hub. They can update "
                          "the game to " XPLAY_GAME_VERSION " in their emulator.",
                          "Esta app juega solo con la versión " XPLAY_GAME_VERSION ": no puedes entrar a esa sala. Puede "
                          "actualizar el juego a la " XPLAY_GAME_VERSION " en su emulador.")
                     : xt("They tried to join your hub, but this app plays only with version " XPLAY_GAME_VERSION ". They can "
                          "update the game to " XPLAY_GAME_VERSION " in their emulator.",
                          "Intentó entrar a tu sala, pero esta app juega solo con la versión " XPLAY_GAME_VERSION ". Puede "
                          "actualizar el juego a la " XPLAY_GAME_VERSION " en su emulador."),
             nick);
    overlay_notice(title, text);
}

// ---- the local wireless network (X.mu held)
static void host_sync_locked(void) {
    int count = 1;
    for (int i = 1; i < MAX_NODES; i++)
        if (X.client[i]) count++;
    X.info[NI_NODE_COUNT] = (uint8_t)count;
    for (int i = 1; i < MAX_NODES; i++)
        if (X.client[i]) send_ldn(L_SYNC, X.client[i], false, X.info, NETINFO_SIZE);
}
static void host_drop_locked(int i) {
    uint32_t ip = X.client[i];
    X.client[i] = 0;
    uint8_t *nd = X.info + NI_NODES + i * NODE_SIZE;
    memset(nd, 0, NODE_SIZE);
    nd[NODE_ID] = (uint8_t)i;
    host_sync_locked();
    LOG("[xroom] %s left the hub\n", ipstr(ip));
    ldn_x_peer_gone(ip);
}

static void on_ldn(uint8_t type, uint32_t from, const uint8_t *d, uint32_t n) {
    uint8_t info[NETINFO_SIZE];
    bool changed = false, lost = false, notify = false;
    char nick[40] = "", version[40] = "";
    pthread_mutex_lock(&X.mu);
    switch (type) {
    case L_SCAN:
        if (X.role == R_HOST) send_ldn(L_SCAN_REPLY, from, false, X.info, NETINFO_SIZE);
        break;
    case L_SCAN_REPLY:
        if (X.scanning && n >= NETINFO_SIZE) {
            int i = 0;
            while (i < X.nfound && memcmp(X.found[i] + NI_BSSID, d + NI_BSSID, 6)) i++;
            if (i < MAX_FOUND) {
                memcpy(X.found[i], d, NETINFO_SIZE);
                if (i == X.nfound) X.nfound++;
            }
        }
        break;
    case L_CONNECT: {
        if (X.role != R_HOST || n < NODE_SIZE) break;
        const Member *m = member_at(from);
        uint16_t lcv = rd16le(d + NODE_LCV);
        int free_slot = 0;
        for (int i = 1; i < MAX_NODES && !free_slot; i++)
            if (X.client[i] == from) free_slot = i;  // (again: the same slot)
        for (int i = 1; i < X.info[NI_NODE_MAX] && i < MAX_NODES && !free_slot; i++)
            if (!X.client[i]) free_slot = i;
        bool wrong = (m && other_version(m)) || lcv != X.lcv;
        if (wrong || !free_slot || X.info[NI_ACCEPT_POLICY] == 1) {  // (1: the game takes nobody now)
            send_ldn(L_DESTROY, from, false, NULL, 0);  // (the emulators' way of sending a player away)
            if (wrong) {
                snprintf(nick, sizeof nick, "%s", m ? m->nick : ipstr(from));
                snprintf(version, sizeof version, "%s", m && *m->version ? m->version : "?");
                notify = true;
                // (the emulator shows nothing of why: the room's chat says it, in English as rooms mix languages)
                char line[200];
                snprintf(line, sizeof line,
                         "%s: this hub plays the game's version " XPLAY_GAME_VERSION " only (yours: %s). Update the game to "
                         XPLAY_GAME_VERSION " to join.",
                         nick, version);
                xroom_chat_send(line);
            }
            LOG("[xroom] %s can't join the hub: %s\n", ipstr(from),
                wrong ? "another version of the game" : !free_slot ? "it is full" : "it takes nobody now");
            break;
        }
        uint8_t *nd = X.info + NI_NODES + free_slot * NODE_SIZE;
        memcpy(nd, d, NODE_SIZE);
        nd[NODE_ID] = (uint8_t)free_slot;
        nd[NODE_CONNECTED] = 1;
        X.client[free_slot] = from;
        host_sync_locked();
        memcpy(info, X.info, NETINFO_SIZE);
        changed = true;
        LOG("[xroom] %s (%s) joined the hub as node %d\n", m ? m->nick : "?", ipstr(from), free_slot);
        break;
    }
    case L_DISCONNECT:
        if (X.role != R_HOST) break;
        for (int i = 1; i < MAX_NODES; i++)
            if (X.client[i] == from) {
                host_drop_locked(i);
                memcpy(info, X.info, NETINFO_SIZE);
                changed = true;
            }
        break;
    case L_SYNC:
        if (X.role != R_STATION || from != X.host_ip || n < NETINFO_SIZE) break;
        {
            bool in = false;
            for (int i = 0; i < MAX_NODES; i++) {
                const uint8_t *nd = d + NI_NODES + i * NODE_SIZE;
                in |= nd[NODE_CONNECTED] && rd32le(nd + NODE_IP) == X.ip;
            }
            if (!in && X.synced) {  // (taken out of the network)
                lost = true;
                X.role = 0;
                break;
            }
            memcpy(X.info, d, NETINFO_SIZE);
            memcpy(info, d, NETINFO_SIZE);
            if (in) X.synced = true;
            changed = X.synced;
            pthread_cond_broadcast(&X.cv);
        }
        break;
    case L_DESTROY:
        if (X.role == R_STATION && from == X.host_ip) {
            lost = true;
            X.role = 0;
            pthread_cond_broadcast(&X.cv);
        }
        break;
    }
    bool station_lost = lost;
    pthread_mutex_unlock(&X.mu);
    if (notify) wrong_version_notice(nick, version, false);
    if (changed) ldn_x_info(info);
    if (station_lost) {
        LOG("[xroom] the hub's host ended it\n");
        ldn_x_lost(DR_DESTROYED_BY_USER);
    }
}

// ---- the protocol byte of a game datagram: 0 when the game opened its socket without one, else UDP's number in the
// emulator's own list - 3 in yuzu and most forks, 4 in Eden from 0.2.1 (and the Suyu built on it). An emulator drops
// a datagram whose byte isn't its socket's. So a datagram of a socket opened as UDP goes out with the number each
// member is known to use (learned from what it sends), and to one not known yet (and in a broadcast) once with each:
// the other one drops the copy it doesn't read. Another port reads both: the second copy is dropped here (seen_twice).
#define PEERS 32
static struct { uint32_t ip; uint8_t udp; } g_num[PEERS];  // (network thread writes, game threads read: a byte each)
static uint8_t peer_udp(uint32_t ip) {
    for (int i = 0; i < PEERS; i++)
        if (g_num[i].ip == ip) return g_num[i].udp;
    return 0;
}
// (the first number a member sends is its own: an emulator always uses one; another port sends both, the first being
// 3, and reads either)
static void learn_udp(uint32_t ip, uint8_t udp) {
    if ((udp != 3 && udp != 4) || peer_udp(ip)) return;
    int k = 0;
    for (int i = 0; i < PEERS; i++)
        if (g_num[i].ip == ip || !g_num[i].ip) { k = i; break; }
    if (g_num[k].ip != ip) LOG("[xroom] %s numbers UDP %u in the room's datagrams (%s)\n", ipstr(ip), udp,
                               udp == 4 ? "Eden 0.2.1 or later" : "yuzu, Citron, Sudachi, older Eden");
    g_num[k].ip = ip, g_num[k].udp = udp;
}
static bool seen_twice(uint32_t src, uint16_t sport, uint16_t dport, const uint8_t *d, long n, uint8_t proto) {
    static struct { uint64_t at; uint32_t src, hash; uint16_t sport, dport; long n; uint8_t proto; } last[16];
    static int next;
    uint32_t h = 2166136261u;
    for (long i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    uint64_t now = plat_now_ns();
    for (int i = 0; i < 16; i++)
        if (last[i].src == src && last[i].hash == h && last[i].n == n && last[i].sport == sport && last[i].dport == dport &&
            last[i].proto != proto && now - last[i].at < 300000000ull) {
            last[i].at = 0;
            return true;
        }
    last[next] = (__typeof__(last[0])){now, src, h, sport, dport, n, proto};
    next = (next + 1) & 15;
    return false;
}

static void on_proxy(Rd *r) {
    uint32_t src, dst, len;
    uint16_t sport, dport;
    rd_u8(r);
    src = rd_u32(r), sport = rd_u16(r);
    rd_u8(r);
    dst = rd_u32(r), dport = rd_u16(r);
    uint8_t proto = rd_u8(r), bcast = rd_u8(r);
    const uint8_t *d = rd_vec(r, &len);
    // (datagrams only: 0, or UDP as 3 or 4. The emulators carry no TCP: 2 is yuzu's)
    if (r->bad || !d || (proto != 0 && proto != 3 && proto != 4)) return;
    static __thread uint8_t *buf;
    if (!buf && !(buf = malloc(DATA_MAX))) return;
    long n = zread(d, len, buf, DATA_MAX);
    if (n < 0) {
        static int bad;
        if (bad++ < 5) LOG("[xroom] a game packet from %s that isn't a zstd frame: dropped\n", ipstr(src));
        return;
    }
    learn_udp(src, proto);
    if (proto && seen_twice(src, sport, dport, buf, n, proto)) return;
    VPacket p = {src, bcast ? 0xFFFFFFFFu : dst, sport, dport, VPROTO_UDP, 0, 0};
    ldn_x_packet(&p, buf, (uint32_t)n);
}

// The room's member list changed: a hub's player who is no longer in the room left it (the emulators don't say so)
static void members_changed(void) {
    uint8_t info[NETINFO_SIZE];
    bool changed = false, lost = false;
    pthread_mutex_lock(&X.mu);
    if (X.role == R_HOST) {
        for (int i = 1; i < MAX_NODES; i++)
            if (X.client[i] && !member_at(X.client[i])) host_drop_locked(i), changed = true;
        if (changed) memcpy(info, X.info, NETINFO_SIZE);
    } else if (X.role == R_STATION && X.host_ip && !member_at(X.host_ip)) {
        lost = true;
        X.role = 0;
        pthread_cond_broadcast(&X.cv);
    }
    pthread_mutex_unlock(&X.mu);
    if (changed) ldn_x_info(info);
    if (lost) {
        LOG("[xroom] the hub's host left the room\n");
        ldn_x_lost(DR_SIGNAL_LOST);
    }
}

static void on_room_info(Rd *r) {
    char name[96], skip[256];
    rd_str(r, name, sizeof name);
    rd_str(r, skip, sizeof skip);  // description
    uint32_t slots = rd_u32(r);
    rd_u16(r);                     // port
    rd_str(r, skip, sizeof skip);  // preferred game
    rd_str(r, skip, sizeof skip);  // host's user name
    uint32_t n = rd_u32(r);
    Member ms[MAX_MEMBERS];
    int k = 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        Member m = {0};
        rd_str(r, m.nick, sizeof m.nick);
        m.ip = rd_u32(r);
        rd_str(r, m.game, sizeof m.game);
        m.id = rd_u64(r);
        rd_str(r, m.version, sizeof m.version);
        rd_str(r, skip, sizeof skip);  // user name, display name, avatar
        rd_str(r, skip, sizeof skip);
        rd_str(r, skip, sizeof skip);
        if (k < MAX_MEMBERS) ms[k++] = m;
    }
    if (r->bad) return;
    pthread_mutex_lock(&X.mu);
    snprintf(X.name, sizeof X.name, "%s", name);
    X.slots = slots;
    memcpy(X.members, ms, sizeof ms[0] * (size_t)k);
    X.nmembers = k;
    pthread_mutex_unlock(&X.mu);
    members_changed();
}

static void fail_locked(const char *why) {
    X.state = XS_FAILED;
    snprintf(X.error, sizeof X.error, "%s", why);
    X.ip = 0;
    pthread_cond_broadcast(&X.cv);
}

static void on_message(const uint8_t *b, size_t n) {
    Rd r = {b, n, 1, false};
    switch (b[0]) {
    case M_JOINED:
    case M_JOINED_MOD: {
        uint32_t ip = rd_u32(&r);
        if (r.bad) break;
        pthread_mutex_lock(&X.mu);
        X.ip = ip;
        X.state = XS_IN;
        pthread_cond_broadcast(&X.cv);
        pthread_mutex_unlock(&X.mu);
        LOG("[xroom] in the room as %s\n", ipstr(ip));
        Pk p = {0};  // what we play, as the emulators say it
        pk_u8(&p, M_GAME_INFO);
        pk_str(&p, XPLAY_GAME_NAME);
        pk_u64(&p, XPLAY_TITLE_ID);
        pk_str(&p, XPLAY_GAME_VERSION);
        send_pk(&p);
        break;
    }
    case M_ROOM_INFO: on_room_info(&r); break;
    case M_PROXY: on_proxy(&r); break;
    case M_LDN: {
        uint8_t type = rd_u8(&r);
        uint32_t from = rd_u32(&r);
        rd_u32(&r);
        rd_u8(&r);
        uint32_t len;
        const uint8_t *d = rd_vec(&r, &len);
        if (!r.bad) on_ldn(type, from, d ? d : (const uint8_t *)"", len);
        break;
    }
    case M_CHAT: {
        char nick[40], user[64], line[520];
        rd_str(&r, nick, sizeof nick);
        rd_str(&r, user, sizeof user);
        rd_str(&r, line, sizeof line);
        if (r.bad) break;
        pthread_mutex_lock(&X.mu);
        if (X.nchat == 16) memmove(X.chat[0], X.chat[1], sizeof X.chat[0] * 15), X.nchat--;
        snprintf(X.chat[X.nchat++], sizeof X.chat[0], "%s\t%s", nick, line);
        pthread_mutex_unlock(&X.mu);
        break;
    }
    case M_NAME_TAKEN:
    case M_IP_TAKEN:
    case M_VERSION:
    case M_PASSWORD:
    case M_CLOSED:
    case M_FULL:
    case M_KICKED:
    case M_BANNED: {
        const char *why;
        switch (b[0]) {
        case M_NAME_TAKEN: why = xt("Someone in the room already has that nickname (or it isn't a valid one: 4 to 20 "
                                    "letters, digits, spaces, '.', '_' or '-').",
                                    "Alguien en la sala ya tiene ese apodo (o no es válido: de 4 a 20 letras, números, "
                                    "espacios, '.', '_' o '-')."); break;
        case M_IP_TAKEN: why = xt("The room gave this address to someone else. Try again.",
                                  "La sala le dio esta dirección a otro. Prueba otra vez."); break;
        case M_VERSION: why = xt("The room is of another kind of emulator (another protocol version): it isn't a room "
                                 "of the yuzu family.",
                                 "La sala es de otro tipo de emulador (otra versión del protocolo): no es una sala de "
                                 "la familia de yuzu."); break;
        case M_PASSWORD: why = xt("Wrong room password.", "Contraseña de la sala incorrecta."); break;
        case M_CLOSED: why = xt("The room was closed.", "La sala se cerró."); break;
        case M_FULL: why = xt("The room is full.", "La sala está llena."); break;
        case M_KICKED: why = xt("The room's host kicked you out.", "El anfitrión de la sala te echó."); break;
        default: why = xt("The room's host banned you.", "El anfitrión de la sala te prohibió la entrada."); break;
        }
        LOG("[xroom] the room refused or ended the connection (message %u)\n", b[0]);
        pthread_mutex_lock(&X.mu);
        fail_locked(why);
        X.run = false;
        bool joined = X.role == R_STATION;
        X.role = 0;
        pthread_mutex_unlock(&X.mu);
        if (joined) ldn_x_lost(DR_SIGNAL_LOST);
        break;
    }
    default: break;  // status, moderation
    }
}

// ---- the room's connection
static bool parse_address(const char *text, char *host, size_t size, uint16_t *port) {
    while (*text == ' ') text++;
    snprintf(host, size, "%s", text);
    size_t n = strlen(host);
    while (n && host[n - 1] == ' ') host[--n] = 0;
    *port = ROOM_PORT;
    char *colon = strrchr(host, ':');
    if (colon) {
        int p = atoi(colon + 1);
        if (p < 1 || p > 65535) return false;
        *port = (uint16_t)p;
        *colon = 0;
    }
    return *host;
}

static void *xr_thread(void *arg) {
    (void)arg;
    plat_thread_name("xroom");
    static bool inited;
    if (!inited) inited = enet_initialize() == 0;
    pthread_mutex_lock(&X.mu);
    char host[128], password[64], nick[24];
    snprintf(host, sizeof host, "%s", X.host);
    snprintf(password, sizeof password, "%s", X.password);
    snprintf(nick, sizeof nick, "%s", X.nick);
    uint16_t port = X.port;
    pthread_mutex_unlock(&X.mu);
    ENetAddress a = {0};
    a.port = port;
    ENetHost *h = inited ? enet_host_create(NULL, 1, 1, 0, 0) : NULL;
    if (!h || enet_address_set_host(&a, host) < 0) {
        pthread_mutex_lock(&X.mu);
        fail_locked(h ? xt("Room not found: check its address.", "No se encontró la sala: revisa su dirección.")
                      : xt("The network could not start.", "La red no pudo iniciarse."));
        pthread_mutex_unlock(&X.mu);
        if (h) enet_host_destroy(h);
        LOG("[xroom] room %s:%u: %s\n", host, port, h ? "the address doesn't resolve" : "no ENet");
        return NULL;
    }
    pthread_mutex_lock(&enet_mu);
    g_host = h;
    ENetPeer *peer = enet_host_connect(h, &a, 1, 0);
    pthread_mutex_unlock(&enet_mu);
    ENetEvent ev;
    bool connected = false;
    for (uint64_t end = plat_now_ns() + 5000000000ull; peer && plat_now_ns() < end && X.run && !connected;) {
        pthread_mutex_lock(&enet_mu);
        int k = enet_host_service(h, &ev, 0);
        pthread_mutex_unlock(&enet_mu);
        if (k > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) connected = true;
        else if (k > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        else plat_sleep_until_ns(plat_now_ns() + 5000000ull);
    }
    if (!connected) {
        LOG("[xroom] room %s:%u doesn't answer\n", host, port);
        pthread_mutex_lock(&X.mu);
        if (X.run)
            fail_locked(xt("The room doesn't answer: check its address and port, and that it is open.",
                           "La sala no responde: revisa su dirección y puerto, y que esté abierta."));
        pthread_mutex_unlock(&X.mu);
        pthread_mutex_lock(&enet_mu);
        g_host = NULL;
        enet_host_destroy(h);
        pthread_mutex_unlock(&enet_mu);
        return NULL;
    }
    pthread_mutex_lock(&enet_mu);
    g_peer = peer;
    pthread_mutex_unlock(&enet_mu);
    Pk p = {0};
    pk_u8(&p, M_JOIN);
    pk_str(&p, nick);
    pk_ip(&p, 0xFFFFFFFFu);
    pk_u32(&p, ROOM_PROTOCOL);
    pk_str(&p, password);
    pk_str(&p, "");
    send_pk(&p);
    LOG("[xroom] connected to the room %s:%u, joining as %s\n", host, port, nick);
    uint64_t asked = plat_now_ns();
    static uint8_t *buf;
    if (!buf) buf = malloc(DATA_MAX + 1024);
    while (X.run) {
        // (events under the lock, handled outside it: the handlers send)
        bool got = false;
        size_t n = 0;
        pthread_mutex_lock(&enet_mu);
        int k = enet_host_service(h, &ev, 0);
        if (k > 0) {
            got = true;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                n = ev.packet->dataLength;
                if (buf && n && n <= DATA_MAX + 1024) memcpy(buf, ev.packet->data, n);
                else n = 0;
                enet_packet_destroy(ev.packet);
            }
        }
        pthread_mutex_unlock(&enet_mu);
        if (got && ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            LOG("[xroom] the room dropped the connection\n");
            pthread_mutex_lock(&X.mu);
            fail_locked(xt("Lost the connection to the room.", "Se perdió la conexión con la sala."));
            X.run = false;
            bool joined = X.role == R_STATION;
            X.role = 0;
            pthread_mutex_unlock(&X.mu);
            if (joined) ldn_x_lost(DR_SIGNAL_LOST);  // (the hub joined through it is gone too)
            break;
        }
        if (n) on_message(buf, n);
        if (!got) {
            if (X.state == XS_CONNECTING && plat_now_ns() - asked > 8000000000ull) {
                pthread_mutex_lock(&X.mu);
                fail_locked(xt("The room didn't answer the request to join.", "La sala no respondió al pedido de entrar."));
                X.run = false;
                pthread_mutex_unlock(&X.mu);
                break;
            }
            enet_socket_wait(h->socket, &(enet_uint32){ENET_SOCKET_WAIT_RECEIVE}, 5);
        }
    }
    // the goodbye: a disconnect, and up to a second for the room to see it
    pthread_mutex_lock(&enet_mu);
    g_peer = NULL;
    enet_peer_disconnect(peer, 0);
    pthread_mutex_unlock(&enet_mu);
    for (uint64_t end = plat_now_ns() + 1000000000ull; plat_now_ns() < end;) {
        pthread_mutex_lock(&enet_mu);
        int k = enet_host_service(h, &ev, 0);
        pthread_mutex_unlock(&enet_mu);
        if (k > 0 && ev.type == ENET_EVENT_TYPE_DISCONNECT) break;
        if (k > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        if (k <= 0) plat_sleep_until_ns(plat_now_ns() + 10000000ull);
    }
    pthread_mutex_lock(&enet_mu);
    g_host = NULL;
    enet_host_destroy(h);
    pthread_mutex_unlock(&enet_mu);
    pthread_mutex_lock(&X.mu);
    X.ip = 0;
    X.nmembers = 0;
    if (X.state == XS_IN) X.state = XS_OFF;
    pthread_mutex_unlock(&X.mu);
    LOG("[xroom] out of the room\n");
    return NULL;
}

static void stop_locked(void) {  // (X.mu held; it is released while the thread ends)
    if (!X.has_thread) return;
    X.run = false;
    pthread_t th = X.th;
    X.has_thread = false;
    pthread_mutex_unlock(&X.mu);
    pthread_join(th, NULL);
    pthread_mutex_lock(&X.mu);
}

void xroom_leave(void) {
    pthread_mutex_lock(&X.mu);
    stop_locked();
    X.state = XS_OFF;
    X.role = 0;
    pthread_mutex_unlock(&X.mu);
}

// A nickname the rooms take: 4 to 20 letters, digits, spaces, '.', '_', '-'
static bool nick_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 4 || n > 20) return false;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && !strchr(" ._-", *s)) return false;
    return true;
}

static bool xr_ready(int wait_ms) {
    XplayLive live;
    config_live_crossplay(&live);
    char host[128];
    uint16_t port;
    char nick[24];
    if (nick_ok(live.nick)) snprintf(nick, sizeof nick, "%s", live.nick);
    else {
        static char made[24];
        if (!*made) snprintf(made, sizeof made, "Hunter-%04u", (unsigned)(arc4random() % 10000));
        snprintf(nick, sizeof nick, "%s", made);
    }
    // (the room this app hosts, RoomHost: opened here, and joined at 127.0.0.1)
    char hosted[32] = "";
    if (live.host) {
        uint16_t hp = live.host_port ? (uint16_t)live.host_port : ROOM_PORT;
        if (xroomhost_start(hp, 8, live.password)) snprintf(hosted, sizeof hosted, "127.0.0.1:%u", hp);
        else {
            pthread_mutex_lock(&X.mu);
            stop_locked();
            fail_locked(xt("The room can't open on this device: its UDP port is in use by another program.",
                           "La sala no puede abrirse en este dispositivo: otro programa usa su puerto UDP."));
            pthread_mutex_unlock(&X.mu);
            return false;
        }
    } else {
        xroomhost_stop();
    }
    pthread_mutex_lock(&X.mu);
    if (!parse_address(*hosted ? hosted : live.room, host, sizeof host, &port)) {
        stop_locked();
        fail_locked(xt("No room set: type a room's address under Multiplayer.",
                       "No hay sala: escribe la dirección de una sala en Multijugador."));
        pthread_mutex_unlock(&X.mu);
        return false;
    }
    bool same = !strcmp(host, X.host) && port == X.port && !strcmp(live.password, X.password) && !strcmp(nick, X.nick);
    // (a failed room is tried again at the next search or hub: the player may have opened it since)
    if (!same || X.state == XS_FAILED || !X.has_thread) {
        stop_locked();
        snprintf(X.host, sizeof X.host, "%s", host);
        X.port = port;
        snprintf(X.password, sizeof X.password, "%s", live.password);
        snprintf(X.nick, sizeof X.nick, "%s", nick);
        X.state = XS_CONNECTING;
        X.error[0] = 0;
        X.nmembers = 0;
        X.name[0] = 0;
        X.run = true;
        X.gen++;
        memset(g_num, 0, sizeof g_num);  // (another room: its members' numbers are learned again)
        X.has_thread = pthread_create(&X.th, NULL, xr_thread, NULL) == 0;
        if (!X.has_thread) fail_locked(xt("The network could not start.", "La red no pudo iniciarse."));
    }
    uint64_t end = plat_now_ns() + (uint64_t)wait_ms * 1000000ull;
    while (X.state == XS_CONNECTING && plat_now_ns() < end) {
        pthread_mutex_unlock(&X.mu);
        plat_sleep_until_ns(plat_now_ns() + 20000000ull);
        pthread_mutex_lock(&X.mu);
    }
    bool in = X.state == XS_IN;
    pthread_mutex_unlock(&X.mu);
    return in;
}

static uint32_t xr_address(void) { return X.state == XS_IN ? X.ip : 0; }
static uint32_t xr_netmask(void) { return 0xFFFFFF00u; }  // (as the emulators answer: the PC's own; a broadcast is x.255)
static void xr_mac(uint8_t out[6]) { mac_of(X.ip, out); }

static uint32_t xr_create(uint8_t *info, const uint8_t *sec, const uint8_t *user) {
    (void)sec, (void)user;
    pthread_mutex_lock(&X.mu);
    if (X.state != XS_IN) { pthread_mutex_unlock(&X.mu); return R_INVALID_STATE; }
    memcpy(X.info, info, NETINFO_SIZE);
    memset(X.client, 0, sizeof X.client);
    X.lcv = rd16le(info + NI_NODES + NODE_LCV);
    X.role = R_HOST;
    for (int i = 1; i < MAX_NODES; i++) X.info[NI_NODES + i * NODE_SIZE + NODE_ID] = (uint8_t)i;
    memcpy(info, X.info, NETINFO_SIZE);
    pthread_mutex_unlock(&X.mu);
    LOG("[xroom] hosting a hub in the room as %s\n", ipstr(X.ip));
    return 0;
}

static void xr_update(const uint8_t *info) {
    pthread_mutex_lock(&X.mu);
    if (X.role == R_HOST) {
        // (the stations' nodes are ours: only the rest comes from ldn.c)
        uint8_t nodes[MAX_NODES * NODE_SIZE];
        memcpy(nodes, X.info + NI_NODES, sizeof nodes);
        memcpy(X.info, info, NETINFO_SIZE);
        memcpy(X.info + NI_NODES + NODE_SIZE, nodes + NODE_SIZE, (MAX_NODES - 1) * NODE_SIZE);
        host_sync_locked();
    }
    pthread_mutex_unlock(&X.mu);
}

static void xr_destroy(void) {
    pthread_mutex_lock(&X.mu);
    if (X.role == R_HOST)
        for (int i = 1; i < MAX_NODES; i++)
            if (X.client[i]) send_ldn(L_DESTROY, X.client[i], false, NULL, 0);
    memset(X.client, 0, sizeof X.client);
    X.role = 0;
    pthread_mutex_unlock(&X.mu);
}

static void xr_reject(uint32_t ip) {
    uint8_t info[NETINFO_SIZE];
    bool changed = false;
    pthread_mutex_lock(&X.mu);
    for (int i = 1; i < MAX_NODES; i++)
        if (X.role == R_HOST && X.client[i] == ip) {
            send_ldn(L_DESTROY, ip, false, NULL, 0);
            host_drop_locked(i);
            memcpy(info, X.info, NETINFO_SIZE);
            changed = true;
        }
    pthread_mutex_unlock(&X.mu);
    if (changed) ldn_x_info(info);
}

static int xr_scan(const uint8_t *filter, uint8_t (*infos)[NETINFO_SIZE], int max) {
    (void)filter;  // (ldn.c applies it)
    pthread_mutex_lock(&X.mu);
    if (X.state != XS_IN) { pthread_mutex_unlock(&X.mu); return 0; }
    X.nfound = 0;
    X.scanning = true;
    send_ldn(L_SCAN, 0, true, NULL, 0);
    pthread_mutex_unlock(&X.mu);
    plat_sleep_until_ns(plat_now_ns() + 1000000000ull);  // (the emulators wait a second for the answers too)
    pthread_mutex_lock(&X.mu);
    X.scanning = false;
    int n = X.nfound < max ? X.nfound : max;
    memcpy(infos, X.found, (size_t)n * NETINFO_SIZE);
    pthread_mutex_unlock(&X.mu);
    return n;
}

static uint32_t xr_connect(const uint8_t *info, const uint8_t *node, uint8_t *out_info) {
    uint32_t host = rd32le(info + NI_NODES + NODE_IP);
    uint16_t their = rd16le(info + NI_NODES + NODE_LCV), ours = rd16le(node + NODE_LCV);
    pthread_mutex_lock(&X.mu);
    if (X.state != XS_IN) { pthread_mutex_unlock(&X.mu); return R_CONNECT_FAILURE; }
    const Member *m = member_at(host);
    char nick[40], version[40];
    snprintf(nick, sizeof nick, "%s", m ? m->nick : ipstr(host));
    snprintf(version, sizeof version, "%s", m && *m->version ? m->version : "?");
    if ((m && other_version(m)) || their != ours) {
        pthread_mutex_unlock(&X.mu);
        LOG("[xroom] the hub of %s is another version of the game (%s, local play version %u, ours %u)\n", nick, version,
            their, ours);
        wrong_version_notice(nick, version, true);
        return their < ours ? R_VERSION_TOO_LOW : their > ours ? R_VERSION_TOO_HIGH : R_CONNECT_REJECTED;
    }
    X.role = R_STATION;
    X.host_ip = host;
    X.synced = false;
    X.lcv = ours;
    send_ldn(L_CONNECT, host, false, node, NODE_SIZE);
    uint64_t end = plat_now_ns() + 5000000000ull;
    while (X.role == R_STATION && !X.synced && plat_now_ns() < end) {
        pthread_mutex_unlock(&X.mu);
        plat_sleep_until_ns(plat_now_ns() + 10000000ull);
        pthread_mutex_lock(&X.mu);
    }
    bool ok = X.role == R_STATION && X.synced;
    if (ok) memcpy(out_info, X.info, NETINFO_SIZE);
    else if (X.role == R_STATION) {
        send_ldn(L_DISCONNECT, host, false, node, NODE_SIZE);
        X.role = 0;
    }
    pthread_mutex_unlock(&X.mu);
    LOG("[xroom] joining %s's hub: %s\n", nick, ok ? "in" : "no answer, or it took nobody");
    return ok ? 0 : R_CONNECT_TIMEOUT;
}

static void xr_disconnect(void) {
    pthread_mutex_lock(&X.mu);
    if (X.role == R_STATION) {
        uint8_t node[NODE_SIZE] = {0};
        for (int i = 0; i < MAX_NODES; i++) {
            const uint8_t *nd = X.info + NI_NODES + i * NODE_SIZE;
            if (rd32le(nd + NODE_IP) == X.ip) memcpy(node, nd, NODE_SIZE);
        }
        send_ldn(L_DISCONNECT, X.host_ip, false, node, NODE_SIZE);
    }
    X.role = 0;
    pthread_mutex_unlock(&X.mu);
}

static void xr_send(const VPacket *p, const void *data, uint32_t len) {
    if (p->proto != VPROTO_UDP || len > DATA_MAX || X.state != XS_IN) return;
    static __thread uint8_t *z;
    if (!z && !(z = malloc(DATA_MAX + 64))) return;
    size_t zn = zframe(z, data, len);
    bool bcast = p->dst_ip == 0xFFFFFFFFu || (p->dst_ip & 0xFF) == 0xFF;
    // (the protocol byte: above seen_twice. p->pad: the game socket's protocol, vsock.c)
    uint8_t one = 0;  // 0: both 3 and 4
    if (!p->pad) one = 0xFF;  // (the socket has none: 0 for everyone)
    else if (!bcast) one = peer_udp(p->dst_ip);
    else {
        pthread_mutex_lock(&X.mu);
        for (int i = 0, first = 1; i < X.nmembers; i++) {
            if (X.members[i].ip == X.ip) continue;
            uint8_t u = peer_udp(X.members[i].ip);
            if (!u || (!first && u != one)) { one = 0; break; }
            one = u, first = 0;
        }
        pthread_mutex_unlock(&X.mu);
    }
    for (int pass = 0; pass < 2; pass++) {
        uint8_t proto = one == 0xFF ? 0 : one ? one : (uint8_t)(3 + pass);
        Pk k = {0};
        pk_u8(&k, M_PROXY);
        pk_u8(&k, 1);
        pk_ip(&k, X.ip);
        pk_u16(&k, p->src_port);
        pk_u8(&k, 1);
        pk_ip(&k, p->dst_ip);
        pk_u16(&k, p->dst_port);
        pk_u8(&k, proto);
        pk_u8(&k, bcast);
        pk_vec(&k, z, zn);
        send_pk(&k);
        if (one) break;
    }
}

const XBackend g_xroom = {"room", xr_ready, xr_address, xr_netmask, xr_mac, xr_create, xr_update, xr_destroy, xr_reject,
                          xr_scan, xr_connect, xr_disconnect, xr_send};

void xroom_status(XStatus *o) {
    memset(o, 0, sizeof *o);
    pthread_mutex_lock(&X.mu);
    o->state = X.state;
    snprintf(o->error, sizeof o->error, "%s", X.error);
    snprintf(o->room, sizeof o->room, "%s", *X.name ? X.name : X.host);
    o->members = X.state == XS_IN ? X.nmembers : -1;
    o->slots = X.state == XS_IN ? (int)X.slots : -1;
    snprintf(o->nick, sizeof o->nick, "%s", X.nick);
    for (int i = 0; i < X.nmembers; i++)
        if (other_version(&X.members[i]) && o->other_version++ == 0)
            snprintf(o->other_detail, sizeof o->other_detail, "%s (%s)", X.members[i].nick, X.members[i].version);
    pthread_mutex_unlock(&X.mu);
}

bool xroom_chat_send(const char *line) {
    if (X.state != XS_IN) return false;
    Pk p = {0};
    pk_u8(&p, M_CHAT);
    pk_str(&p, line);
    send_pk(&p);
    return true;
}

int xroom_chat_take(char (*lines)[600], int max) {
    pthread_mutex_lock(&X.mu);
    int n = X.nchat < max ? X.nchat : max;
    for (int i = 0; i < n; i++) memcpy(lines[i], X.chat[i], sizeof X.chat[0]);
    memmove(X.chat[0], X.chat[n], sizeof X.chat[0] * (size_t)(X.nchat - n));
    X.nchat -= n;
    pthread_mutex_unlock(&X.mu);
    return n;
}
