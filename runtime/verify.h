// Verify game files (System > Your game; runtime/verify.c): the game folder's exefs and romfs read whole and compared
// with the list of version 1.4.0's files (game_files.txt: each file's path, size and CRC-32, made by
// tools/game_files.py; no game data), on a thread of its own, with the game's code and the mods looked at too. The
// settings menu shows its progress and its report (settings_panel.cpp, View::Verify); the log gets every file.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { VERIFY_NONE, VERIFY_LISTING, VERIFY_READING, VERIFY_DONE, VERIFY_CANCELLED, VERIFY_FAILED };
// A report line's kind: a part that is right, a problem, a note worth knowing, an example file under the line before
// it, what to do
enum { VL_OK, VL_PROBLEM, VL_INFO, VL_FILE, VL_TODO };
#define VERIFY_ARG 192
typedef struct {
    int kind;
    const char *text;  // English, with up to two %s for a and b: settings_text translates it
    char a[VERIFY_ARG], b[VERIFY_ARG];
} VerifyLine;
typedef struct {
    int stage;
    uint64_t bytes_done, bytes_total;  // (the reading: the bytes of the files whose size is right)
    int files_done, files_total;
    double seconds;                    // since the reading started
    int problems;                      // (done or cancelled)
} VerifyState;

void verify_start(void);   // (a check running: nothing)
void verify_cancel(void);  // stops within a moment; the report says what was read
VerifyState verify_state(void);
// The report (done, cancelled or failed): its lines, up to max, copied; how many there are
int verify_lines(VerifyLine *out, int max);

#ifdef __cplusplus
}
#endif
