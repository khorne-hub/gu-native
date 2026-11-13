// Local play (the Switch's local wireless, nn::ldn) over IP: one player's game hosts a hub, the others join it by
// address. ldn.c is nn::ldn plus the link between the games (TCP: each client keeps one connection to the host,
// which relays); vsock.c is nn::socket on a virtual network: the sockets the game opens for its own traffic exist
// only here, their packets travel inside the link. Addresses are the ones a Switch would use (169.254.x.n, the
// host n = 1), so the game sees the network it expects.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A packet of the virtual network (fields in host byte order, as sent inside the link after the frame header).
enum { VPROTO_UDP = 0, VPROTO_TCP = 1 };
enum { VTCP_DATA = 0, VTCP_SYN, VTCP_SYNACK, VTCP_FIN, VTCP_RST };
typedef struct {
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    uint8_t proto, kind;  // VPROTO_*; for TCP VTCP_*
    uint16_t pad;         // (a datagram the game sends: its socket's protocol, 0 or 17 - xroom.c needs it; else 0)
} VPacket;

// MHGU_LOG_NET=2: every nn::ldn / nn::socket call (the first 40 of each, then every 1000th), with r0-r2.
#define NTRACE()                                                                                                   \
    do {                                                                                                           \
        const char *nt_ = ENV("MHGU_LOG_NET");                                                                     \
        static int ntn_;                                                                                           \
        if (nt_ && atoi(nt_) >= 2 && (ntn_++ < 40 || ntn_ % 1000 == 0))                                            \
            LOG("[net] %s #%d (%x, %x, %x)\n", __func__, ntn_, ARG(0), ARG(1), ARG(2));                            \
    } while (0)

// ldn.c
bool ldn_net_up(void);           // a network exists (hosting with it created, or joined)
bool ldn_hosting(void);          // we host it
// The hub's players and their maximum, and an id for it (a hash of its session id: the same in every player's
// game); false: no hub.
bool ldn_party(int *n, int *max, char *id, size_t size);
uint32_t ldn_own_ip(void);        // 0 without a network
uint32_t ldn_broadcast_ip(void);
void ldn_send(const VPacket *p, const void *data, uint32_t len);  // from a local socket: into the network
void ldn_send_aux(const void *data, uint32_t len);  // the port's own message to every other game (reliable)
// The hub server (internet hubs) for the settings menu: its last known state for these settings (ini Server, Key),
// from a search, a hosted hub or a check; ms: the answer's round trip, hubs: listed for the key (-1: not known).
enum { HUB_UNKNOWN, HUB_CHECKING, HUB_OFF, HUB_ONLINE, HUB_NOT_FOUND, HUB_UNREACHABLE, HUB_LOST, HUB_NO_ANSWER,
       HUB_BUSY, HUB_VERSION, HUB_REFUSED };
int ldn_hub_status(const char *server, const char *key, int *ms, int *hubs);  // HUB_*
// The server an ini Server names: "" the default (LDN_DEFAULT_SERVER), "Off" none (false, out empty)
bool ldn_server_name(const char *setting, char *out, size_t size);
void ldn_hub_check(const char *server, const char *key);  // asks the server for its hubs, in a thread

// vsock.c
void vsock_deliver(const VPacket *p, const void *data, uint32_t len);  // from the network to the local sockets
void vsock_peer_gone(uint32_t ip);  // a node left: its TCP connections end
void vsock_network_down(void);      // the network is gone: every connection ends, queues empty

// emsync.c: small monsters synced between the players in a zone
void emsync_tick(void);             // each frame, from the game's input polling
void emsync_aux_received(const void *data, uint32_t len);  // an F_AUX message from another game

// hle/hpscale.c: large monster HP by the number of hunters; the host's settings go to the others
void hpscale_tick(void);            // each frame, from the game's input polling
void hpscale_aux_received(const void *data, uint32_t len);

// ---- Crossplay with emulators (ini [Multiplayer] Crossplay). With it on, the local play network is not the port's own
// (the hubs above) but an emulator's online play, so players of the port meet players of emulators running the game:
//   XPLAY_ROOMS: the multiplayer rooms of the yuzu family (Eden, Citron, Sudachi, Suyu, yuzu): runtime/net/xroom.c
//   XPLAY_RYUJINX: Ryujinx's LDN servers (Ryujinx, Ryubing): runtime/net/ryuldn.c
// While it is on the game plays as the original does: the port's own additions that the others' games don't have (small
// monsters synced, monster HP settings, Village quests in the Hub) stay out (ldn_crossplay_vanilla).
enum { XPLAY_OFF, XPLAY_ROOMS, XPLAY_RYUJINX };
int ldn_crossplay(void);           // XPLAY_*: the network's while one is up, else the setting's
bool ldn_crossplay_vanilla(void);  // the game must play as the original (crossplay on)
#define XPLAY_GAME_VERSION "1.4.0"  // the only version of the game the port is (and plays with)
#define XPLAY_TITLE_ID 0x0100770008DD8000ull
#define XPLAY_GAME_NAME "Monster Hunter Generations Ultimate"

