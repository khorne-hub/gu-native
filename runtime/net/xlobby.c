// The public list of the yuzu family's rooms, for crossplay (net.h, xroom.c): the community lobby those emulators
// list their public rooms in (XLOBBY_HOST, the one Eden, Citron and Torzu use since yuzu's own is gone). Asked over
// plain HTTP, without any account: GET /lobby with "api-version: 1" answers {"rooms": [{"name", "address", "port",
// "netVersion", "hasPassword", "maxPlayers", "preferredGameId", "players": [{"nickname", "gameId", ...}], ...}]}.
// Only rooms of the yuzu family's protocol (netVersion 1) where this game is played or wanted are kept.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"
#include "../platform.h"
#include "net.h"

#ifdef _WIN32
typedef SOCKET lsock;
#define LBAD INVALID_SOCKET
#define lclose closesocket
#else
typedef int lsock;
#define LBAD (-1)
#define lclose close
#endif

#define RESPONSE_MAX (4 << 20)

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int g_state;  // XL_*
static XLobbyRoom g_rooms[XLOBBY_MAX];
static int g_n;

// ---- a small JSON reader: just what the lobby's answer needs (objects, arrays, strings, numbers, true/false/null)
typedef struct { const char *p, *end; } J;
static void ws(J *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++; }
static bool lit(J *j, char c) { ws(j); if (j->p < j->end && *j->p == c) { j->p++; return true; } return false; }
static bool skip(J *j, int depth);
// A string into out (UTF-8 as is; escapes: the usual ones, \u as '?'), or skipped (out NULL)
static bool str(J *j, char *out, size_t size) {
    ws(j);
    if (j->p >= j->end || *j->p != '"') return false;
    j->p++;
    size_t k = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char e = *j->p++;
            c = e == 'n' ? ' ' : e == 't' ? ' ' : e == 'u' ? '?' : e;
            if (e == 'u') j->p += j->p + 4 <= j->end ? 4 : 0;
        }
        if (out && k + 1 < size) out[k++] = c;
    }
    if (out && size) out[k] = 0;
    if (j->p >= j->end) return false;
    j->p++;
    return true;
}
static bool num(J *j, double *v) {
    ws(j);
    char *e;
    char buf[40];
    size_t n = 0;
    while (j->p + n < j->end && n < sizeof buf - 1 && strchr("+-0123456789.eE", j->p[n])) n++;
    if (!n) return false;
    memcpy(buf, j->p, n);
    buf[n] = 0;
    *v = strtod(buf, &e);
    j->p += n;
    return true;
}
static bool skip(J *j, int depth) {
    if (depth > 32) return false;
    ws(j);
    if (j->p >= j->end) return false;
    char c = *j->p;
    if (c == '"') return str(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (lit(j, close)) return true;
        do {
            if (c == '{' && (!str(j, NULL, 0) || !lit(j, ':'))) return false;
            if (!skip(j, depth + 1)) return false;
        } while (lit(j, ','));
        return lit(j, close);
    }
    if (c == 't' || c == 'f' || c == 'n') {
        while (j->p < j->end && *j->p >= 'a' && *j->p <= 'z') j->p++;
        return true;
    }
    double v;
    return num(j, &v);
}
static bool jbool(J *j) {
    ws(j);
    bool t = j->p < j->end && *j->p == 't';
    skip(j, 0);
    return t;
}

// One room of the list (the object at j)
static bool room(J *j, XLobbyRoom *r, int *net_version, uint64_t *preferred, int *mhgu_players) {
    memset(r, 0, sizeof *r);
    *net_version = -1, *preferred = 0, *mhgu_players = 0;
    if (!lit(j, '{')) return false;
    if (lit(j, '}')) return true;
    do {
        char key[40];
        double v = 0;
        if (!str(j, key, sizeof key) || !lit(j, ':')) return false;
        if (!strcmp(key, "name")) { if (!str(j, r->name, sizeof r->name)) return false; }
        else if (!strcmp(key, "address")) { if (!str(j, r->address, sizeof r->address)) return false; }
        else if (!strcmp(key, "port")) { if (!num(j, &v)) return false; r->port = (int)v; }
        else if (!strcmp(key, "netVersion")) { if (!num(j, &v)) return false; *net_version = (int)v; }
        else if (!strcmp(key, "maxPlayers")) { if (!num(j, &v)) return false; r->slots = (int)v; }
        else if (!strcmp(key, "hasPassword")) r->password = jbool(j);
        else if (!strcmp(key, "preferredGameId")) { if (!num(j, &v)) return false; *preferred = (uint64_t)v; }
        else if (!strcmp(key, "players")) {
            if (!lit(j, '[')) return false;
            if (!lit(j, ']')) {
                do {
                    if (!lit(j, '{')) return false;
                    r->players++;
                    if (!lit(j, '}')) {
                        do {
                            char k2[40];
                            if (!str(j, k2, sizeof k2) || !lit(j, ':')) return false;
                            if (!strcmp(k2, "gameId")) {
                                if (!num(j, &v)) return false;
                                if ((uint64_t)v == XPLAY_TITLE_ID) (*mhgu_players)++;
                            } else if (!skip(j, 2)) return false;
                        } while (lit(j, ','));
                        if (!lit(j, '}')) return false;
                    }
                } while (lit(j, ','));
                if (!lit(j, ']')) return false;
            }
        } else if (!skip(j, 1)) return false;
    } while (lit(j, ','));
    return lit(j, '}');
}

