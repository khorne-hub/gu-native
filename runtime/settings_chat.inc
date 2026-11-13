// The settings menu's chat (S32) and room keys (S35), included by settings_panel.cpp in its own namespace: the port's
// chat (runtime/chat/chat.h, after tabarong's designs PORT_CHAT.md and MATCHMAKING_KEY_GENERATOR.md) on two pages of
// their own, Chat and Friends (View::Chat, View::Friends), opened from Multiplayer (chat_entry_rows); the question before
// the relays connect the first time (View::ChatOn); the matchmaking key's Generate a key and Share it in the chat. The
// MH4U port has the same pages (app_native/settings_chat.inc), with a room's address and password for the key.
// The chat's settings are its own (chat.txt beside the settings), applied at once: Save and Discard leave them. Its texts
// carry their Spanish beside them (cs here, runtime/chat/chat_text.cpp for the chat's own).

int g_chat = -1;  // the chat the page shows (FRIEND: with g_chat_peer, a friend's key); -1: not picked yet (the hub's
std::string g_chat_peer;  // while in one, else Global)
const char *cs(const char *en, const char *es) {
    chat::set_spanish((s.editing ? s.draft.language : config_language()) == 1);
    return chat::t(en, es);
}
std::string chat_hosts() {  // (the relays without wss://, for the question and the help)
    std::string all;
    for (const std::string &r : chat::relays()) all += (all.empty() ? "" : ", ") + (r.rfind("wss://", 0) == 0 ? r.substr(6) : r);
    return all;
}
std::string chat_on_detail() {
    char b[2048];
    snprintf(b, sizeof b,
             cs("The online chat (Global, General and friends) connects this device to public Nostr relays that anyone can use: "
                "%s. As with any server on the internet, they can see your IP address, and anyone can read Global and "
                "General: never share anything private there. While it is on, your chat name, your game and whether you "
                "are in a hub or room can be seen by anyone, so your friends find you. Messages to a friend are encrypted "
                "on this device. The "
                "project runs no server and there is no account: you are a key made on this device (your friend code). "
                "Nothing is saved anywhere, and the chat works only while you are connected. You can turn it off or choose "
                "other relays at any time. The hub's chat needs none of this.",
                "El chat en línea (Global, General y amigos) conecta este dispositivo a relés públicos de Nostr que cualquiera "
                "puede usar: %s. Como cualquier servidor de internet, ven tu dirección IP, y cualquiera puede leer Global y "
                "General: nunca compartas nada privado ahí. Mientras está activo, cualquiera puede ver tu nombre en el "
                "chat, tu juego y si estás en una sala, para que tus amigos te encuentren. Los mensajes a un amigo se "
                "cifran en este dispositivo. El "
                "proyecto no tiene servidores y no hay cuentas: eres una clave creada en este dispositivo (tu código de "
                "amigo). No se guarda nada en ningún lado, y el chat funciona solo mientras estás conectado. Puedes "
                "apagarlo o elegir otros relés cuando quieras. El chat de la sala no necesita nada de esto."),
             chat_hosts().c_str());
    return b;
}
// The relays on: the first time, the question says what it means
void chat_turn_on() {
    if (chat::notice_seen()) {
        chat::set_enabled(true);
        return;
    }
    push(View::ChatOn, "dialog.cancel");
}
void chat_open(int channel, const std::string &peer) {
    g_chat = channel, g_chat_peer = peer;
    if (s.view == View::Chat) focus("chat.say");
    else push(View::Chat, "chat.say");
}
// A key or room shared in the chat: where to (the hub's own players are already in it: friends online, Global, General)
void chat_share(int kind, const std::string &a, const std::string &b) {
    std::vector<std::string> names;
    std::vector<std::pair<int, std::string>> to;
    for (const chat::Person &p : chat::people())
        if (p.is_friend && p.mutual && p.online) names.push_back(fmt("%s (%s)", p.name.c_str(), cs("friend", "amigo"))), to.push_back({chat::FRIEND, p.id});
    names.push_back(chat::channel_name(chat::GENERAL)), to.push_back({chat::GENERAL, ""});
    names.push_back(chat::channel_name(chat::GLOBAL)), to.push_back({chat::GLOBAL, ""});
    Row row;
    row.id = "chat.share", row.title = cs("Share it with", "Compartirla con");
    choices(row, names, -1, [to, kind, a, b](int i) {
        std::string why;
        if (i < 0 || i >= (int)to.size()) return;
        if (!chat::share(to[i].first, to[i].second, kind, a, b, &why)) s.error = chat::explain(why);
        else chat_open(to[i].first, to[i].second);
    });
}
// S35: a new key for the matchmaking key's row: 128 random bits made here (no server asked), copied for the friends
void chat_new_key() {
    const std::string key = chat::new_key();
    SDL_strlcpy(s.draft.ldn_key, key.c_str(), sizeof s.draft.ldn_key);
    g_custom_key = key;
    const bool copied = SDL_SetClipboardText(key.c_str());
    overlay_notice(cs("New matchmaking key", "Nueva clave de emparejamiento"),
                   copied ? cs("Copied. Save, then share it in the chat (Share it in the chat) or paste it to your friends: "
                               "with the same hub server and key you see each other's hubs.",
                               "Copiada. Guarda, y compártela en el chat (Compartirla en el chat) o pégasela a tus amigos: "
                               "con el mismo servidor de salas y clave se ven las salas entre ustedes.")
                          : cs("It could not be copied: it shows in the row. Save, then share it in the chat.",
                               "No se pudo copiar: se ve en la fila. Guarda, y compártela en el chat."));
    fprintf(stderr, "[settings] a new matchmaking key was generated (%s)\n", copied ? "copied" : "not copied");
}
// The hub server a key goes with, by its name (the default one named, so a friend with another one set gets this one)
std::string chat_server_name() {
    char server[sizeof s.draft.ldn_server];
    return ldn_server_name(s.draft.ldn_server, server, sizeof server) ? std::string(server) : std::string();
}
// Multiplayer's rows under the key: share it in the chat
void chat_key_rows(std::vector<Row> &r) {
    const std::string key = s.draft.ldn_key, server = chat_server_name();
    action(r, "key.share", cs("Share it in the chat", "Compartirla en el chat"),
           [key, server]() {
               if (!chat::enabled()) {
                   chat_turn_on();
                   return;
               }
               chat_share(chat::SHARE_KEY, server, key);
           },
           !key.empty());
    r.back().help = cs("Sends the hub server and the matchmaking key above to a friend online, or to General or Global, as a "
                       "line they can use with one tap. With the same server and key, you see each other's hubs. Generate "
                       "a key in the key's row.",
                       "Manda el servidor de salas y la clave de emparejamiento de arriba a un amigo en línea, o a General o "
                       "Global, como una línea que se usa con un toque. Con el mismo servidor y clave, se ven las salas entre "
                       "ustedes. Genera una clave en la fila de la clave.");
}
// A key from a line of the chat: in the draft (Save keeps it), on the Multiplayer page
void chat_use_share(const chat::Line &l) {
    if (l.share != chat::SHARE_KEY) return;
    SDL_strlcpy(s.draft.ldn_key, l.share_b.c_str(), sizeof s.draft.ldn_key);
    if (!l.share_a.empty()) SDL_strlcpy(s.draft.ldn_server, l.share_a.c_str(), sizeof s.draft.ldn_server);
    first_page(NET_PAGE);
    focus("key");
    overlay_notice(cs("Matchmaking key", "Clave de emparejamiento"),
                   cs("Set from the chat: Save to keep it. Then the hubs with this key show in the game's search.",
                      "Puesta desde el chat: Guarda para conservarla. Así las salas con esta clave salen en la búsqueda del juego."));
}

