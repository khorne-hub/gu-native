// Hunting aids (1.0.1, the owner's: "everything off by default, so whoever wants it turns it on"), the game's side: once a
// frame, on the game's update thread after its update (runtime/overlay.cpp overlay_frame), from the game's memory alone.
// No hook of the game's code: off, nothing here reads or writes anything. runtime/hunt_aids.h says what each aid is;
// runtime/hunt_overlay.cpp draws the numbers and the bars.
//
// The monsters (main v1.4.0, runtime addresses): sEnemy (*0x1a51fe0) +0x1c, ten slots: the quest's large monsters
// (whatever zone they are in) and the small ones of the hunter's zone. A monster: +0xb5f7 0 a large one (the Target Cam's
// own test, main 0x22194c), +0xb5f9 not 0 while it takes part (the map's test below), +0xb5f4 its em number, +0xb5f5 its
// variant (the monster id, small << 12 | variant << 8 | em number, as runtime/hle/presence.c reads quests), +0x1428 its
// status block (inside it: +0x1440), +0x73e0 / +0x73e1 its state (dead 0xb or 0xe, captured 0xc / 0xff: main 0x27ffb8).
// The status block: +0x40 the place (the feet), +0x370 the health (s32), +0x374 the most (the HP setters 0x29a7b8,
// 0x29a764: health + n, clamped to [0, most]), +0x1c4 the mark's timer below, +0x4f0 where the hit the monster reacts to
// landed (a vec3 the hunters' hits write, 0x371350 .. 0x376ad0, as they register: read by 0x29a828, the damage
// step's end, for the flinch's place). A model's skeleton (mousecam.c): +0x490 the joints, +0x494 their array, 0xa0 bytes
// each, the world place at +0x40, +0x91 the parent (0xff the root, the pelvis: the body's middle).
//
// Monsters always on the map ([Hunting Aids] MonstersOnMap). The quest maps (the minimap, cUIOQuestMap's update 0x819614,
// and the full map) show a monster when 0x81a86c says so: past the map's own checks (+0xb5f9, 0x270824, 0x26ff44, the
// zone's), a large monster shows while 0x2c0940 is true, its status block's +0x1c4 above 0 (or by the Autotracker's
// kind of skill, the map's +0xd8 +4 bits). That float is the mark's timer, in 60 fps frames: the game's mark (0x2c0908,
// the monster's vtable +0x3c0: a paintball's hit, a skill) sets it to 36000 (10 minutes) unless +0x15e9 bit 2 (a monster
// never marked), and the monster's status step counts it down (0x2c8ddc -> 0x272128). Found from the map: the drawing
// (0x81a064) shows the monsters of the map's +0xdc bit mask, which 0x819614 fills from 0x81a86c. Here, in a quest hunted
// alone, each large monster that takes part and is not marked gets the timer kept between KEEP_LOW and KEEP_HIGH: the
// game then draws its own icon, from the quest's first frame (seen: written in the camp, the monster's pink mark in its
// zone of the minimap at once). A mark of the game's own (36000) is left to the game until it runs down to our range.
// Turned off in a quest, our timers go back to 0 at once (a monster the game had marked is left as it is).
// Multiplayer: the mark is shared with the party by the game's own message, not by the timer: 0x2c0908 also sets bit 0
// of the monster's +0x7429, the status events a hunter's game sends (vtable +0x43c, 0x2cbdc0: message 0x3e with that
// byte), which a receiving game turns into its own mark (0x2ccc88: bit 0 -> vtable +0x3c0) before it clears +0x7429.
// Writing the timer alone sets none of that. But whether nothing else ever sends a status block whole could not be
// settled, so with another hunter in the quest (sPlayer's slots, the quest's join count) nothing is written: the map is
// the game's own there, and the menu's help says so.
//
// Damage numbers ([Hunting Aids] DamageNumbers): each frame, each monster's health (large and small: 1.0.2) against the
// last frame's; a drop is a number of that much (any damage: the hunter's, the Palicos', bombs, poison, the other
// hunters' in a party). From 1.0.2 this game's own hits are also read as the game works them out (below, "the hits":
// two hooks, set the first time the numbers are on): a drop goes to the hits that made it, each its own number with what
// it was (a weak point, a hard part, fixed damage, a critical hit, its element's part), for the "By hit" colour and the
// styles (runtime/hunt_overlay.cpp); and a party guest's hits, whose drop the host's game makes, show all the same.
// Its place: the reacting hit's (+0x4f0) when it changed since the monster's last number (and is near it), else the body's
// middle (the root joint, or 100 units above the feet). Only for a monster in the hunter's zone of the map (+0x1054 of
// both: each zone has its own coordinates). Placed on the game's picture every frame with the camera the game
// drew this frame with: sCamera (*0x2326504, sFestaCamera) +0x54 the camera, +0xe0 its view matrix and +0x120 its
// projection (the port's aspect and field of view in: tools/gen.py's patch of 0x21c358), row vectors (clip = p * V * P),
// +y up in clip space. Found by matching the monsters' places with a screenshot.
//
// Monster health bars ([Hunting Aids] MonsterHealthBars): each large monster taking part, alive, with its health and the
// most, whether it is in the hunter's zone of the map (a bar fainter when not, as MH4U's), and its name (the owner: "so
// they are easy to tell apart when there are several"; MH4U's bars have it the same way): the game's own text in the
// game's language (sGameControl *0x1a8ca48 +0xa5: 0 jpn, 1 eng, 2 fre, 3 spa, 4 ger, 5 ita, 6 kor, 7 chT, 8 chS: the
// language index main 0xd359a8 turns into the folders' names), from GUI\06_msg\monsterName_<lang>.gmd by the game's
// tables (presence.c's: the monster id, small << 12 | variant << 8 | em number, to the name's index): none for the
// languages the overlay's font can't draw (Japanese, Korean, Chinese).
//   MHGU_HUNT_LOG=1   (debug) a line per number (the damage, from where, the hit's place or the body's) and per mark
//   MHGU_HUNT_COST=1  (debug) every 10 s: the average and the longest time of this step a frame
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"
#include "../hunt_aids.h"
#include "../romfs.h"

