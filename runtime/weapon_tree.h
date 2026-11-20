// The Smithy's weapon tree (1.0.2, the owner's; MH4U for Android has the same with the same texts): while the game's
// Smithy shows a weapon (Forge's list, Upgrade's list), a touch button of its own ("weapon_tree": it comes and goes with
// the Smithy's weapon) or the right stick's click (R3, a controller's or its key) opens the tree of that weapon over the
// game: the ones the player has (box, worn), the ones before them in their line and the next step from each as "?"
// (runtime/weapon_tree_view.h, drawn by runtime/weapon_tree_panel.cpp, the same files as MH4U's). Read-only: nothing is
// forged from here. B, Back, a tap beside it or its X close it; the d-pad or the stick moves its cursor, a finger taps a
// weapon or drags the tree. [Controls] WeaponTree (on unless off) turns the button and R3 off.
//
// Two sides: the game's (runtime/hle/weapontree.c: what the Smithy shows and the player's box, read from the game's
// memory; the Smithy's object from a hook of its update) and the app's
// (runtime/weapon_tree_ui.cpp: the game's weapon tables and names from the RomFS, the panel, the fingers and the
// controller). The game library is the same (GAMELIB_ID unchanged).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL_events.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- the game's side (runtime/hle/weapontree.c)
// What the Smithy shows now: the weapon under its list's cursor, as the game's class number (0 the great sword .. 14 the
// charge blade, no 5: the tables' weaponNN), its base (the line's id in weaponNNBaseData) and level (1 on). serial
// grows each time the weapon shown changes. The game's language: 0 jpn, 1 eng, 2 fre, 3 spa, 4 ger, 5 ita, 6..8 kor,
// chT, chS (sGameControl +0xa5)
typedef struct {
    bool shown;
    int cls, base, level;
    int language;
    uint32_t serial;
} WtSmithy;
void weapontree_init(void);  // (the Smithy's update hooked: fnhook_set, which leaves the game library as it is)
// (game thread, once a frame after the game's update: runtime/overlay.cpp overlay_frame)
void weapontree_frame(void);
// (any thread) the last frame's
WtSmithy weapontree_smithy(void);
// (game thread) the player's weapons of class cls from the equipment box: levels[base] the highest level of that base
// in the box (0 none; n bases at most), and the one worn (its base and level, -1 none of that class)
void weapontree_box(int cls, uint8_t *levels, int n, int *worn_base, int *worn_level);

// ---- the app's side (runtime/weapon_tree_ui.cpp)
// (any thread) the Smithy shows a weapon and the tree is closed: the button shows, R3 opens it
bool weapon_tree_available(void);
bool weapon_tree_is_open(void);
// (input thread) open it (the touch button, R3), close it; Android's Back: closes it (true when it was open)
void weapon_tree_open(bool by_pad);
void weapon_tree_close(void);
bool weapon_tree_back(void);
// (input thread, runtime/host_input.c) the buttons for the game: R3 opens or closes the tree; while it is open the
// controller and the keys are the panel's (the game gets nothing, nor a button held as it closed until let go)
uint64_t weapon_tree_buttons(uint64_t buttons, int lx, int ly);
// (input thread, runtime/host_input.c) while it is open: Back and Esc close it, the fingers and the mouse (a desktop's)
// are the panel's; true when the event was its
bool weapon_tree_event(const SDL_Event *e);
// (game thread, inside the overlay's ImGui frame: runtime/overlay.cpp) the panel, over the touch controls; whether it
// is drawn now (open, or fading out)
void weapon_tree_draw(float display_w, float display_h);
bool weapon_tree_drawing(void);

#ifdef __cplusplus
}
#endif
