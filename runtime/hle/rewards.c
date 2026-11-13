// [Quest Rewards] SendAllRewards (Alfonso's idea): at the end of a quest, choosing Send to the Item Box in the Quest Rewards
// menu sends every reward at once. Off, the game's way: the cursor goes into the Reward Box and A sends one item at a time.
//
// The rewards screen is uUIResult (its DTI 0x1a8a7f8, vtable 0x195332c, 0x3f0 bytes; its update is vtable slot 9,
// 0x584848, a state at +0x3c). Its states 2..5 run the reward lists through 0x588ee8(result, mode): +0x44 the step in
// them, +0x4c the mode, which list is shown: modes 0, 4 and 5 the list at +0x5c, 1 +0x64, 2 +0x6c, 3 +0x74, each
// {u32 slots*, u32 count} of {u16 item, s16 count} slots (the tables at 0x589868, 0x589928 and 0x589cf0 pick them).
// Its menu buttons come from sFestaPad's +0x6bfc (A 0x2000, B 0x4000, X 0x1000), as in runtime/hle/mousemenu.c.
//   +0x44 = 2: the Quest Rewards menu (a cCursor at +0xa0: +0xad the row on its page, +0xae rows a page, +0xb0 the
//          page; Take Items, Send to the Item Box, Sell Items & Exit / Done). A on a row, with any item left in the
//          list (0x589a90), sets +0x44 = 3 + the row (0x589ae4): 3 Take Items, 4 Send to the Item Box, 5 the sale's
//          yes / no; and resets the Reward Box's cBoxCursor (+0xc4: +0xd8 column, +0xd9 row, +0xdc page).
//   +0x44 = 4: Send to the Item Box. B goes back to the menu (0x58919c); A on a cell (0x5898e8 -> 0x589c50) sends its
//          slot: nothing for an empty one; else +0x55 = 1 (drawn again), p = 0x394228([0x1a583a8], item) (the item's
//          data; none: nothing more); an item whose p+0x16 is set goes through 0x5c6c6c([0x1a8b458], slot, 0) and its
//          slot is emptied, with sound 0x1f; any other through 0x58a2dc(result, slot): 0x399390 says whether the box
//          takes it (no: sound 3, and it stays), 0x39945c adds it and leaves what didn't fit in the slot (or empties
//          it), the quest's target items are noted (0x5a864c), sound 0x1f. Then, with no item left in the list
//          (0x589f7c), +0x44 = 2 with the menu's cursor on its third row (Done) and both cursors reset (0x74ecc0).
// The sounds are 0x74f260(0, id), which plays the menu's sound id (0x6eeb78).
//
// Hooked here: 0x588ee8. In the first update after the menu chose Send to the Item Box (+0x44 2 -> 4), before the game's
// own, each slot of the list shown goes through those same steps, the same calls in the same order, with their sounds
// muted (0x74f260 is hooked for that) and then one sound for all; with none left, the menu is set as the game sets it
// after the last one. What the box can't take stays in the list, in the Send to the Item Box step: the player sees it
// and can take it, sell it or try it, as the game lets them. Nothing else is touched: Take Items, the sale and the
// other screens of uUIResult (its state 6, whose own list also goes through 0x58a2dc from 0x5859fc) are the game's.
#include <stdio.h>

#include "../config.h"
#include "../hle.h"

#define FN_RESULT_LISTS  0x588ee8u  // uUIResult's reward lists, each update of states 2..5: (result, mode)
#define FN_SEND_ONE      0x58a2dcu  // (result, slot): an item into the item box, what didn't fit left in the slot
#define FN_ITEM_DATA     0x394228u  // (items, item id) -> the item's data, or 0
#define FN_SEND_SPECIAL  0x5c6c6cu  // (G_SPECIAL, slot, 0): an item whose data's +0x16 is set, sent its own way
#define FN_CURSOR_RESET  0x74ecc0u  // (cursor, how): the game's cursor reset, as 0x589fdc / 0x589fe8 call it
#define FN_MENU_SE       0x74f260u  // (0, id): a menu sound
#define G_ITEMS          0x1a583a8u  // * the item data (0x394228's first argument: 0x589c64)
#define G_SPECIAL        0x1a8b458u  // * 0x5c6c6c's first argument (0x589c8c)
#define RES_STEP         0x44u   // u32: 2 the menu, 3 Take Items, 4 Send to the Item Box, 5 the sale's yes / no
#define RES_REDRAW       0x55u   // u8: 1, the lists drawn again
#define RES_MENU         0xa0u   // cCursor: the Quest Rewards menu
#define RES_MENU_ROW     0xadu   // u8: its row on its page
#define RES_MENU_ROWS    0xaeu   // u8: rows a page
#define RES_MENU_PAGE    0xb0u   // u16: its page
#define RES_BOX          0xc4u   // cBoxCursor: the Reward Box
#define STEP_MENU        2u
#define STEP_SEND        4u
#define ITEM_SPECIAL     0x16u   // u8 of the item's data: sent through FN_SEND_SPECIAL
#define SE_SENT          0x1fu   // the sound of an item sent
#define SE_NO_ROOM       3u      // the sound of one the box can't take
#define MAX_SLOTS        256u    // (the lists hold 48: anything past this is not a list)

