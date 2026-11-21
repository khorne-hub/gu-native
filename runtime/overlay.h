// Forge's ImGui overlay (runtime/overlay.cpp): its menu window with the plugins' settings, drawn over the game.
// C and C++.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// From runtime/forge.c once plugins are loaded: font (a .ttf; NULL: ImGui's own), its size in pixels at 720 lines
// (scaled with the window), the key that opens the menu (SDL scancode = HID usage, as Forge's config has it).
void overlay_init(const char *font_path, float font_size, int menu_key);
bool overlay_enabled(void);
bool overlay_menu_open(void);
// (1.0.2, after tabarong's floating chat) the menu is up as the chat window afloat over the game (settings_panel.h): the
// game goes on and the touch controls are played round it; its controls alone take a finger
bool overlay_chat_floating(void);
bool overlay_chat_typing(void);  // (...and Say something is typed in it: the keys are its text, not the game's)
// Forge's menu from the settings' Mods tab (any thread): it opens once the settings menu closes; opened by touch, it is
// bigger and scrolls by dragging. overlay_forge_typing: one of its fields takes text now (the phone's keyboard).
void overlay_open_forge(void);
bool overlay_forge_typing(void);
bool overlay_settings_dirty(void);
void overlay_request_close(void); // Close / Alt+F4 routes to controller confirmation inside settings.
void overlay_face_positions(const int positions[4]);
int overlay_face_position(int semantic); // A, B, X, Y -> physical south/east/west/north.
void overlay_toggle_settings(void); // host main thread: F1 or Start + Back
// Any thread: a notice shown over the game for a few seconds (title on its own line; the latest replaces any other).
void overlay_notice(const char *title, const char *text);
// What the player used last (runtime/host_input.c, from every event; any thread reads it): the port's menus show a
// controller's buttons only after a controller's press, and the keys (or the mouse) after the keyboard's or the mouse's
enum { USED_KEYS, USED_MOUSE, USED_PAD, USED_TOUCH };
void overlay_input_seen(int kind);
int overlay_last_input(void);

// Host main thread: an event for ImGui while the menu is open (true: taken, not for the game) - and the menu key.
bool overlay_event(const SDL_Event *e);
// Host main thread, while the menu is open: the controller, which moves through the menu instead of the game.
typedef struct { bool a, b, x, y, up, down, left, right, l, r, plus, minus; float lx, ly; } OverlayPad;
void overlay_pad(const OverlayPad *p);

// The game's thread, once a frame (after the plugins' forge_onUpdate): the overlay's frame, plugins' UI included.
void overlay_frame(void *cpu);
// The game's thread, inside the overlay's frame: a texture of the port's own (IM_NEW'd and ImGui::RegisterUserTexture'd:
// the players' pictures on the buttons, runtime/touch.cpp) no longer drawn, handed over. The overlay destroys it once no
// frame being drawn can still use it, takes it out of ImGui's list and deletes it.
struct ImTextureData;
void overlay_retire_texture(struct ImTextureData *tex);

// Render thread, inside the window image's render pass (runtime/nvn/vulkan/device.c, under the Vulkan lock).
typedef struct {
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    uint32_t qfam;
    VkQueue queue;
    VkPipelineCache cache;
    VkRenderPass pass;
    uint32_t width, height;
    int turn;  // the window image drawn turned for the panel: 0, 1 (90 degrees) or 3 (270) quarter turns (device.c sc_turn)
    float hdr_paper;  // an HDR10 window image ([Colour] HDR, runtime/colour_filter.h): the overlay's white, in 10000s of
                      // nits (its shader encodes for it); 0: SDR, as always
} OverlayTarget;
void overlay_draw(VkCommandBuffer cb, const OverlayTarget *t);

#ifdef __cplusplus
}
#endif