// The rooms of the answer's JSON that suit this game: count
static int parse(const char *text, size_t n, XLobbyRoom *out, int max) {
    J j = {text, text + n};
    int k = 0;
    if (!lit(&j, '{')) return -1;
    do {
        char key[16];
        if (!str(&j, key, sizeof key) || !lit(&j, ':')) return -1;
        if (strcmp(key, "rooms")) { if (!skip(&j, 1)) return -1; continue; }
        if (!lit(&j, '[')) return -1;
        if (lit(&j, ']')) continue;
        do {
            XLobbyRoom r;
            int version, players;
            uint64_t preferred;
            if (!room(&j, &r, &version, &preferred, &players)) return -1;
            // (this game's id fits a double exactly: its bits span 41 places)
            bool ours = players > 0 || preferred == XPLAY_TITLE_ID;
            if (version == 1 && ours && *r.address && r.port > 0 && k < max) {
                r.this_game = players;
                out[k++] = r;
            }
        } while (lit(&j, ','));
        if (!lit(&j, ']')) return -1;
    } while (lit(&j, ','));
    return k;
}

static int sort_rooms(const void *a, const void *b) {  // (players of this game first, then free places, then the name)
    const XLobbyRoom *x = a, *y = b;
    if (x->this_game != y->this_game) return y->this_game - x->this_game;
    bool xf = x->players < x->slots, yf = y->players < y->slots;
    if (xf != yf) return yf - xf;
    return strcmp(x->name, y->name);
}

static void *fetch_thread(void *arg) {
    (void)arg;
    plat_thread_name("xlobby");
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    int state = XL_FAILED, n = 0;
    static XLobbyRoom rooms[XLOBBY_MAX];
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char *buf = NULL;
    lsock s = LBAD;
    if (getaddrinfo(XLOBBY_HOST, "80", &hints, &res) || !res) {
        LOG("[xlobby] %s not found\n", XLOBBY_HOST);
        goto done;
    }
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    DWORD tv = 8000;
#else
    struct timeval tv = {8, 0};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    if (s == LBAD || connect(s, res->ai_addr, (int)res->ai_addrlen)) {
        LOG("[xlobby] can't reach %s\n", XLOBBY_HOST);
        goto done;
    }
    char req[256];
    int rl = snprintf(req, sizeof req,
                      "GET /lobby HTTP/1.0\r\nHost: %s\r\napi-version: 1\r\nAccept: application/json\r\nUser-Agent: MHGU-port\r\n\r\n",
                      XLOBBY_HOST);
    if (send(s, req, rl, 0) != rl) goto done;
    buf = malloc(RESPONSE_MAX + 1);
    size_t got = 0;
    int k;
    while (buf && got < RESPONSE_MAX && (k = recv(s, buf + got, (int)(RESPONSE_MAX - got), 0)) > 0) got += (size_t)k;
    if (!buf || !got) goto done;
    buf[got] = 0;
    char *body = strstr(buf, "\r\n\r\n");
    int status = 0;
    sscanf(buf, "HTTP/%*s %d", &status);
    if (status != 200 || !body) {
        LOG("[xlobby] %s answered HTTP %d\n", XLOBBY_HOST, status);
        goto done;
    }
    body += 4;
    n = parse(body, got - (size_t)(body - buf), rooms, XLOBBY_MAX);
    if (n < 0) {
        LOG("[xlobby] %s's answer isn't a list of rooms\n", XLOBBY_HOST);
        n = 0;
        goto done;
    }
    qsort(rooms, (size_t)n, sizeof rooms[0], sort_rooms);
    state = XL_DONE;
    LOG("[xlobby] %d public room(s) for this game on %s\n", n, XLOBBY_HOST);
done:
    if (res) freeaddrinfo(res);
    if (s != LBAD) lclose(s);
    free(buf);
    pthread_mutex_lock(&mu);
    memcpy(g_rooms, rooms, sizeof rooms[0] * (size_t)n);
    g_n = n;
    g_state = state;
    pthread_mutex_unlock(&mu);
    return NULL;
}

void xlobby_fetch(void) {
    pthread_mutex_lock(&mu);
    if (g_state == XL_BUSY) { pthread_mutex_unlock(&mu); return; }
    g_state = XL_BUSY;
    pthread_mutex_unlock(&mu);
    pthread_t t;
    if (pthread_create(&t, NULL, fetch_thread, NULL)) {
        pthread_mutex_lock(&mu);
        g_state = XL_FAILED;
        pthread_mutex_unlock(&mu);
    } else {
        pthread_detach(t);
    }
}

int xlobby_rooms(XLobbyRoom *out, int max, int *state) {
    pthread_mutex_lock(&mu);
    int n = g_n < max ? g_n : max;
    memcpy(out, g_rooms, sizeof g_rooms[0] * (size_t)n);
    *state = g_state;
    pthread_mutex_unlock(&mu);
    return n;
}
