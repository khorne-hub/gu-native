// The Smithy's weapon tree over the game (weapon_tree.h): the app's side. The game's weapon tables and names come from
// the RomFS (the files the game reads, a mod's copy if one replaces them), the player's box and the Smithy's weapon
// from runtime/hle/weapontree.c, and the panel is runtime/weapon_tree_panel.cpp (the same as MH4U's).
//
// The tables (nativeNX/table, one set a class NN: 00 the great sword .. 14 the charge blade, no 05), each a float
// version, a u32 count, then its records:
// - weaponNNBaseData.wNNd, one a base (a weapon's line of levels, its id the record's index): +0 u16 id, +17 the
//   element (0 none, 1 fire, 2 water, 3 thunder, 4 dragon, 5 ice, 6 poison, 7 paralysis, 8 sleep, 9 blast: Alatreon's
//   and Fatalis' 4, Snowclad's 5; not the bowguns'), +18 the dual blades' second one; the last 10 bytes five (level, base) pairs: at that level it upgrades
//   into that base's level 1. Records of 32 bytes (the bowguns), 33 or 34 (the classes with a phial, shells, a kinsect)
//   - always the same at the start and at the end.
// - weaponNNLevelData.wNNd, one a (base, level): +4 base, +5 level; a melee weapon's attack u16 +8, affinity s8 +11,
//   element value +12 (the dual blades' second +14), slots in its last byte; a bow's attack +6, affinity +9, element
//   +10, slots +12; a bowgun's attack +6, affinity +9, slots +10. As the game shows them (MHGU shows the true values).
// - weaponCreateWNN.wcd, the recipes that forge a base from scratch: +2 u16 the base.
// - <lang>/table/weaponNNMsgData.wNNm: a u32 a base, its first string / 6 in weaponNNMsgData_<lang>.gmd, whose six
//   strings a base are its name and text, its final level's name and text, and two more.
// - the details (1.0.2, with Heavenfall's notes on these tables, core/equipment/mhgen/gear_weapons.py and
//   gear_recipes.py, by popzol and BlitzWOLF, GPL-3.0: the facts, the code is this port's): a base's rarity - 1 at +11,
//   its price u32 +12 (forging costs 3/2 of it, an upgrade into it the price, a level L the price x L, from the level
//   at +16 on the price x (L + 9)); a level's sharpness row +6 (in table/kireajiData.kad: where each colour ends, seven
//   u16) shown up to 150 + 25 x the byte +7, its defense +10 (a bow's or a bowgun's +8);
//   weaponCreateWNN.wcd (38 bytes): u16 kind (1 the ordinary row), u16 base, u16, eight (u16 item, u8 count, u8 flag)
//   from +6, the first five the materials; weaponProcessWNN.wpd (32 bytes): u16 base, u16 level, four (u16 item, u8
//   count) from +4, a material category u16 and its value u16 at +16; the items' names: <lang>/table/itemData_<lang>.gmd
//   (two strings an item), the categories': itemCategoryTypeData_<lang>.gmd.
// - the classes' icons: <lang>/GUI/99_texture/HD_cmn_icon_GSM_NOMIP.tex, the game's grey icon sheet (RGBA8, 512 x 1024,
//   the console's block-linear layout), which the game tints by rarity: its large icons from y 416, 48 pixels a cell, ten
//   a row, the weapons' the cells 55 to 69 (the game's table, by class: 55 57 59 61 65 63 63 56 67 62 66 58 60 68 69).
//   Read from the player's own copy of the game; none of it is in the port.
// Checked against the trees players know (Iron Sword: Buster Sword at level 3, Ravager Blade at 4, Lagiacrus Blade
// and Clero Blade at 5; Nargacuga's weapons with their affinity, Plesioth's water, Diablos' negative affinity), and in
// the game's Smithy (Petrified Blade level 2: 1700z and Ore value 4; the sharpness of three level 1 weapons).
#include "weapon_tree.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "hle/input.h"
#include "imgui.h"
#include "logwriter.h"
#include "overlay.h"
#include "platform.h"
#include "weapon_tree_panel.h"