// A crossplay network (xroom.c, ryuldn.c), as ldn.c drives it. NetworkInfo / NodeInfo are the SDK's bytes (ldn.c).
typedef struct {
    const char *name;  // for the log
    // The connection to the room / server: true once in it, connecting first if needed (waits up to wait_ms)
    bool (*ready)(int wait_ms);
    uint32_t (*address)(void);    // ours in the network (host byte order), 0 none yet
    uint32_t (*netmask)(void);    // the network's (host byte order): the game broadcasts to address | ~netmask
    void (*mac)(uint8_t out[6]);  // ours
    // Hosting: info is the network ldn.c made (our address in node 0); the backend may change it (a server's own view).
    // sec, user: the game's SecurityConfig (0x44 bytes) and UserConfig (0x30)
    uint32_t (*create)(uint8_t *info, const uint8_t *sec, const uint8_t *user);  // an nn::ldn result
    void (*update)(const uint8_t *info);  // the network changed here (advertise data, accept policy)
    void (*destroy)(void);
    void (*reject)(uint32_t ip);
    // Joining
    int (*scan)(const uint8_t *filter, uint8_t (*infos)[0x480], int max);  // networks found (filter: the game's ScanFilter)
    uint32_t (*connect)(const uint8_t *info, const uint8_t *node, uint8_t *out_info);  // node: ours (0x40 bytes)
    void (*disconnect)(void);
    // A game packet into the network (from vsock.c)
    void (*send)(const VPacket *p, const void *data, uint32_t len);
} XBackend;
extern const XBackend g_xroom, g_ryuldn;
// From a backend's thread into ldn.c
void ldn_x_info(const uint8_t *info);  // the network's NetworkInfo changed (a player joined or left; the host's sync)
void ldn_x_lost(int reason);           // a joined network is gone (DR_* of nn::ldn: 3 destroyed by the host, 6 lost)
void ldn_x_packet(const VPacket *p, const void *data, uint32_t len);  // a game packet from the network
void ldn_x_peer_gone(uint32_t ip);     // a player left (its connections end)

// What the settings menu shows of a crossplay room / server (xroom_status, ryuldn_status)
enum { XS_OFF, XS_CONNECTING, XS_IN, XS_FAILED };
typedef struct {
    int state;               // XS_*
    char error[200];         // XS_FAILED: why (in the menu's language)
    char room[96];           // the room's name (rooms) / the server's address (Ryujinx)
    int members, slots;      // in the room / its places (-1 unknown)
    char nick[40];           // ours in it
    int other_version;       // members playing this game in another version
    char other_detail[160];  // the first of them: "nick (1.2.0)"
} XStatus;
void xroom_status(XStatus *out);
void ryuldn_status(XStatus *out);
void xroom_leave(void);   // out of the room (the settings changed, the app closes)
void ryuldn_leave(void);
// The room's chat (rooms only: the room relays it to every member, and emulators show it in their chat window), for
// the chat's code: a line said here reaches everyone in the room; xroom_chat_take hands the lines the room sent since
// the last call, "nick\tline" each (up to max). False / 0: not in a room.
bool xroom_chat_send(const char *line);
int xroom_chat_take(char (*lines)[600], int max);

// xlobby.c: the public rooms of the yuzu family where this game is played, from the community lobby they list them in
#define XLOBBY_HOST "api.ynet-fun.xyz"
#define XLOBBY_MAX 64
enum { XL_NONE, XL_BUSY, XL_DONE, XL_FAILED };
typedef struct {
    char name[96], address[64];
    int port, players, slots;
    int this_game;  // players playing this game
    bool password;
} XLobbyRoom;
// xroomhost.c: the room this app hosts (RoomHost)
bool xroomhost_start(uint16_t port, int slots, const char *password);  // (the same again: nothing changes)
void xroomhost_stop(void);
bool xroomhost_running(uint16_t *port, int *members);
int xroomhost_addresses(char (*out)[48], int max);  // this device's IPv4 addresses, Wi-Fi first
void xlobby_fetch(void);  // asks again, on a thread of its own (after the notice about playing online)
int xlobby_rooms(XLobbyRoom *out, int max, int *state);  // the last answer (state: XL_*)

#ifdef __cplusplus
}
#endif
