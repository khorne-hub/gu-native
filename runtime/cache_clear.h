// The shader cache cleared at the next start (runtime/cache_clear.c; the settings menu's Clear shader cache)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
uint64_t cache_clear_bytes(void);   // what the shader cache's files take now
bool cache_clear_pending(void);     // the menu asked for it: the next start clears it
bool cache_clear_request(bool on);  // ask (or take back) the clearing at the next start; false: the mark can't be written
void cache_clear_at_start(void);    // main.c, before the renderer: clears it if asked, and logs what went
#ifdef __cplusplus
}
#endif