extern "C" const char *romfs_host_path(const char *rpath);  // (runtime/romfs.h, a C header)
extern "C" uint32_t tegra_default_block_height_log2(uint32_t height_in_blocks);  // (runtime/nvn/tegra.h, a C header)
extern "C" void tegra_deswizzle(const uint8_t *src, uint8_t *dst, uint32_t wb, uint32_t hb, uint32_t d, uint32_t bpb,
                                uint32_t block_height_log2, uint32_t block_depth_log2);
extern "C" SDL_Window *ui_window(void);                    // (runtime/rt.h)
#define LOG(...) rt_logf(__VA_ARGS__)

namespace {

const uint64_t MS = 1000000ull;
const float ANIM_MS = 130.f;
// the classes by table number (no 05), in the app's words (weapon_tree_panel::Words has their Spanish)
const char *const CLASS_NAMES[15] = {"Great Sword", "Sword and Shield", "Hammer", "Lance", "Heavy Bowgun", "",
                                     "Light Bowgun", "Long Sword", "Switch Axe", "Gunlance", "Bow", "Dual Blades",
                                     "Hunting Horn", "Insect Glaive", "Charge Blade"};
const char *const LANG_FOLDER[6] = {"eng", "eng", "fre", "spa", "ger", "ita"};  // (the font has Latin letters only)
// the classes by table number in the panel's (its icons' order), and the word for a category's value in each language
const int PANEL_CLASS[15] = {weapon_tree_panel::kGreatSword, weapon_tree_panel::kSwordAndShield, weapon_tree_panel::kHammer,
                             weapon_tree_panel::kLance, weapon_tree_panel::kHeavyBowgun, -1, weapon_tree_panel::kLightBowgun,
                             weapon_tree_panel::kLongSword, weapon_tree_panel::kSwitchAxe, weapon_tree_panel::kGunlance,
                             weapon_tree_panel::kBow, weapon_tree_panel::kDualBlades, weapon_tree_panel::kHuntingHorn,
                             weapon_tree_panel::kInsectGlaive, weapon_tree_panel::kChargeBlade};
const int ICON_CELL[weapon_tree_panel::kWeaponClasses] = {55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 66, 67, 68, 69};
const char *const VALUE_WORD[6] = {"value", "value", "valeur", "valor", "Wert", "valore"};

// ---- the tables
std::string read_file(const char *rpath) {
    const char *path = romfs_host_path(rpath);
    FILE *f = path ? fopen(path, "rb") : nullptr;
    if (!f) return std::string();
    std::string d;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && d.size() < (16u << 20)) d.append(buf, n);
    fclose(f);
    return d;
}
uint32_t u32(const std::string &d, size_t o) {
    uint32_t v = 0;
    if (o + 4 <= d.size()) memcpy(&v, d.data() + o, 4);
    return v;
}
// A table's records: count and size (the file's length over the count); false when it isn't one
bool records(const std::string &d, size_t *count, size_t *size) {
    if (d.size() < 8) return false;
    size_t n = u32(d, 4);
    if (!n || (d.size() - 8) % n) return false;
    *count = n, *size = (d.size() - 8) / n;
    return true;
}
std::vector<std::string> gmd(const std::string &d) {  // (huntaids.c's reader: the strings in order)
    std::vector<std::string> out;
    if (d.size() < 0x30 || memcmp(d.data(), "GMD", 3)) return out;
    uint32_t n = u32(d, 0x18), bytes = u32(d, 0x20);
    if (bytes > d.size() || n > 0x10000) return out;
    for (size_t p = d.size() - bytes; p < d.size() && out.size() < n;) {
        size_t e = d.find('\0', p);
        if (e == std::string::npos) e = d.size();
        out.emplace_back(d, p, e - p);
        p = e + 1;
    }
    return out;
}

