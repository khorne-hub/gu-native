// The weapon tree's panel (weapon_tree_panel.h). The same file in both ports.
#include "weapon_tree_panel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace weapon_tree_panel {

namespace {

// the apps' colours (MH4U's town.cpp and touch.cpp, MHGU's touch.cpp): wine-dark plates, red and moss rules, bronze, gold
const ImU32 INK = IM_COL32(244, 232, 200, 255), GOLD = IM_COL32(222, 186, 96, 255), BRONZE = IM_COL32(112, 76, 34, 255),
            GLOW = IM_COL32(255, 214, 92, 255), RED = IM_COL32(176, 34, 30, 255), MOSS = IM_COL32(80, 142, 54, 255),
            DIM = IM_COL32(204, 186, 160, 255), FAINT = IM_COL32(150, 132, 108, 255), BRIGHT = IM_COL32(255, 248, 225, 255);

ImU32 Alpha(ImU32 c, float a) {
    const float al = ((c >> IM_COL32_A_SHIFT) & 0xff) * a;
    return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp(al, 0.f, 255.f) << IM_COL32_A_SHIFT);
}

// the rarity's colour, as the games tint a weapon's icon by its rarity (1 white, 2 violet, 3 yellow, 4 pink, 5 green,
// 6 blue, 7 red, 8 sky blue, 9 orange, 10 lavender, 11 and on gold)
ImU32 RarityColour(int r) {
    static const ImU32 k[] = {IM_COL32(236, 236, 236, 255), IM_COL32(178, 128, 236, 255), IM_COL32(236, 214, 84, 255),
                              IM_COL32(244, 150, 196, 255), IM_COL32(110, 204, 92, 255), IM_COL32(80, 128, 236, 255),
                              IM_COL32(232, 70, 60, 255),   IM_COL32(110, 214, 236, 255), IM_COL32(244, 150, 52, 255),
                              IM_COL32(200, 160, 244, 255)};
    if (r >= 1 && r <= 10) return k[r - 1];
    return GLOW;
}

ImU32 ElementColour(int e) {
    switch (e) {
    case weapon_tree::kFire: return IM_COL32(236, 92, 44, 255);
    case weapon_tree::kWater: return IM_COL32(64, 140, 236, 255);
    case weapon_tree::kThunder: return IM_COL32(240, 212, 60, 255);
    case weapon_tree::kIce: return IM_COL32(160, 224, 244, 255);
    case weapon_tree::kDragon: return IM_COL32(150, 40, 70, 255);
    case weapon_tree::kPoison: return IM_COL32(156, 76, 196, 255);
    case weapon_tree::kParalysis: return IM_COL32(214, 196, 50, 255);
    case weapon_tree::kSleep: return IM_COL32(150, 170, 214, 255);
    case weapon_tree::kBlast: return IM_COL32(244, 140, 40, 255);
    default: return DIM;
    }
}

const char* ElementWord(int e) {
    switch (e) {
    case weapon_tree::kFire: return "Fire";
    case weapon_tree::kWater: return "Water";
    case weapon_tree::kThunder: return "Thunder";
    case weapon_tree::kIce: return "Ice";
    case weapon_tree::kDragon: return "Dragon";
    case weapon_tree::kPoison: return "Poison";
    case weapon_tree::kParalysis: return "Paralysis";
    case weapon_tree::kSleep: return "Sleep";
    case weapon_tree::kBlast: return "Blast";
    default: return "";
    }
}

void Diamond(ImDrawList* dl, ImVec2 c, float d, ImU32 col) {
    ImVec2 q[4] = {ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y)};
    dl->AddConvexPolyFilled(q, 4, col);
}

void Text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, const char* end = nullptr) {
    const float o = std::max(1.f, size * .06f);
    dl->AddText(f, size, ImVec2(p.x + o, p.y + o), IM_COL32(0, 0, 0, (int)(190 * ((col >> 24) & 0xff) / 255)), t, end);
    dl->AddText(f, size, p, col, t, end);
}

