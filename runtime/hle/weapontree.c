// The Smithy's weapon tree (1.0.2, runtime/weapon_tree.h): the game's side. What the Smithy shows and the player's
// equipment box, read from the game's memory: the Smithy's object as its update runs (a hook of that update, which
// only notes the object), and once a frame after the game's update (runtime/overlay.cpp overlay_frame) what it shows.
// The game library is the same (GAMELIB_ID unchanged: fnhook_set, as runtime/hle/gather.c).
//
// main v1.4.0, found on the PC (2026-11-20, Bherna's Smithy, "Don Yunque"):
// - the Smithy is uUIBlackSmith (DTI 0x1a4ce50, 0x3218 bytes, vtable 0x197d988), made as its menu opens and gone as
//   it closes; its update 0x8f1ae8 (vtable slot 9) runs each frame meanwhile. +0x04 its state (u8): 1 its first menu,
//   2 the classes, 3 Forge's list of a class, 7 Upgrade (+0x05 1 the box's grid, 0x19 the upgrades of the one chosen).
//   +0x2108 a cFestaEquip, the equipment it shows now (the list's row under the cursor, the box's cell, the upgrade
//   picked: its model, "Actual:", its stats): +0x210c u16 the kind (bits 0..4: 7 the great sword .. 21 the charge
//   blade, the table's class + 7; 1..6 armour, a talisman), the level - 1 (bits 5..11) and a flag (bit 15: set in
//   Forge's list on the Anbernic, 0x8007, not on the PC), +0x210e u16 the base.
//   Forge's list, the first great swords of a save: Petrified Blade 1, Iron Sword 11, Bone Blade 26, each level 1;
//   Upgrade's grid on a Petrified Blade (its level written 3): base 1 level 3, attack 90; its upgrades: level 4 (Level
//   up), Defender (base 8), Type 41 Wyvernator (base 6), as the game's own list shows them.
// - the equipment box: sEquipBox (DTI 0x1a56fc8; *0x1a56fe8), from +0x1c its cFestaEquip of 0x2c bytes, the hunter's
//   1000 the Smithy's grid shows (10 pages of 100; its cursor +0x20a8 says 1000): +0x04 u16 the kind and level as
//   above, +0x06 u16 the base. A level written into one there (Petrified Blade, 0x47: level 3) showed in the grid as
//   Level 3 with its stats.
// - the weapon worn: sPlayer (*0x1a85b58) +0x84 the box's indices of what is worn (u16: the weapon, the armour, the
//   talisman; 0xffff none) and +0x94 a cFestaEquip, a copy of the weapon worn (the Smithy's "E").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../weapon_tree.h"

#define VT_SMITHY        0x197d988u  // uUIBlackSmith's vtable
#define FN_SMITHY_UPDATE 0x8f1ae8u   // its update (vtable slot 9)
#define SM_STATE         0x04u       // u8: 3 Forge's list, 7 Upgrade
#define SM_SHOWN         0x2108u     // cFestaEquip: what it shows
#define EQ_KIND_LEVEL    0x04u       // (a cFestaEquip's) u16: kind in bits 0..4, level - 1 in bits 5..11, bit 15 a flag
#define EQ_BASE          0x06u       // u16
#define KIND_WEAPON      7u          // (kinds 7 .. 21: the classes 0 .. 14)
#define G_EQUIPBOX       0x1a56fe8u  // sEquipBox *
#define BOX_FIRST        0x1cu
#define BOX_SIZE         0x2cu
#define BOX_COUNT        1000u
#define G_SPLAYER        0x1a85b58u  // sPlayer *
#define PL_WORN          0x94u       // cFestaEquip: the weapon worn
#define G_GAMECONTROL    0x1a8ca48u  // sGameControl *: +0xa5 the game's language

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static uint32_t g_smithy;        // the Smithy's object, as its update ran last (0: none)
static uint64_t g_seen_frame;    // (the frame it ran in)
static uint64_t g_frame;
static int g_log = -1;           // MHGU_WT_LOG=1: a line each time what it shows changes; 2: and the Smithy's fields
// what the Smithy shows, for any thread: shown (8 bits) | class (8) | level (8) | base (8) | language (8) | serial (24)
static uint64_t g_packed;

static WtSmithy unpack(uint64_t v) {
    WtSmithy s;
    s.shown = (v & 0xff) != 0;
    s.cls = (int)(v >> 8 & 0xff), s.level = (int)(v >> 16 & 0xff), s.base = (int)(v >> 24 & 0xff);
    s.language = (int)(v >> 32 & 0xff), s.serial = (uint32_t)(v >> 40);
    return s;
}

WtSmithy weapontree_smithy(void) { return unpack(__atomic_load_n(&g_packed, __ATOMIC_ACQUIRE)); }

