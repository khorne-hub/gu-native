// The cheats loaded from cheats/ (runtime/cheats.c), for the settings' Cheats tab: fixed once cheats_init ran.
// cheats_set turns them on / off while the game runs (any thread: it takes effect the next frame) and keeps that in
// cheats/_off.txt. Master codes are always on.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
int cheats_count(void);
const char *cheats_name(int i);
const char *cheats_file(int i);
bool cheats_master(int i);
bool cheats_on(int i);
bool cheats_set(const bool *on, int n, char *err, size_t errsz);
// The file (cheats_file's spelling) was removed (a desktop's Cheats tab): its cheats stop at the next frame, their
// changes to the game's code undone as when turned off, and from then on they are off and left out of cheats/_off.txt
void cheats_drop_file(const char *file);
bool cheats_dropped(int i);
// Files left out whole: those named by another build id than the game's (made for another version). The count; i in
// range: *file (as cheats_file spells names) and *id (its first 16 hex digits) too
int cheats_left_out(int i, const char **file, const char **id);
#ifdef __cplusplus
}
#endif
