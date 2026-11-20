// The hunting aids' drawing (runtime/hunt_aids.h): the damage numbers over the monsters and the monsters' health bars at
// the top (MH4U for Android's, the same look), on the overlay's background list (over the game's picture, under the
// touch controls and the menus), after the FPS plate. The overlay is drawn over each picture presented, the game's and frame generation's made ones alike,
// after the upscaler: none of them ever samples it.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hud_parts.h"
#include "config.h"
#include "hunt_aids.h"
#include "imgui.h"
#include "logwriter.h"
#include "perfstats.h"
#include "touch.h"

extern "C" uint64_t plat_now_ns(void);

namespace {

// The numbers: one size for all (Dear ImGui bakes the font at each size it is asked for: a size that changed every frame
// would bake glyphs every frame), of the screen's height by [Hunting Aids] DamageNumberSize (0 Medium, the default, 1
// Small, 2 Large: MH4U for Android's three), and a colour by DamageNumberColour, always with a dark outline
constexpr float SIZES[] = {.075f, .04f, .10f};
constexpr ImU32 COLOURS[] = {IM_COL32(255, 255, 255, 0), IM_COL32(255, 214, 40, 0), IM_COL32(255, 80, 64, 0),
                             IM_COL32(110, 200, 255, 0), IM_COL32(110, 230, 110, 0)};
constexpr float RISE = .08f;     // how far a number rises while it shows, of the screen's height (eased: fast at first)
constexpr float FADE = .6f;      // it fades from 60% of its time on
constexpr float JITTER = .006f;  // numbers at one place: up to 4 steps of this (of the width) apart, by their id

// The bars: MH4U for Android's, the same in every measure and colour (its app_native/hunt_bars.cpp; the owner: "identical
// to the 4U's"), of the game's picture's height where MH4U's are of its top screen's: a plate 46% long and 3% tall, one
// under the other, a third of a plate apart; the plate the FPS counter's (wine, a black edge, red and moss rules at the
// top and bottom), a groove in it the health fills in the FPS counter's green (over half), gold (over a quarter) or red,
// the numbers ("1234 / 2546") in ink at its right end; a monster in another zone of the map fainter. The monster's name
// (the owner's, both ports alike) at its left end in the numbers' ink and size, the groove between them, the name
// shortened with an ellipsis when it would take more than kName of the room left of the numbers
constexpr float kLength = .46f;
constexpr float kShortest = .32f;  // shorter than this beside the hunter's bars, they go under them
constexpr float kHeight = .03f;
constexpr float kMargin = .02f;    // from the top and right edges, and from the HUD's parts they keep clear of
constexpr float kGap = .3f;        // between two plates, of a plate's height
constexpr float kAway = .55f;      // a monster in another zone of the map
constexpr float kText = .64f;      // the numbers, of a plate's height
constexpr float kNumbers = 4.4f;   // the room they take at the plate's right end, of a plate's height
constexpr float kName = .45f;      // the most of the room left of the numbers a name takes (the groove keeps the rest)
constexpr ImU32 kWine = IM_COL32(50, 25, 24, 255), kRule = IM_COL32(176, 34, 30, 255), kMoss = IM_COL32(80, 142, 54, 255),
                kDeep = IM_COL32(28, 20, 15, 255), kInk = IM_COL32(244, 232, 200, 255),
                kGood = IM_COL32(132, 204, 96, 255), kGold = IM_COL32(222, 186, 96, 255),
                kBad = IM_COL32(236, 84, 64, 255);

ImU32 alpha_of(ImU32 c, float a) {
    return (c & 0x00ffffffu) | ((ImU32)std::clamp(((c >> 24) & 0xff) * a, 0.f, 255.f) << 24);
}

// A name in at most `most` pixels at `size`: whole, or cut at a character (UTF-8) with an ellipsis ("..." where the font
// has no U+2026); its width (0: no name, or not even one letter fits)
float fit_name(ImFont *font, float size, const char *in, float most, char *out, size_t cap) {
    out[0] = 0;
    if (!in || !*in || most <= 0) return 0;
    float w = font->CalcTextSizeA(size, 1e9f, 0, in).x;
    if (w <= most) {
        std::snprintf(out, cap, "%s", in);
        return w;
    }
    const char *dots = font->IsGlyphInFont(0x2026) ? "\xE2\x80\xA6" : "...";
    for (size_t n = std::strlen(in); n > 0;) {
        do n--; while (n > 0 && (in[n] & 0xC0) == 0x80);  // (back a whole character)
        size_t k = n;
        while (k > 0 && in[k - 1] == ' ') k--;  // (no space before the ellipsis)
        if (!k) break;
        char t[64];
        std::snprintf(t, sizeof t, "%.*s%s", (int)std::min(k, sizeof t - 8), in, dots);
        w = font->CalcTextSizeA(size, 1e9f, 0, t).x;
        if (w <= most) {
            std::snprintf(out, cap, "%s", t);
            return w;
        }
    }
    return 0;
}

// The game's picture in the window (touch.cpp's picture(): as wide as it fits in its own shape, centred)
struct Pic {
    float x0, y0, w, h;
};
Pic picture(float w, float h) {
    float a = g_render_aspect > 0 ? g_render_aspect : w / h, pw = w, ph = w / a;
    if (ph > h) ph = h, pw = h * a;
    return {(w - pw) * .5f, (h - ph) * .5f, pw, ph};
}

void outlined(ImDrawList *dl, ImFont *font, float size, ImVec2 at, ImU32 colour, int alpha, const char *text, float o) {
    static const float DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int k = 0; k < 8; ++k)
        dl->AddText(font, size, ImVec2(at.x + DX[k] * o, at.y + DY[k] * o), IM_COL32(0, 0, 0, alpha * 3 / 4), text);
    dl->AddText(font, size, at, (colour & 0x00ffffffu) | IM_COL32(0, 0, 0, alpha), text);
}