#define G_SENEMY      0x1a51fe0u  // sEnemy *: +0x1c the ten monsters
#define G_SCAMERA     0x2326504u  // sCamera * (sFestaCamera)
#define G_GAMECONTROL 0x1a8ca48u  // sGameControl *: +0x4c the town (0xff in a quest), +0x30 our player slot, +0xa5
                                  // the game's language
#define G_SPLAYER     0x1a85b58u  // sPlayer *: +0x1c the four players
#define G_SQUEST      0x1a8ad90u  // sQuest *: +0xa4 the quest being played
#define G_SELECTOR    0x1a8ae38u  // sQuestSelector *: +0xd750 the quest's join count
#define MON_PAIRS     0x1797ea4u  // (monster id, index) x 137, then the index's UI number: monsterName's string
#define MON_UI        0x182e7f8u
#define MON_PAIR_COUNT 137
#define MON_VALSTRAX_UI 132  // (Valstrax's name, 0x407's UI number: also the Village's Valstrax, 0x56: monster_name)
#define CAM_VIEW      0xe0u
#define CAM_PROJ      0x120u
#define EN_INFO       0x1428u
#define EN_JOINTS     0x490u
#define EN_ZONE       0x1054u     // (u8: the zone of the map it is in, a hunter's too)
#define EN_NO_MARK    0x15e9u     // bit 2: never marked
#define EN_STATE      0x73e0u
#define EN_EM         0xb5f4u
#define EN_SMALL      0xb5f7u
#define EN_TAKES_PART 0xb5f9u
#define ST_PLACE      0x40u
#define ST_MARK       0x1c4u
#define ST_HP         0x370u
#define ST_HP_MAX     0x374u
#define ST_HIT        0x4f0u
#define KEEP_LOW      600.0f      // the mark's timer kept between these (10 and 20 s at 60 fps): low, so that turned
#define KEEP_HIGH     1200.0f     // off it can be told from a mark of the game's own (36000, counting down)
#define SLOTS         10

static bool sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

static struct {
    uint32_t en;       // the monster this slot followed last frame (0: none)
    int hp;            // its health then
    float hit[3];      // its +0x4f0 when it last made a number
    bool marked;       // its mark's timer is ours
    const char *name;  // its name (monster_name), for the game's language name_lang (-1: not looked up yet)
    int name_lang;
    int credit;        // damage of hits shown before the health dropped for them (a party's guest), taken off the
    uint64_t credit_ns;  // next drops until CREDIT_NS after the last such hit
} g_slot[SLOTS];
typedef struct {
    float p[3];
    int value, elem;
    uint32_t id, en;   // (en: the monster, Burst's run)
    uint64_t born, last;
    uint8_t kind, element;
    int8_t crit;
    int hits;
} Num;
static Num g_num[HUNT_NUMBERS];
static int g_nnum;
static uint32_t g_next_id = 1;
static HuntView g_view;  // this frame's (huntaids_view)
static bool g_view_any, g_touched;  // (g_touched: something of a quest is remembered, or a timer is ours)
static int g_log = -1;