// the game's element numbers in weapon_tree::Element's (its 4 is dragon, its 5 ice)
int element(int e) {
    if (e == 4) return weapon_tree::kDragon;
    if (e == 5) return weapon_tree::kIce;
    return e >= 1 && e <= 9 ? e : weapon_tree::kElementNone;
}

struct Level {
    int attack = 0, affinity = 0, element_value = 0, element2_value = 0, slots = 0, defense = 0;
    int sharpness = -1, cut = 0;           // (melee) its sharpness row, shown up to `cut`
    weapon_tree::Recipe recipe;            // the upgrade to it (level 1: into the base, from the one before)
};
struct Base {
    bool real = false, forge = false;
    std::string name, final_name;
    int element = 0, element2 = 0;
    int rarity = 0, price = 0, change_level = 0;
    std::vector<std::pair<int, int>> kids;  // (level, base)
    std::vector<Level> levels;              // [level - 1]
    weapon_tree::Recipe create;             // forged from scratch
};
struct Tables {
    int cls = -1, lang = -1;
    bool ok = false;
    std::vector<Base> bases;
    std::vector<std::array<int, 7>> sharpness;  // table/kireajiData.kad
};

// A recipe's materials: `n` (u16 item, u8 count) from `at`, `step` bytes apart, named from the items' strings
void materials(const uint8_t *r, int at, int n, int step, const std::vector<std::string> &items, weapon_tree::Recipe *out) {
    for (int k = 0; k < n; k++) {
        const int item = r[at + step * k] | r[at + step * k + 1] << 8, count = r[at + step * k + 2];
        if (!item || !count) continue;
        weapon_tree::Material m;
        m.item = item, m.count = count;
        if (2 * (size_t)item < items.size()) m.name = items[2 * item];
        out->items.push_back(m);
    }
}