// By hit (1.0.2, the owner's "complete" numbers, the colours the players asked for): a weak point (45 or more) orange, a
// normal hit white, a hard part (under 25, where a weapon bounces) grey, fixed damage (shells, bombs) blue-green, a drop
// with no hit read (poison, another hunter's) white; a critical hit a red spark at the top right, a negative one a blue
// line under it; the element's damage a second, smaller number in its colour
constexpr ImU32 KIND_COLOURS[] = {IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), IM_COL32(255, 168, 36, 0),
                                  IM_COL32(150, 150, 150, 0), IM_COL32(64, 214, 196, 0)};
constexpr ImU32 ELEMENT_COLOURS[] = {IM_COL32(220, 220, 220, 0), IM_COL32(255, 104, 56, 0), IM_COL32(72, 156, 255, 0),
                                     IM_COL32(255, 224, 64, 0), IM_COL32(170, 236, 255, 0), IM_COL32(196, 104, 240, 0)};
constexpr ImU32 kSpark = IM_COL32(236, 40, 36, 0), kFeeble = IM_COL32(72, 128, 255, 0);
constexpr double COMPACT_NS = .7e9;  // (Compact: its numbers last this long, and rise half as far)

// The styles (1.0.2, [Hunting Aids] DamageNumberStyle, the port's own looks): Classic as they came; Outlined, a thick
// outline and a jump in (larger for its first moment); Compact, smaller and shorter, the element's part beside it;
// Burst, a run of hits on one monster added up in one number (runtime/hle/huntaids.c) that jumps at each hit, stays
// put and shows how many hits it holds. Sizes are a handful of fixed steps of the player's size, never a size that
// changes every frame (each one is baked once).
void numbers(ImDrawList *dl, const HuntView &v, const Pic &p, float h, uint64_t now) {
    ImFont *font = ImGui::GetFont();
    const int style = std::clamp(v.number_style, 0, 3);
    const bool by_hit = v.number_colour == HUNT_COLOUR_BY_HIT;
    const float base = std::max(8.0f, std::floor(SIZES[std::clamp(v.number_size, 0, 2)] * h + .5f));
    auto step = [&](float k) { return std::max(8.0f, std::floor(base * k + .5f)); };
    const float main_size = style == HUNT_STYLE_COMPACT ? step(.72f) : style == HUNT_STYLE_BURST ? step(1.1f) : base;
    const float big = step(style == HUNT_STYLE_BURST ? 1.3f : 1.25f), second = step(style == HUNT_STYLE_COMPACT ? .5f : .6f),
                count = step(.45f);
    const ImU32 colour = COLOURS[std::clamp(v.number_colour, 0, 4)];
    const double life = style == HUNT_STYLE_COMPACT ? COMPACT_NS : (double)HUNT_NUMBER_NS;
    const float rise_k = style == HUNT_STYLE_COMPACT  ? .5f
                         : style == HUNT_STYLE_OUTLINED ? .8f
                         : style == HUNT_STYLE_BURST    ? .15f
                                                        : 1.f;
    dl->PushClipRect(ImVec2(p.x0, p.y0), ImVec2(p.x0 + p.w, p.y0 + p.h), true);
    for (int i = 0; i < v.nnum; i++) {
        const HuntNumber &n = v.num[i];
        const uint64_t since = style == HUNT_STYLE_BURST ? n.last_ns : n.born_ns;  // (a run lasts from its last hit)
        float t = (float)((double)(now - since) / life), age = (float)((double)(now - n.born_ns) / life);
        if (!n.shown || t > 1 || now < n.born_ns) continue;
        age = std::min(age, 1.f);
        const float rise = RISE * rise_k * h * (1 - (1 - age) * (1 - age));
        const float jitter = style == HUNT_STYLE_BURST ? 0.f : (float)((int)(n.id * 37u % 9u) - 4) * JITTER * p.w;
        // (Outlined jumps in, Burst at each hit: larger for its first 0.12 s)
        const bool jump = (style == HUNT_STYLE_OUTLINED || style == HUNT_STYLE_BURST) && now - since < 120000000ull;
        const float size = jump ? big : main_size;
        const int value = by_hit ? n.value : n.value + n.elem;  // (one colour: the whole damage, as it came)
        char text[16];
        std::snprintf(text, sizeof text, "%d", value);
        const ImVec2 sz = font->CalcTextSizeA(size, 1e9f, 0, text);
        const ImVec2 at(p.x0 + n.x * p.w + jitter - sz.x * .5f, p.y0 + n.y * p.h - rise - sz.y * .5f);
        const int a = (int)(255 * std::clamp(t < FADE ? 1 : 1 - (t - FADE) / (1 - FADE), 0.f, 1.f));
        const ImU32 ink = by_hit ? KIND_COLOURS[std::min<int>(n.kind, 4)] : colour;
        const float o =
            std::max(1.f, size * (style == HUNT_STYLE_OUTLINED ? .13f : style == HUNT_STYLE_COMPACT ? .06f : .07f));
        if (style == HUNT_STYLE_OUTLINED)  // (the thick outline: a second ring of shadow around the first)
            outlined(dl, font, size, ImVec2(at.x + o * .5f, at.y + o * .5f), IM_COL32(0, 0, 0, 0), a / 2, text, o);
        outlined(dl, font, size, at, ink, a, text, o);
        float right = at.x + sz.x;
        if (by_hit && n.elem > 0) {  // the element's part: smaller, in its colour (Compact: beside it)
            char e[16];
            std::snprintf(e, sizeof e, style == HUNT_STYLE_COMPACT ? "+%d" : "%d", n.elem);
            const ImVec2 es = font->CalcTextSizeA(second, 1e9f, 0, e);
            const ImVec2 ea = style == HUNT_STYLE_COMPACT ? ImVec2(right + second * .15f, at.y + sz.y - es.y)
                                                          : ImVec2(right - es.x * .3f, at.y + sz.y * .78f);
            outlined(dl, font, second, ea, ELEMENT_COLOURS[std::min<int>(n.element, 5)], a, e, std::max(1.f, second * .08f));
            if (style == HUNT_STYLE_COMPACT) right = ea.x + es.x;
        }
        if (by_hit && n.crit > 0) {  // a critical hit: a red spark at the top right
            const float r = size * .2f, cx = at.x + sz.x + r * .6f, cy = at.y + r * .9f, w = std::max(1.5f, size * .07f);
            const ImU32 c = (kSpark & 0x00ffffffu) | IM_COL32(0, 0, 0, a), s = IM_COL32(0, 0, 0, a * 3 / 4);
            for (int pass = 0; pass < 2; pass++) {  // (its dark edge first, then the spark)
                const float k = pass ? 1.f : 1.3f, ww = pass ? w : w + 2;
                const ImU32 cc = pass ? c : s;
                dl->AddLine(ImVec2(cx - r * k, cy), ImVec2(cx + r * k, cy), cc, ww);
                dl->AddLine(ImVec2(cx, cy - r * k), ImVec2(cx, cy + r * k), cc, ww);
                dl->AddLine(ImVec2(cx - r * .6f * k, cy - r * .6f * k), ImVec2(cx + r * .6f * k, cy + r * .6f * k), cc, ww * .7f);
                dl->AddLine(ImVec2(cx - r * .6f * k, cy + r * .6f * k), ImVec2(cx + r * .6f * k, cy - r * .6f * k), cc, ww * .7f);
            }
        } else if (by_hit && n.crit < 0) {  // a negative one: a blue line under it
            const float y = at.y + sz.y * .98f, w = std::max(2.f, size * .08f);
            dl->AddLine(ImVec2(at.x - 1, y + 1), ImVec2(at.x + sz.x + 1, y + 1), IM_COL32(0, 0, 0, a * 3 / 4), w + 2);
            dl->AddLine(ImVec2(at.x, y), ImVec2(at.x + sz.x, y), (kFeeble & 0x00ffffffu) | IM_COL32(0, 0, 0, a), w);
        }
        if (style == HUNT_STYLE_BURST && n.hits > 1) {  // how many hits the run holds
            char k[16];
            std::snprintf(k, sizeof k, "x%d", n.hits);
            outlined(dl, font, count, ImVec2(right + count * .25f, at.y + sz.y - count * 1.05f), IM_COL32(244, 232, 200, 0),
                     a, k, std::max(1.f, count * .08f));
        }
    }
    dl->PopClipRect();
}