// ---- the names
typedef struct { char *data; char **str; int n; } Gmd;
static bool gmd_load(Gmd *g, const char *rpath) {  // (presence.c's reader: the GMD's strings in order)
    memset(g, 0, sizeof *g);
    const char *path = romfs_host_path(rpath);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = size > 0x30 && size < (8 << 20) ? malloc((size_t)size + 1) : NULL;
    bool ok = d && fread(d, 1, (size_t)size, f) == (size_t)size && !memcmp(d, "GMD", 4);
    fclose(f);
    uint32_t n, bytes;
    if (ok) memcpy(&n, d + 0x18, 4), memcpy(&bytes, d + 0x20, 4);
    if (!ok || bytes > (uint32_t)size || n > 0x10000) { free(d); return false; }
    d[size] = 0;
    g->data = d;
    g->str = calloc(n ? n : 1, sizeof *g->str);
    for (char *p = d + size - bytes; g->str && g->n < (int)n && p < d + size; p += strlen(p) + 1) g->str[g->n++] = p;
    return true;
}

static const char *monster_name(uint32_t en) {
    static Gmd names;
    static int lang = -1;
    static const char *const folder[] = {NULL, "eng", "fre", "spa", "ger", "ita"};  // (Latin letters: the font has them)
    uint32_t gc = LD32(G_GAMECONTROL);
    int now = sane(gc) ? LD8(gc + 0xa5) : 1;
    if (now != lang) {
        lang = now;
        free(names.data), free(names.str), memset(&names, 0, sizeof names);
        if (now >= 1 && now <= 5) {
            char path[96];
            snprintf(path, sizeof path, "nativeNX/%s/GUI/06_msg/monsterName_%s.gmd", folder[now], folder[now]);
            if (!gmd_load(&names, path)) LOG("[hunt] no monster names: %s\n", path);
        }
    }
    uint32_t id = LD8(en + EN_EM) | (uint32_t)LD8(en + EN_EM + 1) << 8 | (uint32_t)LD8(en + EN_SMALL) << 12;
    for (uint32_t i = 0; i < MON_PAIR_COUNT; i++)
        if (LD32(MON_PAIRS + 8 * i) == id) {
            uint32_t index = LD32(MON_PAIRS + 8 * i + 4), ui = index < MON_PAIR_COUNT ? LD32(MON_UI + 4 * index) : ~0u;
            // (1.0.2, T05: a few special monsters' UI numbers, 137 and on, land past the 137 names of the language, in
            // the file's second list, the names' kanji keys "KANJI_Lioleia"..., where they read as another monster:
            // the Village's Valstrax, 0x56 (em086, quest 1005 "Beware the Comet of Disaster"), read as "Rathalos".
            // Valstrax's own name for it (the UI number of 0x407's), no name for the others)
            if (ui >= MON_PAIR_COUNT) ui = id == 0x56 ? MON_VALSTRAX_UI : ~0u;
            return ui < (uint32_t)names.n ? names.str[ui] : NULL;
        }
    return NULL;
}

// ---- the quest and the monsters
static bool in_quest(void) {
    uint32_t gc = LD32(G_GAMECONTROL), sq = LD32(G_SQUEST);
    return sane(gc) && LD8(gc + 0x4c) == 0xff && sane(sq) && sane(LD32(sq + 0xa4));
}

static bool hunted_alone(void) {
    uint32_t sp = LD32(G_SPLAYER), sel = LD32(G_SELECTOR);
    int n = 0;
    for (int i = 0; sane(sp) && i < 4; i++) n += sane(LD32(sp + 0x1c + 4u * (uint32_t)i));
    uint32_t joined = sane(sel) ? LD32(sel + 0xd750) : 0;
    return n <= 1 && joined <= 1;
}

static bool large(uint32_t en) { return LD8(en + EN_SMALL) == 0 && LD8(en + EN_TAKES_PART) != 0; }

// The zone of the map the local hunter is in (+0x1054, a monster's too: each zone has its own coordinates, so a number
// is only placed for a monster in the hunter's zone); -1 none
static int hunter_zone(void) {
    uint32_t gc = LD32(G_GAMECONTROL), sp = LD32(G_SPLAYER);
    uint32_t slot = sane(gc) ? LD32(gc + 0x30) : 9, pl = sane(sp) && slot < 4 ? LD32(sp + 0x1c + 4u * slot) : 0;
    return sane(pl) ? LD8(pl + EN_ZONE) : -1;
}

static bool down(uint32_t en, uint32_t info) {
    uint32_t st = LD8(en + EN_STATE);
    return (int32_t)LD32(info + ST_HP) <= 0 || st == 0xb || st == 0xe || (st == 0xc && LD8(en + EN_STATE + 1) == 0xff);
}

