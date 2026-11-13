// MHGU's side of the port's chat (runtime/chat_port.cpp; the chat itself: runtime/chat/chat.h). C and C++.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void chat_port_init(void);   // the overlay's first frame: the chat started (its relays only if the player turned them on)
bool chat_port_frame(void);  // the game's thread, each frame: true while the chat draws over the game
void chat_port_draw(float w, float h);  // (then) the chat button and the last lines, over the touch controls
// runtime/touch.cpp: a finger that landed on no control (fractions of the screen): true when the chat button took it;
// then its moves and its lift (a drag moves the button, a tap opens the chat)
bool chat_bubble_down(float fx, float fy);
void chat_bubble_finger(bool up, float fx, float fy);
// the menu: the button's size (1: its own; .6 to 2), and its place and size back as they come
void chat_bubble_size(float s);
float chat_bubble_scale(void);
void chat_bubble_reset(void);

#ifdef __cplusplus
}
#endif