// Where the bars were drawn last (display pixels: their plates' block) and when: the editors' frame for them
// (hunt_bars_rect)
std::atomic<float> g_rect[4];
std::atomic<uint64_t> g_rect_ns{0};

// A part of the game's HUD where it was drawn last (display pixels: touch_hud_zone, its art and a margin), else where the
// HUD puts it (hud_parts.h hud_box, with the player's moves); false while it is hidden
bool part_box(int part, const Pic &p, float w, float h, float r[4]) {
    if (touch_hud_zone(part, w, h, r)) return true;
    int prof = g_hud_prof_now;
    if (prof < 0 || prof >= HUD_PROFILES) prof = g_touch_hud ? HUD_TOUCH : HUD_PLAIN;
    if (g_hud_hidden[prof][part]) return false;
    float b[4];
    hud_box(part, prof, g_hud_arts_top, b);
    r[0] = p.x0 + b[0] * p.w / 1280.0f, r[1] = p.y0 + b[1] * p.h / 720.0f;
    r[2] = p.x0 + b[2] * p.w / 1280.0f, r[3] = p.y0 + b[3] * p.h / 720.0f;
    return true;
}

// Where the HUD leaves room for the bars at their own place (MH4U's hunt_bars_room, for MHGU's HUD): they end left of the
// target camera, in the top right corner (MH4U's: under the parts in the corner; here the minimap sits under the camera),
// and start after the hunter's health and stamina, or go under them
struct Room {
    float right, left, under;
};
Room room_of(const Pic &p, float w, float h) {
    const float m = kMargin * p.h;
    Room r{p.x0 + p.w - m, p.x0, p.y0};
    float z[4];
    if (part_box(HP_TARGET, p, w, h, z) && z[1] < p.y0 + .34f * p.h && z[0] > p.x0 + .5f * p.w)
        r.right = std::min(r.right, z[0] - m);
    if (part_box(HP_BARS, p, w, h, z) && z[1] < p.y0 + .34f * p.h) r.left = z[2], r.under = z[3];
    return r;
}