// the game's window: wine-dark, a black edge, a red rule with a green one inside it top and bottom, gold studs
void WindowPlate(ImDrawList* dl, const ImRect& r, float line) {
    dl->AddRectFilled(ImVec2(r.Min.x + line, r.Min.y + line * 1.5f), ImVec2(r.Max.x + line, r.Max.y + line * 1.5f),
                      IM_COL32(0, 0, 0, 120));
    dl->AddRectFilled(r.Min, r.Max, IM_COL32(50, 25, 24, 245));
    dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 255), 0, line);
    dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + line), RED);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Min.y + line), ImVec2(r.Max.x, r.Min.y + line * 1.7f), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line * 1.7f), ImVec2(r.Max.x, r.Max.y - line), MOSS);
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - line), r.Max, RED);
    for (ImVec2 c : {r.Min, ImVec2(r.Max.x, r.Min.y), ImVec2(r.Min.x, r.Max.y), r.Max}) Diamond(dl, c, line * 1.9f, GOLD);
}

// a row or a button: a dark plate, a bronze edge and a gold rule inside; lit ochre with a glowing edge
void Plate(ImDrawList* dl, const ImRect& r, float line, bool lit, bool pressed = false, float fade = 1) {
    const float in = line * 1.6f;
    if (lit || pressed) {
        dl->AddRectFilledMultiColor(r.Min, r.Max, Alpha(IM_COL32(150, 104, 30, 250), fade), Alpha(IM_COL32(206, 158, 42, 250), fade),
                                    Alpha(IM_COL32(122, 82, 22, 250), fade), Alpha(IM_COL32(92, 62, 22, 250), fade));
        if (pressed) dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 236, 170, 46));
    } else {
        dl->AddRectFilled(r.Min, r.Max, Alpha(IM_COL32(28, 20, 15, 238), fade));
    }
    dl->AddRect(r.Min, r.Max, Alpha(lit || pressed ? GLOW : BRONZE, fade), 0, line);
    dl->AddRect(ImVec2(r.Min.x + in, r.Min.y + in), ImVec2(r.Max.x - in, r.Max.y - in),
                Alpha(lit || pressed ? IM_COL32(255, 236, 170, 200) : IM_COL32(222, 186, 96, 150), fade), 0,
                std::max(1.f, line * .45f));
}

void Cross(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float t) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, t);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, t);
}

