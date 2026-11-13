// MHGU's side of the port's chat (runtime/chat/chat.h; the menu's Chat and Friends pages are settings_panel.cpp's):
//   - the chat started once the overlay draws, with what this port gives it: the hunter's name, the hub (runtime/net/
//     ldn.c), the app's files folder for chat.txt, Android's ChatSocket for the relays
//   - the hub's lines, after tabarong's design (PORT_CHAT.md): an "MHPC" message in the link's F_AUX frame, which the hub's
//     host passes on to the other games. Each line is signed with the player's chat key and bound to the hub (its id, a
//     hash of the session's, the same in every game of it), so a line can't be made to come from someone else; the host
//     passes on only lines whose signature holds. Builds without the chat ignore the message, as any F_AUX they don't know,
//     and so does a game whose hub chat is off (as it comes: a host then passes the lines on unread, as those builds do)
//   - over the game (overlay.cpp): the chat button and the last lines, runtime/chat/chat_overlay.cpp
//
// The message: "MHPC", u8 version (1), u8 name bytes, u16 text bytes, i64 unix time, the key (32), the signature (64),
// the name, the text (little endian).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "imgui.h"

#include "chat/chat.h"
#include "chat/chat_crypto.h"
#include "chat/chat_overlay.h"
#include "chat/chat_text.h"
#include "chat_port.h"
#include "config.h"
#include "logwriter.h"
#include "net/discord.h"
#include "net/net.h"
#include "overlay.h"
#include "settings_panel.h"
#include "touch.h"

namespace {
constexpr uint8_t VERSION = 1;
constexpr size_t HEAD = 4 + 1 + 1 + 2 + 8 + 32 + 64;
bool g_started;

std::string hub_id() {
    int n = 0, max = 0;
    char id[64] = "";
    return ldn_party(&n, &max, id, sizeof id) ? std::string(id) : std::string();
}
bool hub_up(std::string *label) {
    int n = 0, max = 0;
    char id[64];
    if (!ldn_party(&n, &max, id, sizeof id)) return false;
    if (label) {
        char b[64];
        snprintf(b, sizeof b, "%d / %d", n, max);
        *label = b;
    }
    return true;
}
bool hub_send(const std::string &text) {
    std::string id = hub_id();
    if (id.empty()) return false;
    std::string name = chat::name();
    if (name.size() > 64) name.resize(64);
    int64_t at = (int64_t)time(nullptr);
    uint8_t pub[32], sig[64];
    if (!chat::hub_sign(id, at, name, text, pub, sig)) return false;
    std::vector<uint8_t> m(HEAD + name.size() + text.size());
    memcpy(&m[0], "MHPC", 4);
    m[4] = VERSION, m[5] = (uint8_t)name.size();
    m[6] = (uint8_t)(text.size() & 255), m[7] = (uint8_t)(text.size() >> 8);
    for (int i = 0; i < 8; i++) m[8 + i] = (uint8_t)((uint64_t)at >> (8 * i));
    memcpy(&m[16], pub, 32);
    memcpy(&m[48], sig, 64);
    memcpy(&m[HEAD], name.data(), name.size());
    memcpy(&m[HEAD + name.size()], text.data(), text.size());
    ldn_send_aux(m.data(), (uint32_t)m.size());
    chat::hub_line(chatc::hex(pub, 32), name, text, true);
    return true;
}
std::string hunter_name() {
    char n[64];
    return presence_hunter_name(n, sizeof n) ? std::string(n) : std::string();
}
void chat_log(const char *s) { rt_logf("%s\n", s); }
}  // namespace

extern "C" bool chat_aux_received(const void *data, uint32_t len) {
    const uint8_t *m = (const uint8_t *)data;
    if (len < 4 || memcmp(m, "MHPC", 4)) return true;  // (not the chat's)
    // (the hub's chat off, as it comes: the line is not read, and a host passes it on unread as 0.1.9.3 passed every
    // message - each game that reads it checks it)
    if (!chat::hub_chat()) return true;
    if (len < HEAD || m[4] != VERSION) return false;
    size_t nn = m[5], tn = m[6] | (size_t)m[7] << 8;
    if (HEAD + nn + tn != len || !tn || tn > 600) return false;
    int64_t at = 0;
    for (int i = 0; i < 8; i++) at |= (int64_t)m[8 + i] << (8 * i);
    std::string name((const char *)m + HEAD, nn), text((const char *)m + HEAD + nn, tn), id = hub_id();
    if (id.empty() || !chat::hub_verify(id, at, name, text, m + 16, m + 48)) {
        rt_logf("[chat] a hub line whose signature fails: dropped\n");
        return false;
    }
    chat::hub_line(chatc::hex(m + 16, 32), name, text, false);
    return true;
}

