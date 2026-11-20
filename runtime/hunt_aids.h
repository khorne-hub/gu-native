// Hunting aids (1.0.1, the owner's; MH4U for Android has the first two with the same names and texts), each off unless
// the player turns it on (Graphics & Audio > Screen and HUD; [Hunting Aids] in the settings file):
//   - Monsters always on the map: in a quest hunted alone, the large monsters show on the game's maps from the start, as
//     if marked (the game draws its own icons);
//   - Damage numbers: each hit on a large monster shows its damage as a number over the monster, which rises and fades
//     in a second; their size and colour;
//   - Monster health bars: a bar a large monster at the top of the screen in a quest, with its name and its health
//     (MH4U for Android's look), moved and sized in the HUD editors.
// The game's side (runtime/hle/huntaids.c) reads and writes the game's memory once a frame, on the game's update thread,
// after the game's update: no hook of the game's code, the game library is the same. The drawing (runtime/hunt_overlay.cpp)
// is the overlay's, over the game's picture and under the touch controls: frame generation and the upscalers never touch it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUNT_NUMBERS 24      // damage numbers kept at once (the oldest goes first)
#define HUNT_BARS 5          // monsters with a bar at once
#define HUNT_NUMBER_NS 1000000000ull  // how long a number shows (it rises and fades meanwhile)

// What a hit was (1.0.2, the "By hit" colour): read from the game's damage step as the hit is worked out
// (runtime/hle/huntaids.c, hit_part); PLAIN a drop of health with no hit of this game's read for it (poison, another
// hunter's hit in a party, a bomb the step doesn't go through)
enum { HUNT_HIT_PLAIN, HUNT_HIT_NORMAL, HUNT_HIT_WEAK, HUNT_HIT_HARD, HUNT_HIT_FIXED };
// [Hunting Aids] DamageNumberStyle: the numbers' own looks (none of another game's)
enum { HUNT_STYLE_CLASSIC, HUNT_STYLE_OUTLINED, HUNT_STYLE_COMPACT, HUNT_STYLE_BURST };
#define HUNT_COLOUR_BY_HIT 5  // [Hunting Aids] DamageNumberColour = ByHit
#define HUNT_BURST_NS 1200000000ull  // (Burst) a hit within this of the last one adds to its number

typedef struct {
    float x, y;        // where its hit is on the game's picture this frame: 0..1 from its left and top edges
    bool shown;        // in front of the camera (else not drawn this frame)
    int value;         // the damage (By hit: its raw part, elem the rest)
    uint32_t id;       // its own number (a steady sideways offset for numbers at one place)
    uint64_t born_ns;  // when it was dealt (plat_now_ns)
    uint64_t last_ns;  // the last hit it holds (Burst: a number sums a run of hits; else born_ns)
    uint8_t kind;      // HUNT_HIT_*
    int8_t crit;       // 1 a critical hit, -1 a negative one (an affinity below 0), 0 neither
    uint8_t element;   // the weapon's element: 0 none, 1 fire, 2 water, 3 thunder, 4 ice, 5 dragon
    int elem;          // the element's part of the damage (By hit: drawn as a second, smaller number)
    int hits;          // how many hits it holds (Burst; 1 for the others)
} HuntNumber;

typedef struct {
    int hp, max;
    bool here;         // in the hunter's zone of the map (else drawn fainter, as MH4U's)
    char name[48];     // its name in the game's own language ("" when the game's text has none the overlay can draw)
} HuntBar;

typedef struct {
    int nnum, nbar;
    HuntNumber num[HUNT_NUMBERS];
    HuntBar bar[HUNT_BARS];
    int number_size, number_colour;  // [Hunting Aids] DamageNumberSize (0 Medium, 1 Small, 2 Large), DamageNumberColour
    int number_style;                // [Hunting Aids] DamageNumberStyle (HUNT_STYLE_*)
} HuntView;

// (game's update thread, once a frame, after the game's update: runtime/overlay.cpp overlay_frame) both sides' step:
// the marks kept, the numbers made and placed for this frame, the bars read
void huntaids_frame(void);
// (game thread, the overlay's frame) what to draw now; false: nothing (every aid off, or none to show). huntaids_any:
// only whether there is anything
bool huntaids_view(HuntView *out);
bool huntaids_any(void);
// (game thread, inside the overlay's ImGui frame: runtime/hunt_overlay.cpp) the numbers and the bars, display pixels; and
// (MHGU_PERF_OVERLAY_REUSE) whether the frame built last drew just what they would draw now (bars alone that didn't
// change, or nothing: a number moves every frame)
void hunt_overlay_draw(float display_w, float display_h);
bool hunt_overlay_unchanged(float display_w, float display_h);
// (any thread) where the monster health bars were drawn last (display pixels: x0, y0, x1, y1), in a quest or as their
// stand-in in an editor; false while they aren't drawn. The editors frame them by it, and move and size them
// (runtime/touch.cpp: the layout's monster_bars line), which touch_bars_place hands back (x, y: the bars' top middle in
// the screen's width and height, < 0 their own place; k: their size, 1 the default)
bool hunt_bars_rect(float r[4]);
void touch_bars_place(float *x, float *y, float *k);
// (tests: the control file's "huntaids <map> <numbers> <bars> [size] [colour]", runtime/host_input.c) the three aids, the
// numbers' size and colour from now on, as if the menu had saved them (not saved); config_live_hunt_* read them
void config_huntaids_test(int map, int numbers, int bars, int size, int colour, int style);
int config_live_monsters_on_map(void);  // [Hunting Aids] MonstersOnMap
int config_live_damage_numbers(void);   // [Hunting Aids] DamageNumbers
int config_live_number_size(void);      // [Hunting Aids] DamageNumberSize: 0 Medium, 1 Small, 2 Large
int config_live_number_colour(void);    // [Hunting Aids] DamageNumberColour: 0 White, 1 Yellow, 2 Red, 3 Light blue, 4 Green,
                                        // 5 By hit
int config_live_number_style(void);     // [Hunting Aids] DamageNumberStyle: 0 Classic, 1 Outlined, 2 Compact, 3 Burst
int config_live_monster_bars(void);     // [Hunting Aids] MonsterHealthBars

#ifdef __cplusplus
}
#endif