// The bars (MH4U's hunt_bars_layout and hunt_bars_draw): at their own place, from the top, the right end left of the
// target camera, as long as they fit after the hunter's bars (under them at full length when that leaves less than
// kShortest), under the FPS plate if it is over them; placed in an editor (touch_bars_place: their top middle, the
// screen's fractions), there at full length, kept on the picture; times the editors' size
void bars(ImDrawList *dl, const HuntView &v, const Pic &p, float w, float h, uint64_t now) {
    float px, py, k;
    touch_bars_place(&px, &py, &k);
    k = std::clamp(k, .5f, 2.f);
    const float bh = std::max(5.f, std::floor(kHeight * k * p.h + .5f)), m = kMargin * p.h;
    const float gap = std::max(2.f, std::floor(kGap * bh + .5f)), length = kLength * k * p.h;
    const float n = (float)v.nbar, block = n * bh + std::max(0.f, n - 1) * gap;
    float x, y, bw;
    if (px < 0 || py < 0) {
        const Room r = room_of(p, w, h);
        bw = std::min(length, r.right - std::max(r.left + m, p.x0 + m));
        y = p.y0 + m;
        if (bw < kShortest * k * p.h) {  // (no room beside the hunter's bars: under them)
            bw = std::min(length, r.right - (p.x0 + m));
            y = std::max(y, r.under + m);
        }
        x = r.right - bw;
        float f[4];  // (the FPS plate at the top over them: under it)
        if (perfstats_rect(f) && f[0] < x + bw && f[2] > x && f[1] < p.y0 + .2f * p.h && f[3] > y) y = f[3] + m * .5f;
    } else {
        bw = std::min(length, p.w);
        x = std::clamp(px * w - bw * .5f, p.x0, std::max(p.x0, p.x0 + p.w - bw));
        y = std::clamp(py * h, p.y0, std::max(p.y0, p.y0 + p.h - block));
    }
    y = std::floor(y);
    g_rect[0] = x, g_rect[1] = y, g_rect[2] = x + bw, g_rect[3] = y + block, g_rect_ns = now;
    const float l = std::max(1.f, std::floor(bh * .09f + .5f)), pad = std::max(2.f, std::floor(bh * .25f));
    const float text = std::max(6.f, std::floor(kText * bh)), groove = std::max(2.f, std::floor(bh * .36f));
    const float numbers = std::min(kNumbers * bh, bw * .55f);
    ImFont *font = ImGui::GetFont();
    dl->PushClipRect(ImVec2(p.x0, p.y0), ImVec2(p.x0 + p.w, p.y0 + p.h), true);
    for (int i = 0; i < v.nbar; i++, y += bh + gap) {
        const HuntBar &b = v.bar[i];
        const float a = b.here ? 1.f : kAway, fill = b.max > 0 ? std::clamp((float)b.hp / (float)b.max, 0.f, 1.f) : 0.f;
        const ImU32 colour = fill > .5f ? kGood : fill > .25f ? kGold : kBad;
        const ImVec2 p0(x, y), p1(x + bw, y + bh);
        // the plate (the FPS counter's, perfstats.cpp: wine, a black edge, red and moss rules at the top and bottom)
        dl->AddRectFilled(p0, p1, alpha_of(kWine, a * .9f));
        dl->AddRect(p0, p1, alpha_of(IM_COL32(0, 0, 0, 255), a), 0.f, 0, l);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + l), alpha_of(kRule, a));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + l), ImVec2(p1.x, p0.y + l * 1.7f), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l * 1.7f), ImVec2(p1.x, p1.y - l), alpha_of(kMoss, a));
        dl->AddRectFilled(ImVec2(p0.x, p1.y - l), p1, alpha_of(kRule, a));
        // the name at the left (shortened to its room), then the groove and the health in it
        const float room = std::max(4.f, bw - numbers - 2 * pad);
        char name[64];
        const float nw = fit_name(font, text, b.name, kName * room, name, sizeof name);
        const float tw = std::max(4.f, room - (nw > 0 ? nw + pad : 0.f));
        const ImVec2 t0(x + pad + (nw > 0 ? nw + pad : 0.f), y + std::floor((bh - groove) * .5f)), t1(t0.x + tw, t0.y + groove);
        if (nw > 0) {
            const ImVec2 ns = font->CalcTextSizeA(text, 1e9f, 0, name);
            const ImVec2 at(x + pad, y + bh * .5f - ns.y * .5f);
            dl->AddText(font, text, ImVec2(at.x + text * .05f, at.y + text * .06f), alpha_of(IM_COL32(0, 0, 0, 200), a), name);
            dl->AddText(font, text, at, alpha_of(kInk, a), name);
        }
        dl->AddRectFilled(ImVec2(t0.x - l, t0.y - l), ImVec2(t1.x + l, t1.y + l), alpha_of(IM_COL32(0, 0, 0, 255), a * .8f));
        dl->AddRectFilled(t0, t1, alpha_of(kDeep, a));
        if (fill > 0) {
            const float fx = t0.x + tw * fill;
            dl->AddRectFilled(t0, ImVec2(fx, t1.y), alpha_of(colour, a));
            dl->AddRectFilled(t0, ImVec2(fx, t0.y + std::max(1.f, groove * .34f)), alpha_of(IM_COL32(255, 255, 255, 70), a));
        }
        // the numbers: ink on the plate, a shadow as the port's other texts
        char t[32];
        std::snprintf(t, sizeof t, "%d / %d", b.hp, b.max);
        const ImVec2 size = font->CalcTextSizeA(text, 1e9f, 0, t);
        const ImVec2 at(x + bw - pad - size.x, y + bh * .5f - size.y * .5f);
        dl->AddText(font, text, ImVec2(at.x + text * .05f, at.y + text * .06f), alpha_of(IM_COL32(0, 0, 0, 200), a), t);
        dl->AddText(font, text, at, alpha_of(kInk, a), t);
    }
    dl->PopClipRect();
}