// The chat started, once (the overlay's first frame: the language, the folder and the app's classes are there by then)
extern "C" void chat_port_init(void) {
    if (g_started) return;
    g_started = true;
    chat::Port p;
    p.game = "mhgu";
#ifdef __ANDROID__
    p.app = "MHGU for Android";
    p.java_class = "com/mhgu/recomp/ChatSocket";
#else
    p.app = "MHGU for PC";
#endif
    p.dir = "";  // (the runtime's working folder: the settings' own, mhgurecomp.ini's)
    p.hub_up = hub_up;
    p.hub_send = hub_send;
    p.default_name = hunter_name;
    p.log = chat_log;
    chat::init(p);
    chat::OverlayPort o;
    o.controls_shown = []() { return touch_drawn(); };
    o.menu_open = []() { return overlay_menu_open(); };
    o.open_chat = []() {  // (the menu, on its chat page)
        if (!overlay_menu_open()) overlay_toggle_settings();
        settings_panel_request_view(SETTINGS_VIEW_CHAT);
    };
    o.log = chat_log;
    chat::overlay_setup(o);
}

// S34: how to play together, where it starts, once the player has turned a chat on (both start off, and these notices
// with them: 0.1.9.3 had none). In the Hub (or the Hunters' Pub) with no hub up, once a session, until the player has
// been in a hub: ZL, Connect, Create Hub or Search Hubs; the first hub with the hub's chat on: the quests at the counter
// and the chat (chat.txt's flags hubbeen, hubseen)
static void play_together_notices() {
    static bool told, was_up;
    static uint64_t since;
    const bool up = ldn_net_up();
    if (up && !was_up) {
        chat::set_flag("hubbeen");  // (the way to a hub needs no more telling)
        if (chat::hub_chat() && !chat::flag("hubseen")) {
            chat::set_flag("hubseen");
#ifdef __ANDROID__
            overlay_notice(chat::t("In a hub", "En una sala"),
                           chat::t("Post a quest at the counter, or join one posted there. The chat button talks to everyone in "
                                   "the hub; gear, Multiplayer, Chat for the rest.",
                                   "Publica una misión en el mostrador, o únete a una publicada ahí. El botón del chat habla con "
                                   "todos los de la sala; engranaje, Multijugador, Chat para lo demás."));
#else
            overlay_notice(chat::t("In a hub", "En una sala"),
                           chat::t("Post a quest at the counter, or join one posted there. To talk to everyone in the hub: F1, "
                                   "Multiplayer, Chat.",
                                   "Publica una misión en el mostrador, o únete a una publicada ahí. Para hablar con todos los de "
                                   "la sala: F1, Multijugador, Chat."));
#endif
        }
    }
    was_up = up;
    if (told || up || chat::flag("hubseen") || chat::flag("hubbeen")) return;
    const int town = presence_town();
    const uint64_t now = SDL_GetTicks();
    if (town != 0 && town != 7) {
        since = 0;
        return;
    }
    if (!since) since = now;
    if (now - since < 5000 || overlay_menu_open()) return;  // (a few seconds in, the area loaded)
    told = true;
#ifdef __ANDROID__
    overlay_notice(chat::t("Playing together", "Jugar juntos"),
                   chat::t("Tap ZL and choose Connect: Create Hub to host, Search Hubs to join one. To meet only your friends, "
                           "use the same matchmaking key: gear, Multiplayer (Generate a key, then share it in the chat).",
                           "Toca ZL y elige Connect: Create Hub para crear una sala, Search Hubs para unirte. Para encontrarte "
                           "solo con tus amigos, usen la misma clave de emparejamiento: engranaje, Multijugador (Generar una "
                           "clave, y compártela en el chat)."));
#else
    overlay_notice(chat::t("Playing together", "Jugar juntos"),
                   chat::t("Press ZL (Tab) and choose Connect: Create Hub to host, Search Hubs to join one. To meet only your "
                           "friends, use the same matchmaking key: F1, Multiplayer (Generate a key, then share it in the chat).",
                           "Pulsa ZL (Tab) y elige Connect: Create Hub para crear una sala, Search Hubs para unirte. Para "
                           "encontrarte solo con tus amigos, usen la misma clave de emparejamiento: F1, Multijugador (Generar "
                           "una clave, y compártela en el chat)."));
#endif
    rt_logf("[chat] the notice about playing together (the Hub, no hub up)\n");
}

// ---- over the game: the shared part (runtime/chat/chat_overlay.cpp), with this port's controls and menu
extern "C" bool chat_port_frame(void) {
    if (!g_started) return false;
    // (both chats off, as they come: no hub watched, no notice, nothing over the game - so the overlay reuses its frames
    // with the touch controls alone, hubs included, as in 0.1.9.3)
    if (!chat::active()) return chat::overlay_frame();
    chat::set_spanish(config_language() == 1);
    chat::tick();
    play_together_notices();
    return chat::overlay_frame();
}
extern "C" void chat_port_draw(float w, float h) { chat::overlay_draw(w, h); }
extern "C" bool chat_bubble_down(float fx, float fy) { return chat::bubble_down(fx, fy); }
extern "C" void chat_bubble_finger(bool up, float fx, float fy) { chat::bubble_finger(up, fx, fy); }
extern "C" void chat_bubble_size(float s) { chat::bubble_size(s); }
extern "C" float chat_bubble_scale(void) { return chat::bubble_scale(); }
extern "C" void chat_bubble_reset(void) { chat::bubble_reset(); }