Tables load_tables(int cls, int lang) {
    Tables t;
    t.cls = cls, t.lang = lang;
    char p[128];
    snprintf(p, sizeof p, "nativeNX/table/weapon%02dBaseData.w%02dd", cls, cls);
    std::string bd = read_file(p);
    snprintf(p, sizeof p, "nativeNX/table/weapon%02dLevelData.w%02dd", cls, cls);
    std::string ld = read_file(p);
    snprintf(p, sizeof p, "nativeNX/table/weaponCreateW%02d.wcd", cls);
    std::string cd = read_file(p);
    snprintf(p, sizeof p, "nativeNX/table/weaponProcessW%02d.wpd", cls);
    std::string pd = read_file(p);
    std::string kd = read_file("nativeNX/table/kireajiData.kad");
    const char *lf = LANG_FOLDER[lang >= 1 && lang <= 5 ? lang : 1];
    snprintf(p, sizeof p, "nativeNX/%s/table/weapon%02dMsgData.w%02dm", lf, cls, cls);
    std::string md = read_file(p);
    snprintf(p, sizeof p, "nativeNX/%s/table/weapon%02dMsgData_%s.gmd", lf, cls, lf);
    std::vector<std::string> names = gmd(read_file(p));
    snprintf(p, sizeof p, "nativeNX/%s/table/itemData_%s.gmd", lf, lf);
    std::vector<std::string> items = gmd(read_file(p));
    snprintf(p, sizeof p, "nativeNX/%s/table/itemCategoryTypeData_%s.gmd", lf, lf);
    std::vector<std::string> cats = gmd(read_file(p));
    size_t nb, bs, nl, ls, nc = 0, cs = 0, np = 0, ps = 0, nk = 0, ks = 0, nm, ms;
    if (!records(bd, &nb, &bs) || bs < 32 || nb > 256 || !records(ld, &nl, &ls) || ls < 15) {
        LOG("[weapon tree] class %02d: its tables can't be read (%s)\n", cls, bd.empty() ? "missing" : "unknown sizes");
        return t;
    }
    records(cd, &nc, &cs);
    if (!records(pd, &np, &ps) || ps < 20) np = 0;
    if (records(kd, &nk, &ks) && ks == 14) {
        t.sharpness.resize(nk);
        for (size_t i = 0; i < nk; i++)
            for (int k = 0; k < 7; k++) t.sharpness[i][k] = (uint8_t)kd[8 + 14 * i + 2 * k] | (uint8_t)kd[9 + 14 * i + 2 * k] << 8;
    }
    const bool ranged = cls == 4 || cls == 6, bow = cls == 10;
    t.bases.resize(nb);
    for (size_t i = 0; i < nb; i++) {
        const uint8_t *r = (const uint8_t *)bd.data() + 8 + i * bs;
        Base &b = t.bases[i];
        b.element = ranged ? 0 : r[17];
        b.element2 = cls == 11 ? r[18] : 0;
        b.rarity = r[11] + 1, b.price = (int)u32(bd, 8 + i * bs + 12), b.change_level = r[16];
        for (int k = 0; k < 5; k++) {
            int lv = r[bs - 10 + 2 * k], kid = r[bs - 9 + 2 * k];
            if (kid > 0 && (size_t)kid < nb && lv > 0) b.kids.push_back({lv, kid});
        }
        uint32_t m = records(md, &nm, &ms) && ms == 4 && i < nm ? u32(md, 8 + 4 * i) : ~0u;
        if (m < 0x4000 && 6 * m + 2 < names.size()) b.name = names[6 * m], b.final_name = names[6 * m + 2];
    }
    for (size_t i = 0; i < nl; i++) {
        const uint8_t *r = (const uint8_t *)ld.data() + 8 + i * ls;
        int base = r[4], lv = r[5];
        if ((size_t)base >= nb || lv < 1 || lv > 64) continue;
        Base &b = t.bases[base];
        if ((int)b.levels.size() < lv) b.levels.resize(lv);
        Level &l = b.levels[lv - 1];
        if (ranged || bow) {
            l.attack = r[6] | r[7] << 8, l.affinity = (int8_t)r[9], l.slots = r[bow ? 12 : 10], l.defense = r[8];
            l.element_value = bow ? r[10] : 0;
        } else {
            l.attack = r[8] | r[9] << 8, l.affinity = (int8_t)r[11], l.element_value = r[12], l.slots = r[ls - 1];
            l.element2_value = cls == 11 && ls > 14 ? r[14] : 0;
            l.defense = r[10], l.sharpness = r[6], l.cut = 150 + 25 * r[7];
        }
        if (l.slots > 3) l.slots = 3;
    }
    for (size_t i = 0; cs >= 26 && i < nc; i++) {  // (the create rows: kind 1 the ordinary one)
        const uint8_t *r = (const uint8_t *)cd.data() + 8 + i * cs;
        size_t base = r[2] | r[3] << 8;
        if (base >= nb) continue;
        Base &b = t.bases[base];
        b.forge = true;
        if ((r[0] | r[1] << 8) != 1 || !b.create.items.empty()) continue;
        materials(r, 6, 5, 4, items, &b.create);
        b.create.zenny = b.price * 3 / 2;
    }
    for (size_t i = 0; i < np; i++) {  // (the upgrade rows: to a base's level)
        const uint8_t *r = (const uint8_t *)pd.data() + 8 + i * ps;
        size_t base = r[0] | r[1] << 8;
        int lv = r[2] | r[3] << 8;
        if (base >= nb || lv < 1 || lv > (int)t.bases[base].levels.size()) continue;
        Base &b = t.bases[base];
        weapon_tree::Recipe &rc = b.levels[lv - 1].recipe;
        materials(r, 4, 4, 3, items, &rc);
        const int cat = r[16] | r[17] << 8, value = r[18] | r[19] << 8;
        if (cat > 0 && value > 0 && (size_t)cat < cats.size())
            rc.points = cats[cat] + " (" + VALUE_WORD[lang >= 1 && lang <= 5 ? lang : 1] + " " + std::to_string(value) + ")";
        rc.level = lv;
        rc.zenny = lv == 1 ? b.price : b.price * (!b.change_level || lv < b.change_level ? lv : lv + 9);
    }
    for (size_t i = 1; i < nb; i++) {
        Base &b = t.bases[i];
        b.real = !b.levels.empty() && !b.name.empty() && b.name != "(None)";
    }
    t.ok = true;
    return t;
}