// The relays' row: their state (a tap turns them on or off), the players online
void chat_status_row(std::vector<Row> &r) {
    int up = 0, total = 0;
    const chat::Status st = chat::status(&up, &total);
    Row row;
    row.id = "chat.status", row.title = cs("Online chat", "Chat en línea");
    row.value = chat::status_text(st, up, total);
    if (st == chat::ONLINE) row.value += fmt(" - %d %s", chat::online_count(), cs("online", "en línea"));
    row.toggle = st == chat::UNAVAILABLE ? -1 : chat::enabled() ? 1 : 0;
    row.warn = st == chat::NO_RELAY;
    row.enabled = st != chat::UNAVAILABLE;
    row.help = std::string(cs("Global, General and your friends travel through public relays (", "Global, General y tus amigos viajan por relés públicos (")) +
               chat_hosts() + cs("), only while this is on. The hub's chat needs no relay.", "), solo mientras esto está activo. El chat de la sala no necesita relés.");
    if (st == chat::NO_RELAY && !chat::status_reason().empty()) row.help += "\n" + chat::status_reason();
    row.activate = []() {
        if (chat::enabled()) chat::set_enabled(false);
        else chat_turn_on();
    };
    row.adjust = [](int d) {
        if (d > 0 && !chat::enabled()) chat_turn_on();
        if (d < 0) chat::set_enabled(false);
    };
    r.push_back(std::move(row));
}
// The chat page: which chat, Say something, its last lines (the sender's name over each, as tabarong's panel), the people
// in it (a tap: add as a friend, mute, block), and the chat's options
void chat_page_rows(std::vector<Row> &r) {
    cs("", "");  // (the language)
    std::vector<chat::Person> people = chat::people();
    if (g_chat < 0) g_chat = chat::hub_up() ? chat::HUB : chat::GLOBAL;
    bool found = g_chat != chat::FRIEND;
    for (const chat::Person &p : people)
        if (g_chat == chat::FRIEND && p.id == g_chat_peer && p.is_friend) found = true;
    if (!found) g_chat = chat::GLOBAL, g_chat_peer.clear();
    chat_status_row(r);
    {  // the chat shown: Global, General, the hub, each friend
        std::vector<std::string> names;
        std::vector<std::pair<int, std::string>> to;
        int at = 0;
        for (int c : {chat::HUB, chat::GENERAL, chat::GLOBAL}) {
            int n = chat::unread(c);
            names.push_back(chat::channel_name(c) + (n ? fmt(" (%d)", n) : std::string()));
            to.push_back({c, ""});
            if (g_chat == c) at = (int)to.size() - 1;
        }
        for (const chat::Person &p : people)
            if (p.is_friend) {
                int n = chat::unread(chat::FRIEND, p.id);
                names.push_back(std::string(cs("Friend: ", "Amigo: ")) + p.name + (n ? fmt(" (%d)", n) : std::string()));
                to.push_back({chat::FRIEND, p.id});
                if (g_chat == chat::FRIEND && g_chat_peer == p.id) at = (int)to.size() - 1;
            }
        add_choice(r, "chat.channel", "", at, names, [to](int i) {
            if (i >= 0 && i < (int)to.size()) g_chat = to[i].first, g_chat_peer = to[i].second;
        });
        r.back().title = cs("Chat", "Chat");
        r.back().help = chat::channel_help(g_chat);
    }
    std::string label;
    const bool hub = chat::hub_up(&label), relays = chat::enabled();
    const bool can = g_chat == chat::HUB ? hub : relays;
    action(r, "chat.say", cs("Say something", "Decir algo"), []() {
        const int ch = g_chat;
        const std::string peer = g_chat_peer;
        keyboard(cs("Say something", "Decir algo"), "", chat::max_text(), [ch, peer](const std::string &typed, std::string &error) {
            std::string why;
            if (typed.empty() || chat::say(ch, peer, typed, &why)) return true;
            error = chat::explain(why);
            return false;
        });
    }, can);
    r.back().help = fmt("%s %d %s", cs("Up to", "Hasta"), (int)chat::max_text(), cs("characters. The keyboard's Enter sends it.", "caracteres. El Enter del teclado lo manda."));
    if (!can)
        note(r, "chat.why", g_chat == chat::HUB ? cs("Not in a hub: in the game, go to the Gathering Hall and create or join one.",
                                                     "No estás en una sala: en el juego, ve al Salón de reuniones y crea o únete a una.")
                                                : cs("Turn on the online chat above to read and write here.",
                                                     "Activa el chat en línea arriba para leer y escribir acá."));
    std::vector<chat::Line> lines = chat::lines(g_chat, g_chat_peer, 30);
    chat::mark_read(g_chat, g_chat_peer);
    if (lines.empty() && can) note(r, "chat.none", cs("Nothing has been said yet.", "Todavía no se dijo nada."));
    std::vector<std::pair<std::string, std::string>> senders;  // (the people in the lines: id, name)
    for (const chat::Line &l : lines) {
        note(r, "chat.line." + std::to_string(l.serial), chat::line_text(l));
        Row &n = r.back();
        if (!l.system) {
            n.who = l.mine ? std::string(cs("You", "Tú")) : l.name;
            if (g_chat == chat::GLOBAL && !l.game.empty()) n.who += l.game == "mh4u" ? "  (MH4U)" : "  (MHGU)";
            n.who_mine = l.mine;
        }
        if (l.share == chat::SHARE_KEY && !l.mine) {
            chat::Line copy = l;
            action(r, "chat.use." + std::to_string(l.serial), cs("Use this key", "Usar esta clave"), [copy]() { chat_use_share(copy); });
        }
        if (!l.mine && !l.system && l.from != chat::my_key()) {
            bool seen = false;
            for (auto &x : senders) seen |= x.first == l.from;
            if (!seen) senders.push_back({l.from, l.name});
        }
    }
    if (!senders.empty() && g_chat != chat::FRIEND) {
        std::vector<std::string> names;
        for (auto &x : senders) names.push_back(x.second);
        Row row;
        row.id = "chat.people", row.title = cs("Someone in this chat", "Alguien de este chat");
        row.value = fmt("%d", (int)senders.size());
        row.help = cs("Add one of them as a friend, mute them (their lines no longer show here) or block them (nothing of "
                      "theirs reaches you). Only you are affected, on this device.",
                      "Agrega a alguno como amigo, sílencialo (sus líneas ya no se ven acá) o bloquéalo (no te llega nada "
                      "suyo). Solo te afecta a ti, en este dispositivo.");
        row.menu = true;
        Row copy = row;
        row.activate = [copy, names, senders]() {
            choices(copy, names, -1, [senders](int i) {
                if (i < 0 || i >= (int)senders.size()) return;
                const std::string id = senders[i].first, name = senders[i].second;
                const bool key = id.size() == 64;
                std::vector<std::string> what;
                if (key) what.push_back(cs("Add as a friend", "Agregar como amigo"));
                what.push_back(cs("Mute", "Silenciar"));
                what.push_back(cs("Block", "Bloquear"));
                Row who;
                who.id = "chat.people", who.title = name;
                choices(who, what, -1, [id, name, key](int k) {
                    if (!key) k++;
                    if (k == 0) chat::add_friend_key(id, name);
                    else if (k == 1) chat::set_muted(id, name, true);
                    else if (k == 2) chat::set_blocked(id, name, true);
                });
            });
        };
        r.push_back(std::move(row));
    }
    action(r, "chat.friends", cs("Friends", "Amigos"), []() { push(View::Friends, "friends.code"); });
    {
        int on = 0;
        for (const chat::Person &p : people) on += p.is_friend && p.online;
        r.back().value = fmt("%d %s ›", on, cs("online", "en línea"));
    }
    header(r, "chat.options", cs("Options", "Opciones"));
    add_choice(r, "chat.overlay", "", chat::overlay(),
               {cs("Off", "Apagado"), cs("Hub and friends", "Sala y amigos"), cs("All", "Todos")},
               [](int i) { chat::set_overlay(i); });
    r.back().title = cs("Lines over the game", "Líneas sobre el juego");
    r.back().help = cs("New lines show for a few seconds over the game, beside the chat button.",
                       "Las líneas nuevas se ven unos segundos sobre el juego, junto al botón del chat.");
#ifdef __ANDROID__
    {
        Row row;
        row.id = "chat.bubble", row.title = cs("Chat button", "Botón del chat");
        bool on = chat::bubble_shown();
        row.toggle = on, row.value = on ? cs("On", "Activo") : cs("Off", "Apagado");
        row.help = cs("A round button over the game while the chat is on or you are in a hub: a tap opens the chat, a drag "
                      "moves it. The number on it: lines not read yet.",
                      "Un botón redondo sobre el juego mientras el chat está activo o estás en una sala: un toque abre el "
                      "chat, arrastrarlo lo mueve. El número: líneas sin leer.");
        row.activate = []() { chat::set_bubble_shown(!chat::bubble_shown()); };
        row.adjust = [](int d) { chat::set_bubble_shown(d > 0); };
        r.push_back(std::move(row));
        add_number(r, "chat.bubble.size", cs("Chat button size", "Tamaño del botón del chat"),
                   []() { return std::round(chat_bubble_scale() * 100.0); }, [](double v) { chat_bubble_size((float)(v / 100)); },
                   60, 200, 10, true);
        r.back().value += " %";
        action(r, "chat.bubble.reset", cs("Chat button back in its place", "Botón del chat a su lugar"), []() { chat_bubble_reset(); });
    }
#endif
    {
        Row row;
        row.id = "chat.name", row.title = cs("Your name in the chat", "Tu nombre en el chat");
        row.value = chat::own_name().empty() ? chat::name() + cs(" (the hunter's)", " (el del cazador)") : chat::own_name();
        row.help = cs("Empty: your hunter's name. Up to 24 characters.", "Vacío: el nombre de tu cazador. Hasta 24 caracteres.");
        row.activate = []() {
            keyboard(cs("Your name in the chat", "Tu nombre en el chat"), chat::own_name(), 24, [](const std::string &t, std::string &) {
                chat::set_name(t);
                return true;
            });
        };
        r.push_back(std::move(row));
    }
    {
        const std::vector<std::string> def = chat::default_relays(), now = chat::relays();
        const bool custom = now != def;
        std::string list;
        for (const std::string &x : now) list += (list.empty() ? "" : " ") + x;
        add_choice(r, "chat.relays", "", custom ? 1 : 0,
                   {cs("The usual ones", "Los de siempre"), custom ? list : std::string(cs("Custom...", "Otros..."))}, [list](int i) {
                       if (i == 0) {
                           chat::set_relays({});
                           return;
                       }
                       keyboard(cs("Relays", "Relés"), list, 600, [](const std::string &t, std::string &error) {
                           std::vector<std::string> v;
                           size_t a = 0;
                           while (a < t.size()) {
                               size_t b = t.find_first_of(" ,", a);
                               std::string x = t.substr(a, b == std::string::npos ? std::string::npos : b - a);
                               if (!x.empty() && x.rfind("wss://", 0) != 0) x = "wss://" + x;
                               if (x.size() > 6) v.push_back(x);
                               if (b == std::string::npos) break;
                               a = b + 1;
                           }
                           if (v.empty()) {
                               error = cs("Type one or more relays, as wss://relay.example.org, separated by spaces.",
                                          "Escribe uno o más relés, como wss://relay.example.org, separados por espacios.");
                               return false;
                           }
                           chat::set_relays(v);
                           return true;
                       });
                   });
        r.back().title = cs("Relays", "Relés");
        r.back().help = std::string(cs("The Nostr relays the online chat goes through. The usual ones: ", "Los relés de Nostr por los que va el chat en línea. Los de siempre: ")) +
                        "relay.damus.io, nos.lol, relay.primal.net. " +
                        cs("Everyone you want to talk to needs at least one relay in common with you.",
                           "Todos con los que quieras hablar necesitan al menos un relé en común contigo.");
    }
    note(r, "chat.credit", cs("Chat and room keys after the designs of tabarong.", "Chat y claves de sala según los diseños de tabarong."));
}
// The friends page: your code (a tap copies it), add one by code, the requests, the friends (a tap: talk, share the key,
// mute, block, remove), the muted and blocked
void friends_page_rows(std::vector<Row> &r) {
    cs("", "");
    chat_status_row(r);
    {
        Row row;
        row.id = "friends.code", row.title = cs("Your friend code", "Tu código de amigo");
        row.value = chat::my_code();
        row.help = cs("Give it to a friend: they add you with it while you are both online. A tap copies it. It is made on "
                      "this device: deleting the app makes a new one.",
                      "Dáselo a un amigo: te agrega con él mientras los dos están en línea. Un toque lo copia. Se crea en este "
                      "dispositivo: borrar la app crea otro.");
        row.activate = []() {
            const std::string c = chat::my_code();
            if (SDL_SetClipboardText(c.c_str())) overlay_notice(cs("Friend code", "Código de amigo"), (c + cs(": copied.", ": copiado.")).c_str());
        };
        r.push_back(std::move(row));
    }
    action(r, "friends.add", cs("Add a friend", "Agregar un amigo"), []() {
        keyboard(cs("Their friend code", "Su código de amigo"), "", 20, [](const std::string &typed, std::string &error) {
            std::string why;
            if (chat::add_friend(typed, &why)) return true;
            error = chat::explain(why);
            return false;
        });
    });
    r.back().help = cs("Type their code (XXXX-XXXX-XXXX). They are found as soon as both of you are online, and get your "
                       "request; you can talk privately once they add you back.",
                       "Escribe su código (XXXX-XXXX-XXXX). Se lo encuentra apenas los dos estén en línea, y le llega tu "
                       "solicitud; pueden hablar en privado cuando te agregue también.");
    std::vector<chat::Person> people = chat::people();
    bool any = false;
    for (const chat::Person &p : people) {
        if (!p.request || p.is_friend || p.blocked) continue;
        if (!any) header(r, "friends.requests", cs("Requests", "Solicitudes"));
        any = true;
        Row row;
        row.id = "friends.req." + p.id, row.title = p.name, row.value = cs("wants to be your friend", "quiere ser tu amigo");
        row.menu = true;
        Row copy = row;
        const std::string id = p.id, name = p.name;
        row.activate = [copy, id, name]() {
            choices(copy, {cs("Accept", "Aceptar"), cs("Ignore", "Ignorar"), cs("Block", "Bloquear")}, -1, [id, name](int k) {
                if (k == 0) chat::accept(id);
                else if (k == 1) chat::remove(id);
                else chat::set_blocked(id, name, true);
            });
        };
        r.push_back(std::move(row));
    }
    header(r, "friends.list", cs("Friends", "Amigos"));
    any = false;
    for (const chat::Person &p : people) {
        if (!p.is_friend && !p.pending) continue;
        any = true;
        Row row;
        row.id = "friends.f." + (p.pending ? p.code : p.id);
        row.title = p.pending ? chat::t("Code ", "Código ") + chatc::show_code(p.code) : p.name.empty() ? chatc::show_code(p.code) : p.name;
        if (p.pending) row.value = cs("looking for them...", "buscándolo...");
        else if (!p.mutual) row.value = cs("waiting for them to add you", "esperando que te agregue");
        else if (p.online) row.value = p.in_hub ? cs("online, in a hub", "en línea, en una sala") : cs("online", "en línea");
        else row.value = p.seen ? std::string(cs("seen ", "visto ")) + chat::ago(p.seen) : std::string(cs("offline", "desconectado"));
        if (p.online && p.mutual) row.value += p.game == "mh4u" ? " (MH4U)" : p.game == "mhgu" ? " (MHGU)" : "";
        row.help = std::string(cs("Friend code: ", "Código de amigo: ")) + chatc::show_code(p.code);
        row.menu = true;
        Row copy = row;
        const chat::Person q = p;
        row.activate = [copy, q]() {
            std::vector<std::string> what;
            std::vector<int> act;
            if (q.is_friend && q.mutual) what.push_back(cs("Talk", "Hablar")), act.push_back(0);
            if (q.is_friend && q.mutual && q.online && *s.draft.ldn_key) what.push_back(cs("Send my matchmaking key", "Mandar mi clave de emparejamiento")), act.push_back(1);
            if (!q.pending) what.push_back(q.muted ? cs("Unmute", "Quitar silencio") : cs("Mute", "Silenciar")), act.push_back(2);
            if (!q.pending) what.push_back(cs("Block", "Bloquear")), act.push_back(3);
            what.push_back(cs("Remove", "Quitar")), act.push_back(4);
            choices(copy, what, -1, [q, act](int k) {
                if (k < 0 || k >= (int)act.size()) return;
                std::string why;
                switch (act[k]) {
                case 0: chat_open(chat::FRIEND, q.id); break;
                case 1:
                    if (!chat::share(chat::FRIEND, q.id, chat::SHARE_KEY, chat_server_name(), s.draft.ldn_key, &why)) s.error = chat::explain(why);
                    else chat_open(chat::FRIEND, q.id);
                    break;
                case 2: chat::set_muted(q.id, q.name, !q.muted); break;
                case 3: chat::set_blocked(q.id, q.name, true); break;
                default: chat::remove(q.pending ? "code:" + q.code : q.id); break;
                }
            });
        };
        r.push_back(std::move(row));
    }
    if (!any) note(r, "friends.none", cs("No friends yet: add one with their code.", "Todavía no tienes amigos: agrega uno con su código."));
    any = false;
    for (const chat::Person &p : people) {
        if (!p.muted && !p.blocked) continue;
        if (!any) header(r, "friends.quiet", cs("Muted and blocked", "Silenciados y bloqueados"));
        any = true;
        Row row;
        row.id = "friends.q." + p.id, row.title = p.name.empty() ? p.id.substr(0, 12) : p.name;
        row.value = p.blocked ? cs("blocked", "bloqueado") : cs("muted", "silenciado");
        row.help = cs("A tap lets them back. Only you are affected, on this device.", "Un toque lo deja volver. Solo te afecta a ti, en este dispositivo.");
        const std::string id = p.id, name = p.name;
        const bool blocked = p.blocked;
        row.activate = [id, name, blocked]() {
            if (blocked) chat::set_blocked(id, name, false);
            chat::set_muted(id, name, false);
        };
        r.push_back(std::move(row));
    }
}
// Multiplayer's rows: the chat and the friends, each a page of its own
void chat_entry_rows(std::vector<Row> &r) {
    header(r, "chat.header", cs("Chat and friends", "Chat y amigos"));
    int unread = 0;
    for (int c = 0; c < chat::CHANNELS; c++) unread += chat::unread(c);
    action(r, "chat.open", cs("Chat", "Chat"), []() { push(View::Chat, "chat.say"); });
    int up = 0, total = 0;
    chat::Status st = chat::status(&up, &total);
    r.back().value = (unread ? fmt("%d %s - ", unread, cs("new", "nuevas")) : std::string()) + chat::status_text(st, up, total) + " ›";
    r.back().help = cs("Talk with the hub you are in, with every player of the ports (Global) or of this game (General), and "
                       "with your friends.",
                       "Habla con la sala en la que estás, con todos los jugadores de los ports (Global) o de este juego "
                       "(General), y con tus amigos.");
    action(r, "friends.open", cs("Friends", "Amigos"), []() { push(View::Friends, "friends.code"); });
    int on = 0, requests = 0;
    for (const chat::Person &p : chat::people()) on += p.is_friend && p.online, requests += p.request && !p.is_friend && !p.blocked;
    r.back().value = (requests ? fmt("%d %s - ", requests, cs("requests", "solicitudes")) : std::string()) + fmt("%d %s ›", on, cs("online", "en línea"));
    r.back().help = cs("Your friend code, adding friends, and who you muted or blocked.", "Tu código de amigo, agregar amigos, y a quién silenciaste o bloqueaste.");
}