// What there is to draw now: the game side's view, and in an editor (the touch layout editor, the HUD editor) with the
// bars on, a stand-in bar while no monster has one (MH4U's: 1900 / 2546), so they can be placed outside a quest
bool what(HuntView *v, bool *editing) {
    bool any = huntaids_view(v);
    *editing = config_live_monster_bars() && touch_editor_open();
    if (!*editing) return any;
    if (!any) v->nnum = 0, v->nbar = 0;
    if (!v->nbar) {
        v->bar[0].hp = 1900, v->bar[0].max = 2546, v->bar[0].here = true, v->nbar = 1;
        std::snprintf(v->bar[0].name, sizeof v->bar[0].name, "%s", config_language() == 1 ? "Monstruo" : "Monster");
    }
    return true;
}

// What a frame draws, for MHGU_PERF_OVERLAY_REUSE: 0 nothing, ~0 numbers (they move every frame) or an editor, else a
// hash of the bars and their place
uint64_t g_drawn;
uint64_t key_of(bool any, bool editing, const HuntView &v, float w, float h) {
    if (!any || w <= 0 || h <= 0 || (!editing && touch_game_menu())) return 0;
    if (v.nnum || editing) return ~0ull;
    uint64_t k = 1469598103934665603ull;  // (FNV-1a)
    auto mix = [&k](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) k = (k ^ ((const unsigned char *)p)[i]) * 1099511628211ull;
    };
    float place[3];
    touch_bars_place(&place[0], &place[1], &place[2]);
    mix(&w, sizeof w), mix(&h, sizeof h), mix(place, sizeof place), mix(&v.nbar, sizeof v.nbar);
    for (int i = 0; i < v.nbar; i++)
        mix(&v.bar[i].hp, sizeof(int)), mix(&v.bar[i].max, sizeof(int)), mix(&v.bar[i].here, 1),
            mix(v.bar[i].name, std::strlen(v.bar[i].name));
    return k == 0 || k == ~0ull ? 1 : k;
}

}  // namespace