// ---- the classes' icons: the game's sheet, its weapons' cells cut and made 96 pixels a side (whole pixels)
weapon_tree_panel::Icons g_icons;
int g_icons_lang = -1;

void load_icons(int lang) {
    if (g_icons_lang == lang) return;
    g_icons_lang = lang;
    char p[128];
    snprintf(p, sizeof p, "nativeNX/%s/GUI/99_texture/HD_cmn_icon_GSM_NOMIP.tex", LANG_FOLDER[lang >= 1 && lang <= 5 ? lang : 1]);
    const std::string d = read_file(p);
    // TEX: +8 the levels (bits 0-5), the width (6-18) and the height (19-31), +12 the format (bits 8-15: 7 RGBA8)
    const uint32_t b = u32(d, 8), c = u32(d, 12);
    const int w = (b >> 6) & 0x1fff, h = (b >> 19) & 0x1fff;
    if (d.size() < 0x18 || memcmp(d.data(), "TEX", 4) || ((c >> 8) & 0xff) != 7 || w < 256 || w % 256 || h != 2 * w ||
        d.size() < 0x18 + (size_t)w * h * 4) {
        LOG("[weapon tree] the game's icons can't be read (%s): drawn ones\n", d.empty() ? "no file" : "not the sheet known");
        return;
    }
    std::vector<uint8_t> lin((size_t)w * h * 4);
    tegra_deswizzle((const uint8_t *)d.data() + 0x18, lin.data(), w, h, 1, 4, tegra_default_block_height_log2(h), 0);
    const int sc = w / 256, cell = 24 * sc, top = 208 * sc, out = 96, k = out / cell, n = weapon_tree_panel::kWeaponClasses;
    weapon_tree_panel::Icons ic;
    ic.cell = out;
    ic.rgba.assign((size_t)out * out * n * 4, 0);
    for (int j = 0; j < n; j++) {
        const int cx = ICON_CELL[j] % 10 * cell, cy = top + ICON_CELL[j] / 10 * cell;
        for (int y = 0; y < out; y++)
            for (int x = 0; x < out; x++)
                memcpy(&ic.rgba[((size_t)y * out * n + j * out + x) * 4], &lin[((size_t)(cy + y / k) * w + cx + x / k) * 4], 4);
    }
    ic.serial = g_icons.serial + 1;
    g_icons = std::move(ic);
    LOG("[weapon tree] the game's icons read (%dx%d sheet, %d pixel cells)\n", w, h, cell);
}

// ---- the state: the input thread asks (g_req), the game thread draws and loads (draw_frame)
std::mutex g_lock;
std::atomic<bool> g_open{false};       // open, or asked to open (the input thread's view)
std::atomic<bool> g_drawing{false};    // drawn now (open or fading out)
struct Req {
    int kind;  // 0..2 a finger (down, move, up), 3 a step of the cursor
    float x, y;
    int dx, dy;
};
std::vector<Req> g_req;     // (g_lock)
bool g_opened_by_pad;       // (g_lock)
bool g_want_open;           // (g_lock) the input thread asked for it to open
// (the game thread's own, inside the overlay's frame)
Tables g_tables;
weapon_tree_panel::Content g_content;
weapon_tree_panel::State g_state;
bool g_shown;               // the panel is up (the game thread's view)
uint64_t g_in, g_out;       // when it opened, when it closed (the animation)
uint32_t g_serial;          // the Smithy's weapon it shows
bool g_finger_on;           // (input thread) a finger went down on the panel and is held
SDL_FingerID g_finger;
uint64_t g_held_back;       // (input thread) buttons held as it opened or closed: the game gets none until let go
int g_rep_dx, g_rep_dy;
uint64_t g_rep_at;