// The body's middle: the root joint (the pelvis) when the skeleton reads as measured, else 100 units above the feet
static void body_middle(uint32_t en, uint32_t info, float out[3]) {
    out[0] = LDF(info + ST_PLACE), out[1] = LDF(info + ST_PLACE + 4) + 100.0f, out[2] = LDF(info + ST_PLACE + 8);
    uint32_t n = LD32(en + EN_JOINTS), arr = LD32(en + EN_JOINTS + 4);
    if (n < 3 || n > 512 || !sane(arr)) return;
    for (uint32_t i = 0; i < n && i < 8; i++) {
        uint32_t j = arr + i * 0xa0u;
        if (LD32(j) < 0x00200000u || LD32(j) >= 0x02347000u) return;  // (not a joint)
        if (LD8(j + 0x91) != 0xff) continue;
        float p[3] = {LDF(j + 0x40), LDF(j + 0x44), LDF(j + 0x48)};
        if (isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && LDF(j + 0x4c) == 1.0f &&
            fabsf(p[0] - out[0]) + fabsf(p[2] - out[2]) < 3000.0f)
            out[0] = p[0], out[1] = p[1], out[2] = p[2];
        return;
    }
}

// A number: a new one, or (Burst) added to its monster's number of a run of hits still going
static void add_number(uint32_t en, int value, int elem, uint8_t kind, int8_t crit, uint8_t element, const float p[3],
                       uint64_t now) {
    static const uint8_t RANK[] = {0, 1, 3, 0, 2};  // (Burst: what the run shows: a weak point over fixed over normal)
    if (config_live_number_style() == HUNT_STYLE_BURST)
        for (int i = g_nnum - 1; i >= 0; i--) {
            Num *n = &g_num[i];
            if (n->en != en || now >= n->last + HUNT_BURST_NS) continue;
            n->value += value, n->elem += elem, n->hits++, n->last = now;
            if (RANK[kind] > RANK[n->kind]) n->kind = kind;
            if (crit > 0 || (crit < 0 && n->crit == 0)) n->crit = crit;
            if (element) n->element = element;
            return;
        }
    if (g_nnum == HUNT_NUMBERS) memmove(g_num, g_num + 1, sizeof g_num[0] * (HUNT_NUMBERS - 1)), g_nnum--;
    Num *n = &g_num[g_nnum++];
    memset(n, 0, sizeof *n);
    n->p[0] = p[0], n->p[1] = p[1], n->p[2] = p[2];
    n->value = value, n->elem = elem, n->id = g_next_id++, n->en = en, n->born = n->last = now;
    n->kind = kind, n->crit = crit, n->element = element, n->hits = 1;
}

// ---- the hits (1.0.2: the "By hit" colour, small monsters, a party's guest): what the game's damage step worked out
// for each hit on a monster, read as it is worked out. A hit on a monster, every kind of attacker, goes through
// 0x370968(?, attack, monster, &place; on the stack &place2, ?, &hit): attack +0x6d picks the step, 0x371350 the
// hunter's own hits (raw = attack * motion * the hit zone * sharpness... * the critical multiplier, + the element's),
// 0x372bf0 a Palico's, 0x373a24, 0x374c0c and 0x376ad0 shells and shots. Each ends with the hit's total (an s16) and
// hands it to 0x29d6f8(monster, part, total, ?), which counts it on the part (its flinch and break), once: hooked here,
// that call shows the step's own floats in the callee-saved VFP registers and its frame (PATHS: by its return address,
// which register holds what, found reading each step), and the monster's health drops by the total in the monster's
// next update (0x297ddc, the parts' totals). The hit zone: 0x2bab60(monster, hit +6) the part's zones (cut, impact,
// shot, fire, water, thunder, ice, dragon), the step's own factor (hz / 100) in s18 for the hunter's hits and at its
// frame +0x54 for shells; the critical multiplier (1.25, 1, 0.75 from attack +0x90, the affinity, and a roll of 100) in
// s24 (the hunter's), s26 (a Palico's) or at the frame +0x4c (shots). attack +0x30: 0x8000 a cut or an impact, 0x1 a
// shot (both take a hit zone), 0x200000 a shell's hit, none of them damage with no hit zone (fixed); +0x48 the
// elements (0x10 fire, 0x20 water, 0x200 thunder, 0x40 ice, 0x80 dragon).
// A hit's number waits for its monster's health to drop (up to HIT_WAIT frames) and shows that drop, split among the
// hits it holds; a hit whose drop doesn't come (a party's guest: the host's game works the health out, T04) shows its
// own total, which a later drop of that monster pays back first (credit), so a hit is never shown twice. Only this
// game's hunters, Palicos and shells are hits here: another hunter's hit in a party drops the health with no hit, a
// plain number as before.
#define FN_HIT_DISPATCH 0x370968u
#define FN_PART_DAMAGE  0x29d6f8u
#define AT_FLAGS        0x30u
#define AT_ELEMENTS     0x48u
#define AT_ZONED        0x8001u
#define AT_SHELL        0x200000u
#define HITS            32
#define HIT_WAIT        12  // (frames: a hit stop holds the monster a few frames before its health drops)
#define CREDIT_NS       3000000000ull
#define WEAK_ZONE       45  // (a weak point; under HARD_ZONE a hard part, where a weapon bounces)
#define HARD_ZONE       25