// ---- our marks (drawn: no art of the games')
// the tree's own sign (its button and its title): a trunk with three branches, a node at each end
void TreeSign(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.5f, r * .16f);
    const ImVec2 a(c.x - r * .75f, c.y), m(c.x - r * .1f, c.y);
    dl->AddLine(a, m, col, t);
    for (float dy : {-.62f, 0.f, .62f}) {
        const ImVec2 e(c.x + r * .62f, c.y + dy * r);
        dl->AddLine(m, ImVec2(m.x, e.y), col, t);
        dl->AddLine(ImVec2(m.x, e.y), e, col, t);
        dl->AddCircleFilled(e, r * .2f, col);
    }
    dl->AddCircleFilled(a, r * .24f, col);
}
// the box: a chest with a lid line
void BoxMark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.f, r * .2f);
    dl->AddRectFilled(ImVec2(c.x - r, c.y - r * .55f), ImVec2(c.x + r, c.y + r * .75f), IM_COL32(90, 58, 26, 255), r * .15f);
    dl->AddRect(ImVec2(c.x - r, c.y - r * .55f), ImVec2(c.x + r, c.y + r * .75f), col, r * .15f, t);
    dl->AddLine(ImVec2(c.x - r, c.y - r * .05f), ImVec2(c.x + r, c.y - r * .05f), col, t);
    dl->AddRectFilled(ImVec2(c.x - r * .2f, c.y - r * .2f), ImVec2(c.x + r * .2f, c.y + r * .2f), col);
}
// worn: a round gold plate with an E
void WornMark(ImDrawList* dl, ImFont* f, ImVec2 c, float r, ImU32 col) {
    dl->AddCircleFilled(c, r, IM_COL32(28, 20, 15, 255));
    dl->AddCircle(c, r, col, 0, std::max(1.f, r * .16f));
    const float fs = r * 1.45f;
    const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, "E");
    dl->AddText(f, fs, ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), col, "E");
}
// forged from scratch: a small hammer
void HammerMark(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.f, r * .22f);
    dl->AddLine(ImVec2(c.x - r * .7f, c.y + r * .8f), ImVec2(c.x + r * .25f, c.y - r * .15f), col, t);
    ImVec2 q[4] = {ImVec2(c.x - r * .15f, c.y - r * .95f), ImVec2(c.x + r * .9f, c.y + r * .1f),
                   ImVec2(c.x + r * .55f, c.y + r * .45f), ImVec2(c.x - r * .5f, c.y - r * .6f)};
    dl->AddConvexPolyFilled(q, 4, col);
}
// an element's or a status's mark: a disc of its colour with a sign of its own
void ElementMark(ImDrawList* dl, ImVec2 c, float r, int e, bool hidden) {
    const ImU32 col = ElementColour(e), ink = IM_COL32(255, 250, 235, 255);
    dl->AddCircleFilled(c, r, hidden ? IM_COL32(70, 62, 54, 255) : col);
    dl->AddCircle(c, r, IM_COL32(0, 0, 0, 200), 0, std::max(1.f, r * .14f));
    const float t = std::max(1.f, r * .18f), s = r * .58f;
    const ImU32 k = hidden ? col : ink;
    switch (e) {
    case weapon_tree::kFire: {
        ImVec2 q[3] = {ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x + s * .7f, c.y + s * .7f), ImVec2(c.x - s * .7f, c.y + s * .7f)};
        dl->AddConvexPolyFilled(q, 3, k);
        break;
    }
    case weapon_tree::kWater:
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * .25f), s * .55f, k);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - s * .95f), ImVec2(c.x + s * .52f, c.y + s * .1f), ImVec2(c.x - s * .52f, c.y + s * .1f), k);
        break;
    case weapon_tree::kThunder:
    case weapon_tree::kParalysis:
        dl->PathLineTo(ImVec2(c.x + s * .35f, c.y - s));
        dl->PathLineTo(ImVec2(c.x - s * .35f, c.y));
        dl->PathLineTo(ImVec2(c.x + s * .35f, c.y));
        dl->PathLineTo(ImVec2(c.x - s * .35f, c.y + s));
        dl->PathStroke(k, t);
        if (e == weapon_tree::kParalysis) dl->AddCircle(c, s * 1.05f, k, 0, t * .6f);
        break;
    case weapon_tree::kIce:
        for (int i = 0; i < 3; ++i) {
            const float an = 3.14159f * i / 3;
            dl->AddLine(ImVec2(c.x - std::cos(an) * s, c.y - std::sin(an) * s), ImVec2(c.x + std::cos(an) * s, c.y + std::sin(an) * s), k, t);
        }
        break;
    case weapon_tree::kDragon:
        dl->AddQuad(ImVec2(c.x, c.y - s), ImVec2(c.x + s * .7f, c.y), ImVec2(c.x, c.y + s), ImVec2(c.x - s * .7f, c.y), k, t);
        break;
    case weapon_tree::kPoison:
        dl->AddCircleFilled(ImVec2(c.x - s * .3f, c.y + s * .25f), s * .38f, k);
        dl->AddCircleFilled(ImVec2(c.x + s * .35f, c.y - s * .3f), s * .28f, k);
        break;
    case weapon_tree::kSleep:
        dl->PathLineTo(ImVec2(c.x - s * .55f, c.y - s * .6f));
        dl->PathLineTo(ImVec2(c.x + s * .55f, c.y - s * .6f));
        dl->PathLineTo(ImVec2(c.x - s * .55f, c.y + s * .6f));
        dl->PathLineTo(ImVec2(c.x + s * .55f, c.y + s * .6f));
        dl->PathStroke(k, t);
        break;
    case weapon_tree::kBlast:
        for (int i = 0; i < 4; ++i) {
            const float an = 3.14159f * i / 4;
            dl->AddLine(ImVec2(c.x - std::cos(an) * s, c.y - std::sin(an) * s), ImVec2(c.x + std::cos(an) * s, c.y + std::sin(an) * s), k, t);
        }
        break;
    default: break;
    }
}

ImFont* TitleFont() {
    ImGuiIO& io = ImGui::GetIO();
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
}

// `t` cut with "..." to fit `most` pixels
std::string Fit(ImFont* f, float size, const std::string& t, float most) {
    if (f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x <= most) return t;
    std::string s = t;
    while (!s.empty()) {
        do s.pop_back(); while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xc0) == 0x80);  // (UTF-8)
        if (!s.empty()) s.pop_back();
        const std::string cut = s + "...";
        if (f->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x <= most) return cut;
    }
    return "";
}

// A weapon's name as a node shows it: with its level where one is known (MHGU: "Buster Sword Lv3")
std::string Named(const weapon_tree::Weapon& w, int id, bool es) {
    std::string n = w.name.empty() ? "#" + std::to_string(id) : w.name;
    if (w.level > 0) n += std::string(" ") + Words("Lv", es) + std::to_string(w.level);
    return n;
}