// A cFestaEquip's weapon: its class (0..14, no 5), base and level; false for none or not a weapon
static bool weapon_of(uint32_t eq, int *cls, int *base, int *level) {
    uint32_t kl = LD16(eq + EQ_KIND_LEVEL), kind = kl & 0x1f;
    int b = LD16(eq + EQ_BASE), lv = (int)(kl >> 5 & 0x7f) + 1;  // (bit 15: a flag, seen set on a phone's Forge list)
    if (kind < KIND_WEAPON || kind > KIND_WEAPON + 14 || kind == KIND_WEAPON + 5 || b <= 0 || b > 255 || lv > 255) return false;
    *cls = (int)(kind - KIND_WEAPON), *base = b, *level = lv;
    return true;
}

static void publish(bool shown, int cls, int base, int level) {
    uint64_t old = __atomic_load_n(&g_packed, __ATOMIC_ACQUIRE);
    WtSmithy o = unpack(old);
    uint32_t gc = LD32(G_GAMECONTROL);
    int lang = sane(gc) ? LD8(gc + 0xa5) : 1;
    if (o.shown == shown && (!shown || (o.cls == cls && o.base == base && o.level == level)) && o.language == lang) return;
    uint32_t serial = (o.serial + 1) & 0xffffff;
    uint64_t v = (uint64_t)(shown ? 1 : 0) | (uint64_t)(cls & 0xff) << 8 | (uint64_t)(level & 0xff) << 16 |
                 (uint64_t)(base & 0xff) << 24 | (uint64_t)(lang & 0xff) << 32 | (uint64_t)serial << 40;
    __atomic_store_n(&g_packed, v, __ATOMIC_RELEASE);
    if (g_log > 0) {
        if (shown) LOG("[weapon tree] the Smithy shows class %d base %d level %d (language %d)\n", cls, base, level, lang);
        else if (o.shown) LOG("[weapon tree] the Smithy shows no weapon\n");
    }
}

// (debug, MHGU_WT_LOG=2) the Smithy's state and what it shows, each time they change
static void trace(uint32_t sm) {
    static uint32_t last[4];
    uint32_t now[4] = {sm, LD32(sm + SM_STATE), LD32(sm + SM_SHOWN + EQ_KIND_LEVEL), LD8(sm + 0x1f71)};
    if (!memcmp(now, last, sizeof now)) return;
    memcpy(last, now, sizeof now);
    LOG("[weapon tree] smithy %08x: state %08x, shows %08x, list row %u\n", now[0], now[1], now[2], now[3]);
}

// uUIBlackSmith's update, each frame while the Smithy is open: its object (nothing else is done here)
static void smithy_update(Cpu *c) {
    uint32_t sm = c->r[0];
    fnhook_call_original(c, FN_SMITHY_UPDATE);
    if (!sane(sm) || LD32(sm) != VT_SMITHY) return;
    g_smithy = sm, g_seen_frame = g_frame;
    if (g_log > 1) trace(sm);
}

void weapontree_frame(void) {
    g_frame++;
    if (g_log < 0) g_log = getenv("MHGU_WT_LOG") ? atoi(getenv("MHGU_WT_LOG")) : 0;
    uint32_t sm = g_smithy;
    // (its update ran this frame or the last: the Smithy is open; else gone, or a load)
    if (!sm || g_frame - g_seen_frame > 2 || !sane(sm) || LD32(sm) != VT_SMITHY) {
        if (sm && g_frame - g_seen_frame > 2) g_smithy = 0;
        publish(false, 0, 0, 0);
        return;
    }
    uint32_t state = LD8(sm + SM_STATE);
    int cls, base, level;
    if ((state == 3 || state == 7) && weapon_of(sm + SM_SHOWN, &cls, &base, &level)) publish(true, cls, base, level);
    else publish(false, 0, 0, 0);
}

void weapontree_box(int cls, uint8_t *levels, int n, int *worn_base, int *worn_level) {
    memset(levels, 0, (size_t)n);
    *worn_base = -1, *worn_level = 0;
    int c, b, lv;
    uint32_t box = LD32(G_EQUIPBOX);
    for (uint32_t i = 0; sane(box) && i < BOX_COUNT; i++)
        if (weapon_of(box + BOX_FIRST + i * BOX_SIZE, &c, &b, &lv) && c == cls && b < n && lv > levels[b])
            levels[b] = (uint8_t)lv;
    uint32_t pl = LD32(G_SPLAYER);
    if (sane(pl) && weapon_of(pl + PL_WORN, &c, &b, &lv) && c == cls) *worn_base = b, *worn_level = lv;
}

void weapontree_init(void) {
    fnhook_set(FN_SMITHY_UPDATE, hle_register("weapontree:smithy_update", smithy_update));
}