// Each step's call of 0x29d6f8: its return address; the VFP register of the raw damage, of the element's, of the hit
// zone's factor and of the critical multiplier (-1 none), or the frame's offset of the zone and the critical (0 none)
static const struct {
    uint32_t lr;
    int8_t raw, elem, zone, crit;
    uint8_t zone_sp, crit_sp, hunter;
} PATHS[] = {
    {0x371b74u, 16, 22, 18, 24, 0, 0, 1},       // 0x371350: the hunter's own hits
    {0x3733bcu, -1, -1, -1, 26, 0, 0, 0},       // 0x372bf0: a Palico's
    {0x37420cu, 18, 20, -1, -1, 0x54, 0x4c, 0}, // 0x373a24: shots
    {0x375304u, 16, 20, -1, -1, 0x54, 0x4c, 0}, // 0x374c0c: shells
    {0x377780u, 16, 20, -1, -1, 0, 0, 0},       // 0x376ad0: shots
};

typedef struct {
    uint32_t en, path;
    float p[3];
    int total, elem, zone;  // (zone: the hit zone the raw damage took, -1 not known)
    uint8_t kind, element;
    int8_t crit;
    int frames;
} Hit;
static Hit g_hit[HITS];
static int g_nhit;
static struct {
    uint32_t en, flags, elements;
    float p[3];
    bool place, on;
    uint8_t by;  // (attack +0x6d: the step it takes)
} g_cur;  // (the hit 0x370968 is working out: hit_part reads it)
static bool g_hooked;

static uint8_t element_of(uint32_t e) {
    return e & 0x10u ? 1 : e & 0x20u ? 2 : e & 0x200u ? 3 : e & 0x40u ? 4 : e & 0x80u ? 5 : 0;
}

static void hit_part(Cpu *c) {
    uint32_t en = c->r[0], lr = c->r[14];
    int total = (int32_t)c->r[2];
    int p = -1;
    for (int i = 0; i < (int)(sizeof PATHS / sizeof PATHS[0]); i++)
        if (PATHS[i].lr == lr) p = i;
    if (g_cur.on && en == g_cur.en && total > 0 && p >= 0 && g_nhit < HITS) {
        float raw = PATHS[p].raw >= 0 ? c->v.f[PATHS[p].raw] : (float)total;
        float elem = PATHS[p].elem >= 0 ? c->v.f[PATHS[p].elem] : 0.0f;
        if (!(raw >= 0 && elem >= 0 && raw + elem > 0) || !isfinite(raw + elem)) raw = (float)total, elem = 0;
        float zone = -1, crit = 1;
        if (PATHS[p].zone >= 0 && (g_cur.flags & AT_ZONED)) zone = c->v.f[PATHS[p].zone] * 100.0f;
        else if (PATHS[p].zone_sp) zone = LDF(c->r[13] + PATHS[p].zone_sp) * 100.0f;
        if (PATHS[p].crit >= 0) crit = c->v.f[PATHS[p].crit];
        else if (PATHS[p].crit_sp) crit = LDF(c->r[13] + PATHS[p].crit_sp);
        Hit *h = &g_hit[g_nhit++];
        memset(h, 0, sizeof *h);
        h->en = en, h->path = lr, h->total = total;
        h->elem = (int)lrintf((float)total * elem / (raw + elem));
        h->zone = zone >= 0 && zone < 400 ? (int)lrintf(zone) : -1;
        h->crit = crit > 1.01f ? 1 : crit > 0 && crit < 0.99f ? -1 : 0;
        h->element = h->elem > 0 ? element_of(g_cur.elements) : 0;
        h->kind = PATHS[p].hunter && !(g_cur.flags & (AT_ZONED | AT_SHELL)) ? HUNT_HIT_FIXED
                  : PATHS[p].hunter && (g_cur.flags & AT_SHELL)               ? HUNT_HIT_FIXED
                  : h->zone >= WEAK_ZONE                                      ? HUNT_HIT_WEAK
                  : h->zone >= 0 && h->zone < HARD_ZONE                       ? HUNT_HIT_HARD
                                                                              : HUNT_HIT_NORMAL;
        if (g_cur.place) memcpy(h->p, g_cur.p, sizeof h->p);
        else body_middle(en, LD32(en + EN_INFO), h->p);
        if (g_log > 0)
            LOG("[hunt] hit on %08x: %d (raw %.1f, element %.1f of %x), zone %d, critical %.2f, flags %08x, kind %d by %08x: %s\n",
                en, total, raw, elem, g_cur.elements, h->zone, crit, g_cur.flags, g_cur.by, lr,
                (const char *[]){"plain", "normal", "a weak point", "hard", "fixed"}[h->kind]);
    }
    fnhook_call_original(c, FN_PART_DAMAGE);
}

