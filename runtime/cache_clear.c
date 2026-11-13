// The shader cache, cleared at the player's request (the menu's "Clear shader cache", 1.0's S30: a cache that a driver
// update or a crash left wrong, a phone that misdraws until it is built again). The files are the start's and the
// game's own: the program cache (cache/programs.bin, programs.src), the recorded pipelines (cache/pipelines.bin), the
// SPIR-V (cache/spirv/), the driver's pipeline cache (cache/vk_pipelines.bin) and the mods' merged archives
// (cache/arcmerge/). All are made again as the game draws, so the first start after it builds every shader again.
// The game has them open while it runs: the menu only leaves a mark (cache/clear_at_start), and the next start deletes
// them before anything reads them (main.c, before the renderer starts). The paths are relative to MHGU_HOME, the
// process's folder, as exec.c and device.c open them.
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cache_clear.h"
#include "platform.h"
#include "rt.h"

#define CLEAR_MARK "cache/clear_at_start"
static const char *const FILES[] = {"cache/programs.bin", "cache/programs.src", "cache/pipelines.bin",
                                    "cache/vk_pipelines.bin", "cache/vk_pipelines.bin.tmp"};
static const char *const DIRS[] = {"cache/spirv", "cache/arcmerge"};

// (a folder's files, one level: what spirv/ and arcmerge/ hold) their bytes, and deleted when del
static uint64_t dir_files(const char *dir, bool del, int *count) {
    uint64_t bytes = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    char path[1024];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        bytes += (uint64_t)st.st_size;
        if (del && remove(path) == 0 && count) (*count)++;
    }
    closedir(d);
    return bytes;
}

static uint64_t cache_files(bool del, int *count) {
    uint64_t bytes = 0;
    for (size_t i = 0; i < sizeof FILES / sizeof *FILES; i++) {
        struct stat st;
        if (stat(FILES[i], &st) || !S_ISREG(st.st_mode)) continue;
        bytes += (uint64_t)st.st_size;
        if (del && remove(FILES[i]) == 0 && count) (*count)++;
    }
    for (size_t i = 0; i < sizeof DIRS / sizeof *DIRS; i++) bytes += dir_files(DIRS[i], del, count);
    return bytes;
}

uint64_t cache_clear_bytes(void) { return cache_files(false, NULL); }

bool cache_clear_pending(void) {
    struct stat st;
    return stat(CLEAR_MARK, &st) == 0;
}

bool cache_clear_request(bool on) {
    if (!on) return remove(CLEAR_MARK) == 0 || !cache_clear_pending();
    mkdir("cache", 0755);
    FILE *f = fopen(CLEAR_MARK, "w");
    if (!f) return false;
    fputs("the shader cache is deleted at the next start (the settings menu's Clear shader cache)\n", f);
    fclose(f);
    LOG("[cache] the shader cache will be cleared at the next start (%.1f MB now)\n", cache_clear_bytes() / 1048576.0);
    return true;
}

void cache_clear_at_start(void) {
    if (!cache_clear_pending()) return;
    int n = 0;
    uint64_t bytes = cache_files(true, &n);
    remove(CLEAR_MARK);
    LOG("[cache] the shader cache cleared, as asked in the menu: %d files, %.1f MB; every shader is built again this "
        "time\n", n, bytes / 1048576.0);
}