static Cpu *g_quiet;       // the menu's sounds muted, on this thread (while all of them are sent)
static uint32_t g_result;  // the uUIResult of the last update seen, and its step then
static uint32_t g_step;

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static uint32_t call(Cpu *c, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t r2) {
    uint32_t saved[16];
    for (int i = 0; i < 16; i++) saved[i] = c->r[i];
    c->r[0] = r0, c->r[1] = r1, c->r[2] = r2;
    guest_call(c, fn);
    uint32_t v = c->r[0];
    for (int i = 0; i < 16; i++) c->r[i] = saved[i];
    return v;
}

// The list a mode shows: {slots, count} (0x589868's table)
static uint32_t list_of(uint32_t mode) {
    switch (mode) {
    case 1: return 0x64u;
    case 2: return 0x6cu;
    case 3: return 0x74u;
    default: return 0x5cu;
    }
}

static void menu_se(Cpu *c) {
    if (g_quiet == c) return;
    fnhook_call_original(c, FN_MENU_SE);
}

// Every slot of the list through the game's steps for one (0x589c50); then, with none left, the menu as 0x589fb0 sets it
static void send_all(Cpu *c, uint32_t res, uint32_t mode) {
    uint32_t list = res + list_of(mode), slots = LD32(list), count = LD32(list + 4);
    if (!sane(slots) || count > MAX_SLOTS) return;
    uint32_t items = LD32(G_ITEMS), special = LD32(G_SPECIAL);
    if (!sane(items)) return;
    int sent = 0, part = 0, kept = 0;
    g_quiet = c;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t slot = slots + 4 * i;
        uint16_t item = LD16(slot);
        if (!item) continue;
        uint16_t had = LD16(slot + 2);
        ST8(res + RES_REDRAW, 1);
        uint32_t data = call(c, FN_ITEM_DATA, items, item, 0);
        if (!data) {  // (the game does nothing more with it)
            kept++;
            continue;
        }
        if (LD8(data + ITEM_SPECIAL)) {
            call(c, FN_SEND_SPECIAL, special, slot, 0);
            ST16(slot, 0), ST16(slot + 2, 0);
            sent++;
            continue;
        }
        call(c, FN_SEND_ONE, res, slot, 0);
        if (!LD16(slot)) sent++;
        else if (LD16(slot + 2) != had) part++;
        else kept++;
    }
    g_quiet = NULL;
    bool left = false;
    for (uint32_t i = 0; i < count && !left; i++) left = LD16(slots + 4 * i) != 0;
    if (sent || part) call(c, FN_MENU_SE, 0, SE_SENT, 0);
    else if (kept) call(c, FN_MENU_SE, 0, SE_NO_ROOM, 0);
    if (!left) {  // (0x589fb0: the menu again, its cursor on the third row, Done)
        ST32(res + RES_STEP, STEP_MENU);
        uint8_t rows = LD8(res + RES_MENU_ROWS);
        uint32_t page = rows ? 2u / rows : 0;
        ST16(res + RES_MENU_PAGE, (uint16_t)page);
        ST8(res + RES_MENU_ROW, (uint8_t)(2u - page * rows));
        call(c, FN_CURSOR_RESET, res + RES_MENU, 0, 0);
        call(c, FN_CURSOR_RESET, res + RES_BOX, 2, 0);
    }
    LOG("[rewards] Send to the Item Box: %d sent, %d partly (the box full for the rest), %d left in the list (list %u)%s\n",
        sent, part, kept, mode, left ? "" : ": none left, back to the menu");
}

static void result_lists(Cpu *c) {
    uint32_t res = c->r[0], mode = c->r[1];
    if (sane(res)) {
        uint32_t step = LD32(res + RES_STEP);
        bool chose = res == g_result && g_step == STEP_MENU && step == STEP_SEND;  // (the menu chose it last update)
        g_result = res, g_step = step;
        if (chose && config_live_send_all_rewards()) send_all(c, res, mode);
    }
    fnhook_call_original(c, FN_RESULT_LISTS);
}

void rewards_init(void) {
    fnhook_set(FN_RESULT_LISTS, hle_register("rewards:result_lists", result_lists));
    fnhook_set(FN_MENU_SE, hle_register("rewards:menu_se", menu_se));
}