static void hit_dispatch(Cpu *c) {
    __typeof__(g_cur) before = g_cur;
    uint32_t atk = c->r[1], en = c->r[2], at = c->r[3];
    g_cur.on = config_live_damage_numbers() && sane(atk) && sane(en);
    // (tests only: the test save's weapons have no affinity and no element) MHGU_HUNT_TEST_AFFINITY=<-100..100> the
    // hunter's own hits' affinity (attack +0x90; +0x32 bit 1 lets a hit roll it), MHGU_HUNT_TEST_ELEMENT=<the +0x48 bit,
    // hex>:<its attack> (e.g. 10:300 fire 30): the game works them out with these
    static const char *aff = (const char *)-1, *ele;
    if (aff == (const char *)-1) aff = getenv("MHGU_HUNT_TEST_AFFINITY"), ele = getenv("MHGU_HUNT_TEST_ELEMENT");
    if ((aff || ele) && sane(atk) && LD8(atk + 0x6d) != 1 && LD8(atk + 0x6d) != 2 && LD8(atk + 0x6d) != 4) {
        if (aff) ST16(atk + 0x90, (uint16_t)atoi(aff)), ST8(atk + 0x32, LD8(atk + 0x32) | 2);
        if (ele && strchr(ele, ':'))
            ST32(atk + AT_ELEMENTS, LD32(atk + AT_ELEMENTS) | (uint32_t)strtoul(ele, NULL, 16)),
                ST16(atk + 0x50, (uint16_t)atoi(strchr(ele, ':') + 1));
    }
    if (g_cur.on) {
        g_cur.en = en, g_cur.flags = LD32(atk + AT_FLAGS), g_cur.elements = LD32(atk + AT_ELEMENTS);
        g_cur.by = LD8(atk + 0x6d);
        g_cur.place = sane(at);
        if (g_cur.place) {
            memcpy(g_cur.p, G2H(at), sizeof g_cur.p);
            g_cur.place = isfinite(g_cur.p[0]) && isfinite(g_cur.p[1]) && isfinite(g_cur.p[2]);
        }
    }
    fnhook_call_original(c, FN_HIT_DISPATCH);
    g_cur = before;
}

// The hits on en waiting for its health: the drop shared among them (by their totals), each a number. Returns what
// of the drop they didn't take (none: they took it all)
static int hits_take(uint32_t en, int drop, uint64_t now) {
    int sum = 0, n = 0;
    for (int i = 0; i < g_nhit; i++)
        if (g_hit[i].en == en) sum += g_hit[i].total, n++;
    if (!n) return drop;
    int left = drop, seen = 0;
    for (int i = 0; i < g_nhit;) {
        Hit *h = &g_hit[i];
        if (h->en != en) { i++; continue; }
        int v = ++seen == n ? left : (int)((int64_t)drop * h->total / sum);
        left -= v;
        int e = h->total > 0 ? (int)((int64_t)v * h->elem / h->total) : 0;
        add_number(en, v - e, e, h->kind, h->crit, h->element, h->p, now);
        if (g_log > 0) LOG("[hunt] monster %08x: -%d for its hit of %d (%d of them)\n", en, v, h->total, n);
        memmove(h, h + 1, sizeof *h * (size_t)(--g_nhit - i));
    }
    return 0;
}

// Hits whose health didn't drop in HIT_WAIT frames (a party's guest): their own totals, owed to the next drops
static void hits_age(uint64_t now) {
    for (int i = 0; i < g_nhit;) {
        Hit *h = &g_hit[i];
        if (++h->frames <= HIT_WAIT) { i++; continue; }
        add_number(h->en, h->total - h->elem, h->elem, h->kind, h->crit, h->element, h->p, now);
        for (int k = 0; k < SLOTS; k++)
            if (g_slot[k].en == h->en) {
                if (now >= g_slot[k].credit_ns + CREDIT_NS) g_slot[k].credit = 0;
                g_slot[k].credit += h->total, g_slot[k].credit_ns = now;
            }
        if (g_log > 0) LOG("[hunt] monster %08x: its hit of %d shown with no drop of its health\n", h->en, h->total);
        memmove(h, h + 1, sizeof *h * (size_t)(--g_nhit - i));
    }
}