// "Upgraded from <the one before>" (MHGU: "... Lv3", the level of it that upgrades)
std::string From(const weapon_tree::Weapon& p, const weapon_tree::Weapon& w, bool es) {
    std::string t = std::string(Words("Upgraded from", es)) + " " + p.name;
    if (w.from_level > 0) t += std::string(" ") + Words("Lv", es) + std::to_string(w.from_level);
    return t;
}

struct Metrics {
    float line, nw, nh, px, py, pad;
};
Metrics Measure(float h) {
    Metrics m;
    m.line = std::max(2.f, .0042f * h);
    m.nw = .34f * h, m.nh = .082f * h;   // a node
    m.px = m.nw + .07f * h;              // from a column to the next
    m.py = m.nh + .03f * h;              // from a row to the next
    m.pad = .03f * h;                    // round the tree in its area
    return m;
}

ImVec2 NodeAt(const Metrics& m, const ImRect& area, const State& s, const weapon_tree::Node& n) {
    return ImVec2(area.Min.x + m.pad + n.column * m.px - s.scroll_x, area.Min.y + m.pad + n.row * m.py - s.scroll_y);
}

void ClampScroll(const Metrics& m, const ImRect& area, const Content& c, State& s) {
    const float cw = 2 * m.pad + std::max(0, c.view.columns - 1) * m.px + m.nw;
    const float ch = 2 * m.pad + std::max(0, c.view.rows - 1) * m.py + m.nh;
    s.scroll_x = std::clamp(s.scroll_x, 0.f, std::max(0.f, cw - area.GetWidth()));
    s.scroll_y = std::clamp(s.scroll_y, 0.f, std::max(0.f, ch - area.GetHeight()));
}

// the focus kept in sight
void Reveal(const Metrics& m, const ImRect& area, const Content& c, State& s) {
    if (s.focus < 0 || s.focus >= static_cast<int>(c.view.nodes.size())) return;
    const weapon_tree::Node& n = c.view.nodes[s.focus];
    const float x = m.pad + n.column * m.px, y = m.pad + n.row * m.py;
    if (x - s.scroll_x < m.pad) s.scroll_x = x - m.pad;
    if (x + m.nw - s.scroll_x > area.GetWidth() - m.pad) s.scroll_x = x + m.nw - area.GetWidth() + m.pad;
    if (y - s.scroll_y < m.pad) s.scroll_y = y - m.pad;
    if (y + m.nh - s.scroll_y > area.GetHeight() - m.pad) s.scroll_y = y + m.nh - area.GetHeight() + m.pad;
    ClampScroll(m, area, c, s);
}

}  // namespace

const char* Words(const char* english, bool spanish) {
    if (!spanish) return english;
    static const struct {
        const char *en, *es;
    } k[] = {
        {"Weapon tree", "Árbol de armas"},
        {"In the box", "En la caja"},
        {"Equipped", "Equipada"},
        {"Next: not known yet", "Siguiente: todavía no la conocés"},
        {"Forged from scratch", "Se forja desde cero"},
        {"Upgraded from", "Se mejora desde"},
        {"Lv", "Nv"},
        {"Level", "Nivel"},
        {"Rarity", "Rareza"},
        {"Attack", "Ataque"},
        {"Affinity", "Afinidad"},
        {"Slots", "Ranuras"},
        {"needs Awaken", "necesita Despertar"},
        {"Fire", "Fuego"},
        {"Water", "Agua"},
        {"Thunder", "Trueno"},
        {"Ice", "Hielo"},
        {"Dragon", "Dragón"},
        {"Poison", "Veneno"},
        {"Paralysis", "Parálisis"},
        {"Sleep", "Sueño"},
        {"Blast", "Explosión"},
        {"Close", "Cerrar"},
        {"Move", "Mover"},
        {"The Smithy's weapon", "El arma de la herrería"},
        {"Read only: forge and upgrade in the Smithy.", "Solo consulta: se forja y se mejora en la herrería."},
        {"Great Sword", "Gran espada"},
        {"Sword and Shield", "Espada y escudo"},
        {"Hammer", "Martillo"},
        {"Lance", "Lanza"},
        {"Light Bowgun", "Ballesta ligera"},
        {"Heavy Bowgun", "Ballesta pesada"},
        {"Long Sword", "Espada larga"},
        {"Switch Axe", "Hacha espada"},
        {"Gunlance", "Lanza pistola"},
        {"Bow", "Arco"},
        {"Dual Blades", "Espadas dobles"},
        {"Hunting Horn", "Cornamusa"},
        {"Insect Glaive", "Glaive insecto"},
        {"Charge Blade", "Hacha cargada"},
    };
    for (const auto& m : k)
        if (std::strcmp(m.en, english) == 0) return m.es;
    return english;
}