// The tree of the Smithy's weapon now, into g_content (the game thread); false when it shows none
bool load(const WtSmithy &s) {
    if (!s.shown || s.cls < 0 || s.cls > 14 || s.cls == 5) return false;
    int lang = s.language >= 1 && s.language <= 5 ? s.language : 1;
    if (!g_tables.ok || g_tables.cls != s.cls || g_tables.lang != lang) g_tables = load_tables(s.cls, lang);
    if (!g_tables.ok || s.base <= 0 || (size_t)s.base >= g_tables.bases.size() || !g_tables.bases[s.base].real) return false;
    const int n = (int)g_tables.bases.size();
    std::vector<uint8_t> have(n, 0);
    int worn = -1, worn_level = 0;
    weapontree_box(s.cls, have.data(), n, &worn, &worn_level);
    weapon_tree::Class cls;
    cls.weapons.resize(n);
    std::vector<uint8_t> owned(n, 0);
    for (int i = 0; i < n; i++) {
        const Base &b = g_tables.bases[i];
        weapon_tree::Weapon &w = cls.weapons[i];
        w.real = b.real;
        if (!b.real) continue;
        owned[i] = have[i] != 0;
        // (the level it shows: the Smithy's, the one worn, the highest in the box; none for the others)
        int level = i == s.base ? s.level : 0;
        if (i == worn) level = std::max(level, worn_level);
        level = std::max(level, (int)have[i]);
        level = std::clamp(level, 0, (int)b.levels.size());
        w.level = level, w.levels = (int)b.levels.size();
        w.name = level == w.levels && !b.final_name.empty() && b.final_name != "(None)" ? b.final_name : b.name;
        const Level &l = b.levels[std::max(level, 1) - 1];
        w.attack = l.attack, w.affinity = l.affinity, w.slots = l.slots;
        w.element = element(b.element), w.element_value = w.element ? l.element_value : 0;
        w.element2 = element(b.element2), w.element2_value = w.element2 ? l.element2_value : 0;
        w.forge = b.forge;
        w.rarity = b.rarity, w.defense = l.defense;
        if (l.sharpness >= 0 && (size_t)l.sharpness < g_tables.sharpness.size())
            for (int k = 0; k < 7; k++) w.sharpness.push_back(std::min(g_tables.sharpness[l.sharpness][k], l.cut));
        w.create = b.create, w.upgrade = b.levels[0].recipe;
        if (level >= 1 && level < w.levels) w.next = b.levels[level].recipe;  // (its next level's, when it is had)
        if (w.upgrade.level == 1) w.upgrade.level = 0;
        for (const auto &k : b.kids) w.children.push_back(k.second);
    }
    cls.sharpness_full = 450;
    for (int i = 0; i < n; i++)  // (the level of its parent that upgrades into it)
        for (const auto &k : g_tables.bases[i].kids)
            if (k.second < n && !cls.weapons[k.second].from_level) cls.weapons[k.second].from_level = k.first;
    weapon_tree::View view = weapon_tree::Build(cls, s.base, owned, worn);
    if (view.nodes.empty()) return false;
    const bool same = g_shown && s.serial == g_serial;
    const int focus = g_state.focus;
    g_content.cls = std::move(cls);
    g_content.view = std::move(view);
    g_content.spanish = config_language() == 1;
    g_content.class_name = weapon_tree_panel::Words(CLASS_NAMES[s.cls], g_content.spanish);
    g_content.weapon_class = PANEL_CLASS[s.cls];
    load_icons(lang);
    g_content.icons = &g_icons;
    if (!same) {
        g_state = weapon_tree_panel::State{};
        LOG("[weapon tree] class %02d base %d level %d (worn %d level %d): %d weapons shown\n%s", s.cls, s.base, s.level, worn,
            worn_level, (int)g_content.view.nodes.size(), weapon_tree::Text(g_content.cls, g_content.view).c_str());
    } else {
        g_state.focus = focus;
    }
    g_serial = s.serial;
    return true;
}

float ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return 1 - (1 - t) * (1 - t);
}

}  // namespace