static void hits_hook(void) {
    if (g_hooked) return;
    g_hooked = true;
    fnhook_set(FN_HIT_DISPATCH, hle_register("huntaids:hit", hit_dispatch));
    fnhook_set(FN_PART_DAMAGE, hle_register("huntaids:part", hit_part));
}

// The mark's timer: ours kept (on), or ours taken back (off)
static void mark(int k, uint32_t en, uint32_t info, bool on) {
    float t = LDF(info + ST_MARK);
    if (on) {
        if (!(t < KEEP_LOW) || (LD8(en + EN_NO_MARK) & 4) || down(en, info)) return;
        STF(info + ST_MARK, KEEP_HIGH);
        if (!g_slot[k].marked && g_log) LOG("[hunt] monster %08x marked on the map (its timer was %.0f)\n", en, t);
        g_slot[k].marked = true;
    } else if (g_slot[k].marked) {
        if (t > 0 && t <= KEEP_HIGH) STF(info + ST_MARK, 0.0f);
        g_slot[k].marked = false;
        if (g_log) LOG("[hunt] monster %08x: our mark taken back (its timer was %.0f)\n", en, t);
    }
}

// Every mark of ours taken back (the option turned off, a party): the slots' monsters, if they are still there
static void unmark_all(void) {
    uint32_t sen = LD32(G_SENEMY);
    for (int k = 0; k < SLOTS; k++) {
        if (!g_slot[k].marked) continue;
        uint32_t en = sane(sen) ? LD32(sen + 0x1c + 4u * (uint32_t)k) : 0, info = sane(en) ? LD32(en + EN_INFO) : 0;
        if (en == g_slot[k].en && sane(info)) mark(k, en, info, false);
        g_slot[k].marked = false;
    }
}

static void forget(void) {
    memset(g_slot, 0, sizeof g_slot);
    g_nnum = g_nhit = 0;
    g_touched = false;
}

// p (world) on the game's picture this frame: x, y 0..1 from its left and top; false behind the camera
static bool project(const float v[16], const float pr[16], const float p[3], float *x, float *y) {
    float e[4], c[4];
    for (int j = 0; j < 4; j++) e[j] = p[0] * v[j] + p[1] * v[4 + j] + p[2] * v[8 + j] + v[12 + j];
    for (int j = 0; j < 4; j++) c[j] = e[0] * pr[j] + e[1] * pr[4 + j] + e[2] * pr[8 + j] + e[3] * pr[12 + j];
    if (!(c[3] > 1.0f)) return false;
    *x = (c[0] / c[3] + 1.0f) * 0.5f, *y = (1.0f - c[1] / c[3]) * 0.5f;
    return isfinite(*x) && isfinite(*y);
}