void Draw(ImDrawList* dl, float W, float H, float a, bool pad, const Content& c, State& s) {
    const bool es = c.spanish;
    ImFont* font = ImGui::GetFont();
    ImFont* title = TitleFont();
    const Metrics m = Measure(H);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(120 * a)));
    const int v0 = dl->VtxBuffer.Size;
    // the window: as wide as fits (16:9 and wider: up to 1.7 heights), the title, the tree, the details
    const float pw = std::min(W * .95f, H * 1.7f), ph = H * .9f;
    const ImVec2 p0(std::floor((W - pw) / 2), std::floor((H - ph) / 2));
    s.panel = ImRect(p0, ImVec2(p0.x + pw, p0.y + ph));
    WindowPlate(dl, s.panel, m.line);
    const float th = .09f * H, ih = .225f * H;
    {  // the title: the tree's sign, "Weapon tree · <class>"
        TreeSign(dl, ImVec2(p0.x + .045f * H, p0.y + th * .55f), .022f * H, GLOW);
        std::string t = std::string(Words("Weapon tree", es)) + "  -  " + c.class_name;
        Text(dl, title, .05f * H, ImVec2(p0.x + .08f * H, p0.y + th * .55f - .03f * H), GLOW, t.c_str());
        const float xs = .066f * H, xr = p0.x + pw - .03f * H;
        s.close = ImRect(ImVec2(xr - xs, p0.y + th * .55f - xs / 2), ImVec2(xr, p0.y + th * .55f + xs / 2));
        Plate(dl, s.close, m.line, false, s.close_held);
        Cross(dl, s.close.GetCenter(), xs * .2f, s.close_held ? BRIGHT : INK, std::max(2.f, xs * .085f));
    }
    s.area = ImRect(ImVec2(p0.x + .03f * H, p0.y + th), ImVec2(p0.x + pw - .03f * H, p0.y + ph - ih - .015f * H));
    dl->AddRectFilled(s.area.Min, s.area.Max, IM_COL32(18, 12, 10, 150));
    dl->AddRect(s.area.Min, s.area.Max, Alpha(BRONZE, .8f), 0, std::max(1.f, m.line * .5f));
    if (s.focus < 0 || s.focus >= static_cast<int>(c.view.nodes.size())) s.focus = c.view.selected;
    if (!s.placed) {  // (the Smithy's weapon in the middle of the area, as far as the tree allows)
        s.placed = true;
        if (c.view.selected >= 0) {
            const weapon_tree::Node& n = c.view.nodes[c.view.selected];
            bool kids = false;
            for (const weapon_tree::Node& o : c.view.nodes) kids = kids || o.parent == c.view.selected;
            s.scroll_x = m.pad + n.column * m.px + m.nw + (kids ? m.px : 0.f) + m.pad - s.area.GetWidth();
            s.scroll_y = m.pad + n.row * m.py + m.nh / 2 - s.area.GetHeight() / 2;
        }
    }
    ClampScroll(m, s.area, c, s);
    // the tree: its links, then its weapons
    dl->PushClipRect(s.area.Min, s.area.Max, true);
    const int n = static_cast<int>(c.view.nodes.size());
    s.nodes.assign(n, ImRect());
    for (int i = 0; i < n; ++i) {
        const weapon_tree::Node& node = c.view.nodes[i];
        if (node.parent < 0) continue;
        const ImVec2 from = NodeAt(m, s.area, s, c.view.nodes[node.parent]), to = NodeAt(m, s.area, s, node);
        const ImVec2 a0(from.x + m.nw, from.y + m.nh / 2), a1(to.x, to.y + m.nh / 2);
        const float mx = a0.x + (m.px - m.nw) / 2;
        const ImU32 col = (node.flags & weapon_tree::kNext) ? Alpha(BRONZE, .9f) : GOLD;
        const float t = std::max(2.f, m.line * ((node.flags & weapon_tree::kNext) ? .7f : 1.1f));
        dl->AddLine(a0, ImVec2(mx, a0.y), col, t);
        dl->AddLine(ImVec2(mx, a0.y), ImVec2(mx, a1.y), col, t);
        dl->AddLine(ImVec2(mx, a1.y), a1, col, t);
    }
    const float fs = .036f * H;
    for (int i = 0; i < n; ++i) {
        const weapon_tree::Node& node = c.view.nodes[i];
        const ImVec2 q = NodeAt(m, s.area, s, node);
        const ImRect r(q, ImVec2(q.x + m.nw, q.y + m.nh));
        s.nodes[i] = r;
        if (!r.Overlaps(s.area)) continue;
        const bool next = node.flags & weapon_tree::kNext;
        const bool smithy = node.flags & weapon_tree::kSelected;
        const bool focus = i == s.focus;
        Plate(dl, r, m.line, smithy, false, next ? .8f : 1.f);
        if (focus && (pad || !smithy))  // (the cursor: a glowing frame round it)
            dl->AddRect(ImVec2(r.Min.x - m.line * 1.5f, r.Min.y - m.line * 1.5f), ImVec2(r.Max.x + m.line * 1.5f, r.Max.y + m.line * 1.5f),
                        GLOW, 0, m.line * 1.2f);
        if (next) {
            const float qs = .055f * H;
            const ImVec2 sz = title->CalcTextSizeA(qs, FLT_MAX, 0, "?");
            Text(dl, title, qs, ImVec2(r.GetCenter().x - sz.x / 2, r.GetCenter().y - sz.y / 2), GOLD, "?");
            continue;
        }
        const weapon_tree::Weapon& w = c.cls.weapons[node.id];
        Diamond(dl, ImVec2(r.Min.x + .028f * H, r.GetCenter().y), .013f * H, RarityColour(w.rarity));
        // the marks at the right end: worn, box, forged from scratch
        float right = r.Max.x - .016f * H;
        const float mr = .016f * H;
        if (node.flags & weapon_tree::kWorn) WornMark(dl, font, ImVec2(right - mr, r.GetCenter().y), mr * 1.1f, GLOW), right -= mr * 2.6f;
        if (node.flags & weapon_tree::kOwned) BoxMark(dl, ImVec2(right - mr, r.GetCenter().y), mr, GOLD), right -= mr * 2.6f;
        if (w.forge && !(node.flags & (weapon_tree::kWorn | weapon_tree::kOwned)))
            HammerMark(dl, ImVec2(right - mr, r.GetCenter().y), mr, DIM), right -= mr * 2.4f;
        const float x0 = r.Min.x + .05f * H;
        const std::string name = Named(w, node.id, es);
        // (a long name, or a wide font - a desktop's -: smaller, down to 3/4, before it is cut)
        const float room = right - x0 - .008f * H, wide = font->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str()).x;
        const float nfs = wide > room ? std::max(fs * .75f, fs * room / wide) : fs;
        const std::string fit = Fit(font, nfs, name, room);
        const ImU32 ink = smithy ? BRIGHT : (node.flags & weapon_tree::kPast) ? FAINT : INK;
        Text(dl, font, nfs, ImVec2(x0, r.GetCenter().y - nfs * .56f), ink, fit.c_str());
    }
    dl->PopClipRect();
    // the details of the focus, and the marks' legend
    const ImRect ir(ImVec2(p0.x + .03f * H, p0.y + ph - ih), ImVec2(p0.x + pw - .03f * H, p0.y + ph - .025f * H));
    Plate(dl, ir, m.line, false);
    if (s.focus >= 0 && s.focus < n) {
        const weapon_tree::Node& node = c.view.nodes[s.focus];
        const weapon_tree::Weapon& w = c.cls.weapons[node.id];
        const float ts = .042f * H, ls = .034f * H;
        float y = ir.Min.y + .022f * H;
        const float x = ir.Min.x + .03f * H;
        char line[256];
        if (node.flags & weapon_tree::kNext) {
            Text(dl, title, ts, ImVec2(x, y), GOLD, Words("Next: not known yet", es));
            if (node.parent >= 0) {
                const weapon_tree::Weapon& p = c.cls.weapons[c.view.nodes[node.parent].id];
                std::snprintf(line, sizeof line, "%s", From(p, w, es).c_str());
                Text(dl, font, ls, ImVec2(x, y + ts * 1.35f), DIM, line);
            }
        } else {
            Diamond(dl, ImVec2(x + .012f * H, y + ts * .55f), .013f * H, RarityColour(w.rarity));
            Text(dl, title, ts, ImVec2(x + .035f * H, y), (node.flags & weapon_tree::kSelected) ? GLOW : INK,
                 Named(w, node.id, es).c_str());
            float lx = x, ly = y + ts * 1.4f;
            const bool stats = w.attack > 0;  // (a port that reads no stats of its weapons leaves the line out)
            if (stats) {
                if (w.rarity > 0)  // (a port that reads no rarity leaves it out: MHGU)
                    std::snprintf(line, sizeof line, "%s %d   %s %d", Words("Rarity", es), w.rarity, Words("Attack", es), w.attack);
                else
                    std::snprintf(line, sizeof line, "%s %d", Words("Attack", es), w.attack);
                Text(dl, font, ls, ImVec2(lx, ly), INK, line);
                lx += font->CalcTextSizeA(ls, FLT_MAX, 0, line).x + .03f * H;
            }
            for (int k = 0; k < 2; ++k) {
                const int e = k == 0 ? w.element : w.element2, v = k == 0 ? w.element_value : w.element2_value;
                if (e == weapon_tree::kElementNone) continue;
                const bool hid = k == 0 && w.element_hidden;
                ElementMark(dl, ImVec2(lx + ls * .5f, ly + ls * .55f), ls * .5f, e, hid);
                std::snprintf(line, sizeof line, hid ? "%s %d (%s)" : "%s %d", Words(ElementWord(e), es), v, Words("needs Awaken", es));
                Text(dl, font, ls, ImVec2(lx + ls * 1.2f, ly), INK, line);
                lx += ls * 1.2f + font->CalcTextSizeA(ls, FLT_MAX, 0, line).x + .03f * H;
            }
            if (w.levels > 1 && w.level > 0) {  // (MHGU: its level of how many)
                std::snprintf(line, sizeof line, "%s %d / %d", Words("Level", es), w.level, w.levels);
                Text(dl, font, ls, ImVec2(lx, ly), INK, line);
                lx += font->CalcTextSizeA(ls, FLT_MAX, 0, line).x + .03f * H;
            }
            if (stats || lx > x) ly += ls * 1.3f, lx = x;  // (affinity and slots on the next line)
            if (w.affinity != 0) {
                std::snprintf(line, sizeof line, "%s %+d%%", Words("Affinity", es), w.affinity);
                Text(dl, font, ls, ImVec2(lx, ly), INK, line);
                lx += font->CalcTextSizeA(ls, FLT_MAX, 0, line).x + .03f * H;
            }
            if (stats) {
                std::snprintf(line, sizeof line, "%s", Words("Slots", es));
                Text(dl, font, ls, ImVec2(lx, ly), INK, line);
                lx += font->CalcTextSizeA(ls, FLT_MAX, 0, line).x + .012f * H;
                for (int k = 0; k < 3; ++k) {
                    const ImVec2 sc(lx + ls * .4f + k * ls * .95f, ly + ls * .58f);
                    if (k < w.slots) dl->AddCircleFilled(sc, ls * .32f, GOLD);
                    else dl->AddLine(ImVec2(sc.x - ls * .25f, sc.y), ImVec2(sc.x + ls * .25f, sc.y), FAINT, std::max(1.f, m.line * .7f));
                }
            }
            if (lx > x) ly += ls * 1.3f;
            lx = x;
            if (w.forge) {
                HammerMark(dl, ImVec2(lx + ls * .45f, ly + ls * .55f), ls * .45f, DIM);
                Text(dl, font, ls * .92f, ImVec2(lx + ls * 1.2f, ly), DIM, Words("Forged from scratch", es));
                lx += ls * 1.2f + font->CalcTextSizeA(ls * .92f, FLT_MAX, 0, Words("Forged from scratch", es)).x + .03f * H;
            }
            if (node.parent >= 0) {
                const weapon_tree::Weapon& p = c.cls.weapons[c.view.nodes[node.parent].id];
                std::snprintf(line, sizeof line, "%s", From(p, w, es).c_str());
                Text(dl, font, ls * .92f, ImVec2(lx, ly), DIM, line);
            }
        }
    }
    {  // the legend, at the details' right: box, worn, ? (and a controller's keys)
        const float ls = .03f * H, mr = .014f * H;
        float y = ir.Min.y + .024f * H;
        float need = 0;  // (its widest line, so that a wider font - a desktop's - stays inside)
        for (const char* t : {"In the box", "Equipped", "Next: not known yet"})
            need = std::max(need, font->CalcTextSizeA(ls, FLT_MAX, 0, Words(t, es)).x);
        const float x = std::min(ir.Max.x - .32f * H, ir.Max.x - need - mr * 2.8f - .03f * H);
        BoxMark(dl, ImVec2(x + mr, y + ls * .55f), mr, GOLD);
        Text(dl, font, ls, ImVec2(x + mr * 2.8f, y), DIM, Words("In the box", es));
        y += ls * 1.35f;
        WornMark(dl, font, ImVec2(x + mr, y + ls * .55f), mr * 1.1f, GLOW);
        Text(dl, font, ls, ImVec2(x + mr * 2.8f, y), DIM, Words("Equipped", es));
        y += ls * 1.35f;
        Text(dl, title, ls * 1.2f, ImVec2(x + mr * .45f, y - ls * .12f), GOLD, "?");
        Text(dl, font, ls, ImVec2(x + mr * 2.8f, y), DIM, Words("Next: not known yet", es));
        y += ls * 1.35f;
        const char* note = Words("Read only: forge and upgrade in the Smithy.", es);
        dl->AddText(font, ls * .82f, ImVec2(x, y), FAINT, note, nullptr, ir.Max.x - x - .02f * H);
    }
    // the open's animation: the window grows and fades in
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer.Data[i];
        const ImVec2 cc = s.panel.GetCenter();
        const float k = .94f + .06f * a;
        v.pos.x = cc.x + (v.pos.x - cc.x) * k, v.pos.y = cc.y + (v.pos.y - cc.y) * k;
        v.col = Alpha(v.col, a);
    }
}