extern "C" {

bool weapon_tree_available(void) { return !g_open.load() && weapontree_smithy().shown; }
bool weapon_tree_is_open(void) { return g_open.load(); }
bool weapon_tree_drawing(void) { return g_drawing.load() || g_open.load(); }

void weapon_tree_open(bool by_pad) {
    if (g_open.load() || !weapontree_smithy().shown) return;
    std::lock_guard<std::mutex> l(g_lock);
    g_open = true, g_want_open = true, g_opened_by_pad = by_pad, g_req.clear();
    g_finger_on = false;
    LOG("[weapon tree] opened (%s)\n", by_pad ? "the controller" : "the button");
}

void weapon_tree_close(void) {
    if (!g_open.exchange(false)) return;
    std::lock_guard<std::mutex> l(g_lock);
    g_want_open = false, g_req.clear(), g_finger_on = false;
    LOG("[weapon tree] closed\n");
}

bool weapon_tree_back(void) {
    if (!g_open.load()) return false;
    weapon_tree_close();
    return true;
}

uint64_t weapon_tree_buttons(uint64_t b, int lx, int ly) {
    static uint64_t was;
    const uint64_t down = b & ~was;
    was = b;
    const uint64_t R3 = 1ull << BTN_RSTICK, BB = 1ull << BTN_B;
    g_held_back &= b;  // (let go: the game gets it again)
    static int trace = -1;  // (MHGU_WT_LOG=2: each press while it is open)
    if (trace < 0) trace = getenv("MHGU_WT_LOG") && atoi(getenv("MHGU_WT_LOG")) > 1;
    if (trace && down && g_open.load()) LOG("[weapon tree] pressed %llx while open\n", (unsigned long long)down);
    if (g_open.load()) {
        if (down & (R3 | BB)) {
            weapon_tree_close();
            g_held_back |= b;
            return 0;
        }
        // (the d-pad, the stick or the move keys: a step, held a repeat)
        const int dx = (b & (1ull << BTN_LEFT)) || lx < -18000 ? -1 : (b & (1ull << BTN_RIGHT)) || lx > 18000 ? 1 : 0;
        const int dy = (b & (1ull << BTN_UP)) || ly > 18000 ? -1 : (b & (1ull << BTN_DOWN)) || ly < -18000 ? 1 : 0;
        const uint64_t now = plat_now_ns();
        if ((dx || dy) && (dx != g_rep_dx || dy != g_rep_dy || now >= g_rep_at)) {
            g_rep_at = now + (dx != g_rep_dx || dy != g_rep_dy ? 380 : 140) * MS;
            std::lock_guard<std::mutex> l(g_lock);
            g_req.push_back({3, 0, 0, dx, dy != 0 && dx == 0 ? dy : 0});
        }
        g_rep_dx = dx, g_rep_dy = dy;
        g_held_back |= b;
        return 0;
    }
    if ((down & R3) && weapon_tree_available()) {
        weapon_tree_open(true);
        g_held_back |= b;
        return 0;
    }
    if (weapontree_smithy().shown) b &= ~R3;  // (in the Smithy the click is the tree's)
    return b & ~g_held_back;
}

static bool finger(int kind, float x, float y, SDL_FingerID id) {
    if (!g_open.load()) return false;
    std::lock_guard<std::mutex> l(g_lock);
    if (kind == 0) {
        if (g_finger_on) return true;
        g_finger_on = true, g_finger = id;
    } else if (!g_finger_on || id != g_finger) {
        return true;  // (a finger that was down before it opened, or another one: nothing)
    }
    if (kind == 2) g_finger_on = false;
    g_req.push_back({kind, x, y, 0, 0});
    return true;
}

bool weapon_tree_event(const SDL_Event *e) {
    if (!g_open.load()) return false;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (!e->key.repeat && (e->key.scancode == SDL_SCANCODE_AC_BACK || e->key.scancode == SDL_SCANCODE_ESCAPE)) {
            weapon_tree_close();
            return true;
        }
        return false;  // (the other keys: the game's buttons, which weapon_tree_buttons takes while it is open)
    case SDL_EVENT_FINGER_DOWN: return finger(0, e->tfinger.x, e->tfinger.y, e->tfinger.fingerID);
    case SDL_EVENT_FINGER_MOTION: return finger(1, e->tfinger.x, e->tfinger.y, e->tfinger.fingerID);
    case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: return finger(2, e->tfinger.x, e->tfinger.y, e->tfinger.fingerID);
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_MOTION: {
        // (a desktop's mouse, as a finger: the left button; a finger's own copy as the mouse is left out)
        SDL_MouseID which = e->type == SDL_EVENT_MOUSE_MOTION ? e->motion.which : e->button.which;
        if (which == SDL_TOUCH_MOUSEID) return true;
        SDL_Window *win = ui_window();  // (the game's window: a test's events name none)
        int ww = 0, wh = 0;
        if (win) SDL_GetWindowSize(win, &ww, &wh);
        if (ww <= 0 || wh <= 0) return true;
        if (e->type == SDL_EVENT_MOUSE_MOTION) {
            if (e->motion.state & SDL_BUTTON_LMASK) finger(1, e->motion.x / ww, e->motion.y / wh, (SDL_FingerID)-7);
        } else if (e->button.button == SDL_BUTTON_LEFT) {
            finger(e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0 : 2, e->button.x / ww, e->button.y / wh, (SDL_FingerID)-7);
        } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_RIGHT) {
            weapon_tree_close();  // (a right click: B)
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: return true;
    default: return false;
    }
}

void weapon_tree_draw(float w, float h) {
    if (w <= 0 || h <= 0) return;
    const uint64_t now = plat_now_ns();
    bool want, by_pad;
    std::vector<Req> req;
    {
        std::lock_guard<std::mutex> l(g_lock);
        want = g_want_open, by_pad = g_opened_by_pad;
        req.swap(g_req);
    }
    const WtSmithy s = weapontree_smithy();
    if (want && !g_shown) {  // (it opens: the tree of the Smithy's weapon now)
        if (load(s)) {
            g_shown = true, g_in = now, g_out = 0;
        } else {
            LOG("[weapon tree] the Smithy's weapon has no tree (class %d base %d level %d)\n", s.cls, s.base, s.level);
            weapon_tree_close();
            want = false;
        }
    } else if (!want && g_shown) {
        g_shown = false, g_out = now;
    }
    if (g_shown) {
        if (!s.shown && now - g_in > 300 * MS) {  // (the Smithy closed under it, or its list went)
            weapon_tree_close();
            g_shown = false, g_out = now;
        } else if (s.shown && s.serial != g_serial) {
            load(s);  // (the Smithy's cursor moved: its weapon's tree)
        }
    }
    if (g_shown) {
        for (const Req &r : req) {
            if (r.kind == 3) {
                weapon_tree_panel::Move(r.dx, r.dy, g_content, g_state);
                continue;
            }
            bool close = false;
            weapon_tree_panel::Finger(r.kind == 0 ? weapon_tree_panel::kDown : r.kind == 1 ? weapon_tree_panel::kMove
                                                                                            : weapon_tree_panel::kUp,
                                      r.x * w, r.y * h, g_content, g_state, &close);
            if (close) {
                weapon_tree_close();
                g_shown = false, g_out = now;
                break;
            }
        }
    }
    float a;
    if (g_shown) a = ease((now - g_in) / (ANIM_MS * MS));
    else if (g_out && now - g_out < (uint64_t)(ANIM_MS * MS)) a = 1 - ease((now - g_out) / (ANIM_MS * MS));
    else a = 0;
    g_drawing = a > 0;
    if (a <= 0) return;
    (void)by_pad;
    weapon_tree_panel::Draw(ImGui::GetForegroundDrawList(), w, h, a, overlay_last_input() == USED_PAD || by_pad, g_content,
                            g_state);
}

}  // extern "C"