extern "C" bool hunt_overlay_unchanged(float w, float h) {
    HuntView v;
    bool editing, any = what(&v, &editing);
    uint64_t k = key_of(any, editing, v, w, h);
    return k != ~0ull && k == g_drawn;
}

extern "C" void hunt_overlay_draw(float w, float h) {
    HuntView v;
    bool editing, any = what(&v, &editing);
    g_drawn = key_of(any, editing, v, w, h);
    if (!g_drawn) return;
    const Pic p = picture(w, h);
    const uint64_t now = plat_now_ns();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();  // (under the touch controls and the menus, as the FPS plate)
    if (v.nbar) bars(dl, v, p, w, h, now);
    if (v.nnum) numbers(dl, v, p, h, now);
    static int cost = -1;  // ((debug) MHGU_HUNT_COST=1: this drawing's time too, every 10 s, as huntaids.c's step)
    if (cost < 0) cost = getenv("MHGU_HUNT_COST") && atoi(getenv("MHGU_HUNT_COST")) > 0;
    if (cost) {
        static uint64_t sum, most, since, frames;
        uint64_t t = plat_now_ns() - now;
        sum += t, frames++, most = std::max(most, t);
        if (!since) since = now;
        if (now - since >= 10000000000ull) {
            rt_logf("[hunt] drawing: %.2f us a frame drawn on average, %.1f at most (%llu frames, %d numbers, %d bars now)\n",
                    sum / 1e3 / (double)frames, most / 1e3, (unsigned long long)frames, v.nnum, v.nbar);
            sum = most = frames = 0, since = now;
        }
    }
}

extern "C" bool hunt_bars_rect(float r[4]) {
    uint64_t at = g_rect_ns.load();
    if (!at || plat_now_ns() - at > 400000000ull) return false;
    for (int i = 0; i < 4; i++) r[i] = g_rect[i].load();
    return true;
}
