// The Supply Box without the wait between items (1.0.2, Alfonso's idea): each item taken from a quest's Supply Box goes
// to the pouch at once, and the next one can be taken right away. The game's rules stay: the pouch's room and each item's
// maximum, the shared box online (the host grants each item), the same sounds and messages. Always on: only the icon's
// flight to the pouch, which kept the box from taking the next item, is cut.
//
// The game's Supply Box (lifted code of v1.4.0):
// - the box is sQuest::SupplyBox (DTI 0x1a8ad30), at *(0x11bee38 + 0x879ef0) + 0x1a54 (sQuest + 0x1a54): +0xad / +0xae
//   the cursor's column and row, +0xc + row * 0x20 + column * 4 the slots ({u16 item, s16 count}, 8 a row), +0xb0 a wait
//   (float, in the game's frames), +0xb4 the wait after it opens (20), +0xb8 the slots taken (online: by anyone),
//   +0xc0 the slots there are, +0xea the slot whose item is on its way to the pouch (-1: none), +0xec / +0xee its item and
//   count.
// - each update of the open box (0x5af4e4 -> 0x5af5b4) takes nothing while +0xb0 or +0xb4 is above 0 or an item is on
//   its way (+0xea >= 0). A on a slot: the pouch's room for that item (0x39cbb4) must hold its whole count, else sound 3
//   and nothing. Alone: the item into the pouch (0x39b7fc), sound 9, the slot emptied, +0xea = that slot. Online
//   (0x427a98): a request to the host (remote call 0x57, nFestaNetwork::netSupplyBox) and +0xb0 = 1800 until its answer;
//   the host (0x5afd54) grants a slot nobody took yet (+0xb8) and tells everyone; the player it was granted to puts it in
//   the pouch there, with +0xea = the slot and +0xb0 = 1. So two players never get the same slot, however fast they ask.
// - the box's window, cUIOSupplyBox (DTI 0x1aa0360, vtable 0x1977214; its update 0x83a6b0, from vtable slot 19
//   0x83a698), flies the item's icon to the pouch while +0xea >= 0: its first update there sets +0x50 = the flight's
//   time, +0x4c = 20 and +0x54 = 1; each update takes the frame's time off both, and once +0x4c is spent it calls
//   0x5af4d8 (+0xea = -1: the box takes again) and sets +0x54 = 0. Those 20 frames are the wait between items.
//
// Hooked here: 0x83a6b0. Before the game's own update, with an icon on its way (+0x54 = 1), +0x4c is spent, so that same
// update ends the flight the game's way (0x5af4d8). Nothing else is touched: what is taken, the requests to the host
// and their answers, the room in the pouch and the box's own waits (the one after it opens, the one for the host's
// answer) are the game's.
// MHGU_SUPPLY_WAIT=1 (tests): the game's wait, to compare. MHGU_SUPPLY_LOG=1: a line for each item's flight.
#include <stdlib.h>

#include "../hle.h"
#include "../platform.h"

#define FN_UI_UPDATE  0x83a6b0u    // cUIOSupplyBox's update (r0: the window)
#define VT_WINDOW     0x1977214u   // cUIOSupplyBox's vtable
#define UI_WAIT       0x4cu        // float: the frames left before the item's flight ends (20 when it starts)
#define UI_FLIGHT     0x54u        // u8: 0 no flight started, 1 an icon on its way to the pouch

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static int g_mode = -1;  // 0 the game's wait (MHGU_SUPPLY_WAIT=1), 1 cut; and the log
static bool g_log;
static unsigned g_items;
static bool g_flying;    // (the log: this flight seen already)

static void ui_update(Cpu *c) {
    uint32_t w = c->r[0];
    if (g_mode < 0) {
        const char *e = getenv("MHGU_SUPPLY_WAIT");
        g_mode = e && atoi(e) ? 0 : 1;
        g_log = (e = getenv("MHGU_SUPPLY_LOG")) && atoi(e);
        LOG("[supply] the Supply Box: %s\n", g_mode ? "no wait between items" : "the game's wait between items (MHGU_SUPPLY_WAIT)");
    }
    if (sane(w) && LD32(w) == VT_WINDOW) {
        bool flight = LD8(w + UI_FLIGHT) == 1;
        if (flight && LDF(w + UI_WAIT) > 0) {
            if (g_log && !g_flying)
                LOG("[supply] item %u on its way at %llu ms, %.1f frames of wait left%s\n", ++g_items,
                    (unsigned long long)(plat_now_ns() / 1000000), (double)LDF(w + UI_WAIT), g_mode ? ": ended now" : "");
            if (g_mode) STF(w + UI_WAIT, 0.0f);
        }
        g_flying = flight;
    }
    fnhook_call_original(c, FN_UI_UPDATE);
}

void supplybox_init(void) {
    fnhook_set(FN_UI_UPDATE, hle_register("supplybox:ui_update", ui_update));
}
