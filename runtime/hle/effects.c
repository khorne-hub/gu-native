// [Performance] Effects: the game's particle effects, left out while it is Off (gu-native #121: lower particles for
// frames on weak phones; the owner's call for 1.0: an option to turn them off, its cost - attack cues the effects carry
// go too - on the players who turn it off).
//
// The game's effects are units of a few classes (DTI names in main: each MtDTI constructor call, 0x9aedcc): uEffect
// (vtable 0x198d620, 0x210 bytes; an rEffectList's effect played at a place: hits, sparks, blood, fire, breath, dust,
// glows, the Hunter Arts' and the monsters' attacks), its subclasses uProofEffect and uMHProofEffect (the same draw),
// uProjectionEffect (its own draw, which calls uEffect's) and uCameraEffect (effects that follow the camera: rain,
// snow, ash, sand), and uSimpleEffect (vtable 0x1997ecc). A cUnit's vtable +0x30 draws it into one of the scene's
// passes, as uGrass2's (hle/grass.c); each class's particles (cParticleGenerator*: billboards, polygons, trails, lens
// flares, lights) are drawn from there. Off, those draws are not run: no particle is drawn, the effects still play
// (their sounds, their timing, what they do to the game) and it applies at once. The HUD's 2D effects (uEffect2D),
// models used as effects (uProofEffectModel) and the shells (arrows, shots) stay.
//   MHGU_EFFECTS_LOG=1     (debug) each effect unit's class the first time it draws, and whether it was drawn
//   MHGU_EFFECTS_HOOKS=<n> (test) which draws Off leaves out, by bit: 1 uEffect, 2 uSimpleEffect, 4 uCameraEffect,
//                          8 uEffect2D, 16 uProofEffectModel (default 7)
//   MHGU_EFFECTS_FLIP=<s>  (test) drawn and left out in turns of that many seconds, whatever Effects says (with
//                          MHGU_EFFECTS_LOG=1, every 10 s how many times each class drew)
//   MHGU_EFFECTS_FREEZE=1  (test) the engine's frame step held near 0 while effect units draw: the scene stands still, so
//                          shots in the two turns of MHGU_EFFECTS_FLIP differ only by what the effects drew
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../hle.h"
#include "../platform.h"

typedef struct {
    uint32_t fn;       // the class's draw into a pass (vtable +0x30): r0 = the unit, r1 = the pass
    const char *name;  // (the log's)
} EffectDraw;
static const EffectDraw DRAWS[] = {
    {0xbba5b0u, "uEffect"},          // (uProofEffect, uMHProofEffect; uProjectionEffect's 0xbc0b84 calls it)
    {0xe667a0u, "uSimpleEffect"},
    {0xbb827cu, "uCameraEffect"},
    {0xe5c80cu, "uEffect2D"},
    {0x52b8c0u, "uProofEffectModel"},
};
#define DRAWS_N ((int)(sizeof DRAWS / sizeof *DRAWS))
static uint32_t g_hooks = 7;  // (the bits of DRAWS hooked: MHGU_EFFECTS_HOOKS)
static bool g_log;

static int g_flip;  // (test) MHGU_EFFECTS_FLIP=<s>: drawn and left out in turns of that many seconds, Effects aside
static _Atomic uint32_t g_calls[DRAWS_N];

static bool g_freeze;  // (test) MHGU_EFFECTS_FREEZE=1: the engine's frame step held near 0 (the scene still, for shots)
#define G_SYSTEM 0x2326764u  // engine system object *: +0x68 the frame step in 60 fps frames (hle/buffcycle.c)

static bool effects_on(void) {
    if (g_freeze) {
        uint32_t sys = LD32(G_SYSTEM);
        float tiny = 0.0001f;
        uint32_t bits;
        memcpy(&bits, &tiny, 4);
        if (sys >= 0x1000u) ST32(sys + 0x68u, bits);
    }
    if (!g_flip) return config_live_effects();
    static _Atomic int last = -1;
    int on = (int)((plat_now_ns() / 1000000000ull / (uint64_t)g_flip) % 2 == 0);
    if (atomic_exchange(&last, on) != on) LOG("[effects] (MHGU_EFFECTS_FLIP) %s\n", on ? "drawn" : "left out");
    return on;
}

static void log_draw(int i, uint32_t unit) {
    static _Atomic uint32_t seen;
    static _Atomic uint64_t next_ns;
    atomic_fetch_add(&g_calls[i], 1);
    uint64_t now = plat_now_ns(), due = atomic_load(&next_ns);
    if (now >= due && atomic_compare_exchange_strong(&next_ns, &due, now + 10000000000ull) && due) {
        char line[256];
        int n = 0;
        for (int k = 0; k < DRAWS_N; k++)
            n += snprintf(line + n, sizeof line - (size_t)n, "%s%s %u", k ? ", " : "", DRAWS[k].name,
                          atomic_exchange(&g_calls[k], 0));
        LOG("[effects] the last 10 s's draws of effect units: %s\n", line);
    }
    if (atomic_fetch_or(&seen, 1u << i) & (1u << i)) return;
    LOG("[effects] %s 0x%08x draws: %s\n", DRAWS[i].name, unit, effects_on() ? "drawn" : "left out");
}

#define EFFECT_DRAW(i)                                             \
    static void effect_draw_##i(Cpu *c) {                          \
        if (g_log) log_draw(i, c->r[0]);                           \
        if (effects_on()) fnhook_call_original(c, DRAWS[i].fn);    \
    }
EFFECT_DRAW(0)
EFFECT_DRAW(1)
EFFECT_DRAW(2)
EFFECT_DRAW(3)
EFFECT_DRAW(4)
static void (*const HOOKS[DRAWS_N])(Cpu *) = {effect_draw_0, effect_draw_1, effect_draw_2, effect_draw_3, effect_draw_4};

void effects_init(void) {
    const char *e = getenv("MHGU_EFFECTS_LOG");
    g_log = e && *e == '1';
    if ((e = getenv("MHGU_EFFECTS_HOOKS"))) g_hooks = (uint32_t)strtoul(e, NULL, 0);
    if ((e = getenv("MHGU_EFFECTS_FLIP"))) g_flip = atoi(e) > 0 ? atoi(e) : 0;
    g_freeze = (e = getenv("MHGU_EFFECTS_FREEZE")) && *e == '1';
    int hooked = 0;
    for (int i = 0; i < DRAWS_N; i++) {
        if (!(g_hooks & (1u << i))) continue;
        char name[48];
        snprintf(name, sizeof name, "effects:%s::draw", DRAWS[i].name);
        hooked += fnhook_set(DRAWS[i].fn, hle_register(strdup(name), HOOKS[i]));
    }
    LOG("[effects] %d effect draws hooked (bits 0x%x); Effects %s\n", hooked, g_hooks, config_live_effects() ? "On" : "Off");
}