static void step(uint64_t now) {
    bool map = config_live_monsters_on_map(), numbers = config_live_damage_numbers(), bars = config_live_monster_bars();
    g_view_any = false;
    if (!map && !numbers && !bars) {
        if (g_touched) {  // (all turned off: our marks back, the rest forgotten, once)
            unmark_all();
            forget();
        }
        return;
    }
    if (!in_quest()) {
        if (g_touched) forget();
        return;
    }
    g_touched = true;
    if (g_log < 0) g_log = getenv("MHGU_HUNT_LOG") && atoi(getenv("MHGU_HUNT_LOG")) > 0;
    bool alone = map && hunted_alone();
    int zone = numbers || bars ? hunter_zone() : -1;
    uint32_t gc = LD32(G_GAMECONTROL);
    int lang = sane(gc) ? LD8(gc + 0xa5) : 1;  // (the game's language: the names looked up again when it changes)
    uint32_t sen = LD32(G_SENEMY);
    HuntView *v = &g_view;
    v->nnum = v->nbar = 0;
    if (numbers) hits_hook();
    for (int k = 0; sane(sen) && k < SLOTS; k++) {
        uint32_t en = LD32(sen + 0x1c + 4u * (uint32_t)k), info = sane(en) ? LD32(en + EN_INFO) : 0;
        // (the numbers are every monster's, large or small: 1.0.2, T40; the mark and the bars a large one's)
        bool big = sane(info) && large(en);
        if (!sane(info) || (!big && !numbers)) {
            g_slot[k].en = 0, g_slot[k].marked = false;  // (a monster gone takes its timer with it)
            continue;
        }
        if (g_slot[k].en != en) {  // (another monster in this slot: nothing of the one before)
            g_slot[k].en = en, g_slot[k].hp = (int32_t)LD32(info + ST_HP), g_slot[k].marked = false;
            g_slot[k].name = NULL, g_slot[k].name_lang = -1, g_slot[k].credit = 0;
            memcpy(g_slot[k].hit, G2H(info + ST_HIT), sizeof g_slot[k].hit);
        }
        if (big) mark(k, en, info, alone);
        int hp = (int32_t)LD32(info + ST_HP), most = (int32_t)LD32(info + ST_HP_MAX);
        int drop = numbers && hp < g_slot[k].hp && g_slot[k].hp - hp <= most ? g_slot[k].hp - hp : 0;
        if (drop) drop = hits_take(en, drop, now);  // (this game's own hits on it take the drop)
        if (drop && g_slot[k].credit) {  // (hits shown before it: their part of the drop was shown)
            int owed = now < g_slot[k].credit_ns + CREDIT_NS ? g_slot[k].credit : 0, paid = owed < drop ? owed : drop;
            g_slot[k].credit = owed - paid, drop -= paid;
            if (g_log > 0 && paid) LOG("[hunt] monster %08x: -%d already shown by its hits\n", en, paid);
        }
        if (drop && zone >= 0 && LD8(en + EN_ZONE) == zone) {
            float hit[3], at[3];
            memcpy(hit, G2H(info + ST_HIT), sizeof hit);
            body_middle(en, info, at);
            bool fresh = memcmp(hit, g_slot[k].hit, sizeof hit) && isfinite(hit[0]) && isfinite(hit[1]) && isfinite(hit[2]) &&
                         fabsf(hit[0] - at[0]) + fabsf(hit[1] - at[1]) + fabsf(hit[2] - at[2]) < 2500.0f;
            if (fresh) memcpy(at, hit, sizeof at), memcpy(g_slot[k].hit, hit, sizeof hit);
            add_number(en, drop, 0, HUNT_HIT_PLAIN, 0, 0, at, now);
            if (g_log)
                LOG("[hunt] monster %08x: -%d (%d of %d left) at the %s %.0f %.0f %.0f\n", en, drop, hp, most,
                    fresh ? "hit" : "body", at[0], at[1], at[2]);
        }
        g_slot[k].hp = hp;
        if (big && bars && v->nbar < HUNT_BARS && most > 0 && !down(en, info)) {
            HuntBar *b = &v->bar[v->nbar++];
            b->hp = hp, b->max = most, b->here = zone >= 0 && LD8(en + EN_ZONE) == zone;
            if (g_slot[k].name_lang != lang) g_slot[k].name = monster_name(en), g_slot[k].name_lang = lang;
            snprintf(b->name, sizeof b->name, "%s", g_slot[k].name ? g_slot[k].name : "");
        }
    }
    hits_age(now);
    for (int i = 0; i < g_nnum;)  // (a number goes a while after its last hit: Burst's runs last longer)
        if (now >= g_num[i].last + HUNT_NUMBER_NS) memmove(g_num + i, g_num + i + 1, sizeof g_num[0] * (size_t)(--g_nnum - i));
        else i++;
    if (!numbers) g_nnum = g_nhit = 0;
    uint32_t cam = LD32(G_SCAMERA);
    if (g_nnum && sane(cam)) {
        float view[16], proj[16];
        memcpy(view, G2H(cam + CAM_VIEW), sizeof view);
        memcpy(proj, G2H(cam + CAM_PROJ), sizeof proj);
        for (int i = 0; i < g_nnum; i++) {
            HuntNumber *n = &v->num[v->nnum++];
            n->shown = project(view, proj, g_num[i].p, &n->x, &n->y);
            n->value = g_num[i].value, n->id = g_num[i].id, n->born_ns = g_num[i].born, n->last_ns = g_num[i].last;
            n->kind = g_num[i].kind, n->crit = g_num[i].crit, n->element = g_num[i].element, n->elem = g_num[i].elem;
            n->hits = g_num[i].hits;
        }
    }
    v->number_size = config_live_number_size(), v->number_colour = config_live_number_colour();
    v->number_style = config_live_number_style();
    g_view_any = v->nnum || v->nbar;
}

void huntaids_frame(void) {
    static int cost = -1;
    if (cost < 0) cost = getenv("MHGU_HUNT_COST") && atoi(getenv("MHGU_HUNT_COST")) > 0;
    uint64_t t0 = plat_now_ns();
    step(t0);
    if (cost) {
        static uint64_t sum, most, since, frames;
        uint64_t t = plat_now_ns() - t0;
        sum += t, frames++, most = t > most ? t : most;
        if (!since) since = t0;
        if (t0 - since >= 10000000000ull) {
            LOG("[hunt] cost: %.2f us a frame on average, %.1f at most (%llu frames, %d numbers, %d bars now)\n",
                sum / 1e3 / (double)frames, most / 1e3, (unsigned long long)frames, g_view.nnum, g_view.nbar);
            sum = most = frames = 0, since = t0;
        }
    }
}

bool huntaids_view(HuntView *out) {
    if (!g_view_any) return false;
    *out = g_view;
    return true;
}

bool huntaids_any(void) { return g_view_any; }