bool Finger(int kind, float x, float y, const Content& c, State& s, bool* close) {
    const ImVec2 p(x, y);
    *close = false;
    if (kind == kDown) {
        s.finger = true, s.dragged = false, s.finger_at = s.finger_last = p;
        s.close_held = s.close.Contains(p);
        return true;
    }
    if (!s.finger) return true;
    if (kind == kMove) {
        const ImVec2 d(p.x - s.finger_last.x, p.y - s.finger_last.y);
        s.finger_last = p;
        if (std::fabs(p.x - s.finger_at.x) + std::fabs(p.y - s.finger_at.y) > .02f * ImGui::GetIO().DisplaySize.y) s.dragged = true;
        if (s.dragged && s.area.Contains(s.finger_at)) {  // (a drag in the tree moves it)
            const Metrics m = Measure(ImGui::GetIO().DisplaySize.y);
            s.scroll_x -= d.x, s.scroll_y -= d.y;
            ClampScroll(m, s.area, c, s);
        }
        s.close_held = s.close.Contains(p) && s.close.Contains(s.finger_at);
        return true;
    }
    // up: a tap
    s.finger = false;
    const bool was_held = s.close_held;
    s.close_held = false;
    if (s.dragged) return true;
    if (was_held && s.close.Contains(p)) return *close = true, true;
    if (!s.panel.Contains(p) && !s.panel.Contains(s.finger_at)) return *close = true, true;  // (outside: closed)
    if (s.area.Contains(p)) {
        for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i)
            if (s.nodes[i].Contains(p)) s.focus = i;
    }
    return true;
}

void Move(int dx, int dy, const Content& c, State& s) {
    const int n = static_cast<int>(c.view.nodes.size());
    if (n == 0) return;
    if (s.focus < 0 || s.focus >= n) s.focus = std::max(0, c.view.selected);
    const weapon_tree::Node& f = c.view.nodes[s.focus];
    int best = -1;
    if (dx < 0) {
        best = f.parent;
    } else if (dx > 0) {
        for (int i = 0; i < n && best < 0; ++i)
            if (c.view.nodes[i].parent == s.focus) best = i;
    } else if (dy != 0) {  // (the nearest weapon in the next row up or down, by column)
        float score = 1e9f;
        for (int i = 0; i < n; ++i) {
            const weapon_tree::Node& o = c.view.nodes[i];
            const int dr = o.row - f.row;
            if (dr == 0 || (dr > 0) != (dy > 0)) continue;
            const float sc = std::abs(dr) * 10.f + std::abs(o.column - f.column);
            if (sc < score) score = sc, best = i;
        }
    }
    if (best >= 0) {
        s.focus = best;
        const Metrics m = Measure(ImGui::GetIO().DisplaySize.y);
        Reveal(m, s.area, c, s);
    }
}

}  // namespace weapon_tree_panel
