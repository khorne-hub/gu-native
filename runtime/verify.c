// Verify game files (verify.h). Two passes on a thread of its own: first the folder is listed and every file's size
// compared with version 1.4.0's list (seconds), then every file whose size is right is read whole and its CRC-32
// compared (minutes: 12.3 GB). The report goes to the settings menu (View::Verify) and, every file of it, to the log.
//
// The list (game_files.txt, tools/game_files.py) has a line per file: CRC-32, size and path from the game folder
// ("exefs/main", "romfs/nativeNX/..."): sizes and hashes only, nothing of the game's. Android reads it from the APK's
// assets (SDL's file functions do that for a relative path), a desktop from beside the executable.
//
// What a file that doesn't match is told as:
// - missing; named with other capitals (the game's lookups tell them apart: it doesn't find it)
// - cut short: smaller, and empty or not whole (an archive, .arc, whose entries reach past its end; a movie whose boxes
//   do; any other file smaller); copies cut short start and then fail later (#106)
// - different: as big but another CRC, bigger, or smaller but whole (an archive packed again): changed from 1.4.0's
// - the movies (nativeNX/movie) as big or not but whole: encoded again, which plays (#171): a note, not a problem
// - many different files: the copy looks modified (a repack), and the report says so plainly
#include "verify.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include <sys/auxv.h>
#include <sys/resource.h>
#endif
#if defined(__aarch64__)
#include <arm_acle.h>
#endif

#include "config.h"
#include "gamelib.h"
#include "modorder.h"
#include "overlay.h"
#include "rt.h"
#include "settings_locale.h"

#define LIST_NAME "game_files.txt"
#define ROMFS_FILES 35531          // (the list's own count is used; this is what the texts say)
#define BASE_ONLY 34149            // (romfs of the base game alone: main.c's comment, players' dumps)
#define MODIFIED_REPACK 20         // different files from which the copy is told as modified
#define FILE_EXAMPLES 5            // files shown under a line (the log has all)
#define LOG_FILES 2000

// ------------------------------------------------------------------ CRC-32 (zlib's: the list's, Python's zlib.crc32)
static uint32_t crc_table[8][256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++) crc_table[t][i] = crc_table[0][crc_table[t - 1][i] & 0xff] ^ (crc_table[t - 1][i] >> 8);
}
static uint32_t crc_soft(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n && ((uintptr_t)p & 7)) crc = crc_table[0][(crc ^ *p++) & 0xff] ^ (crc >> 8), n--;
    while (n >= 8) {
        uint32_t a, b;
        memcpy(&a, p, 4), memcpy(&b, p + 4, 4);
        a ^= crc;
        crc = crc_table[7][a & 0xff] ^ crc_table[6][(a >> 8) & 0xff] ^ crc_table[5][(a >> 16) & 0xff] ^ crc_table[4][a >> 24] ^
              crc_table[3][b & 0xff] ^ crc_table[2][(b >> 8) & 0xff] ^ crc_table[1][(b >> 16) & 0xff] ^ crc_table[0][b >> 24];
        p += 8, n -= 8;
    }
    while (n--) crc = crc_table[0][(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}
#if defined(__aarch64__)
// (the ARMv8 CRC32 instructions are zlib's polynomial; a phone without them, rare, takes the table)
__attribute__((target("crc"))) static uint32_t crc_arm(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n && ((uintptr_t)p & 7)) crc = __crc32b(crc, *p++), n--;
    while (n >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        crc = __crc32d(crc, v);
        p += 8, n -= 8;
    }
    while (n--) crc = __crc32b(crc, *p++);
    return ~crc;
}
#endif
static uint32_t (*crc32_of)(uint32_t, const uint8_t *, size_t) = crc_soft;
static void crc_pick(void) {
    crc_init();
#if defined(__aarch64__) && defined(__ANDROID__)
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1 << 7)
#endif
    if (getauxval(AT_HWCAP) & HWCAP_CRC32) crc32_of = crc_arm;
#endif
}

// ------------------------------------------------------------------ the state the menu reads
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static VerifyState g_state;
static VerifyLine *g_lines;  // (the published report)
static int g_nlines;
static atomic_int g_cancel;
static atomic_int g_running;

static void set_stage(int stage) {
    pthread_mutex_lock(&g_lock);
    g_state.stage = stage;
    pthread_mutex_unlock(&g_lock);
}

// ------------------------------------------------------------------ the list of 1.4.0's files
typedef struct {
    const char *path;  // "romfs/nativeNX/..."
    uint64_t size;
    uint32_t crc;
    int have;          // the file found for it (-1: none)
    int state;         // WS_*
} Want;
enum { WS_MISSING, WS_OK, WS_CUT, WS_DIFF, WS_MOVIE, WS_CASE, WS_UNREAD, WS_PENDING };
typedef struct {
    char *rel, *host;
    uint64_t size;
    int want;  // -1: a file the game doesn't have
} Have;

typedef struct {
    char *text;
    Want *w;
    int n;
    int *slot, *lower;  // open addressing: exact paths, and paths in lower case
    unsigned mask;
    Have *h;
    int nh, ch;
    int unread_dirs;
    char **unread;  // (folders that can't be listed)
    int nunread, cunread;
} Work;

static uint32_t hash_str(const char *s, bool lower) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)(lower ? tolower((unsigned char)*s) : *s)) * 16777619u;
    return h;
}
static void index_add(Work *k, int *slots, int i, bool lower) {
    unsigned at = hash_str(k->w[i].path, lower) & k->mask;
    while (slots[at] >= 0) at = (at + 1) & k->mask;
    slots[at] = i;
}
static int find_want(const Work *k, const char *path, bool lower) {
    const int *slots = lower ? k->lower : k->slot;
    for (unsigned at = hash_str(path, lower) & k->mask; slots[at] >= 0; at = (at + 1) & k->mask) {
        const char *p = k->w[slots[at]].path;
        if (lower ? !strcasecmp(p, path) : !strcmp(p, path)) return slots[at];
    }
    return -1;
}

static bool load_list(Work *k) {
    char path[1100];
#ifdef __ANDROID__
    snprintf(path, sizeof path, "%s", LIST_NAME);  // (the APK's assets)
#else
    char exe[1024];
    if (!plat_exe_path(exe, sizeof exe)) exe[0] = 0;
    char *s = strrchr(exe, '/'), *bs = strrchr(exe, '\\');
    if (bs > s) s = bs;
    if (s) s[1] = 0; else exe[0] = 0;
    snprintf(path, sizeof path, "%s%s", exe, LIST_NAME);
#endif
    const char *env = getenv("MHGU_GAME_FILES");  // (tests: another list)
    if (env && *env) snprintf(path, sizeof path, "%s", env);
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        LOG("[verify] %s: %s\n", path, SDL_GetError());
        return false;
    }
    k->text = malloc(size + 1);
    memcpy(k->text, text, size);
    k->text[size] = 0;
    SDL_free(text);
    int cap = 0;
    for (char *line = k->text; line && *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *t = line;
        line = end ? end + 1 : NULL;
        if (*t == '#' || !*t) continue;
        size_t l = strlen(t);
        if (l && t[l - 1] == '\r') t[l - 1] = 0;
        char *a = strchr(t, '\t'), *b = a ? strchr(a + 1, '\t') : NULL;
        if (!b) continue;
        *a = *b = 0;
        if (k->n == cap) k->w = realloc(k->w, sizeof *k->w * (cap = cap ? cap * 2 : 40000));
        Want *w = &k->w[k->n++];
        w->crc = (uint32_t)strtoul(t, NULL, 16), w->size = strtoull(a + 1, NULL, 10), w->path = b + 1;
        w->have = -1, w->state = WS_MISSING;
    }
    if (k->n < 100) return false;
    unsigned slots = 1;
    while (slots < (unsigned)k->n * 2) slots <<= 1;
    k->mask = slots - 1;
    k->slot = malloc(sizeof *k->slot * slots), k->lower = malloc(sizeof *k->lower * slots);
    memset(k->slot, 0xff, sizeof *k->slot * slots), memset(k->lower, 0xff, sizeof *k->lower * slots);
    for (int i = 0; i < k->n; i++) index_add(k, k->slot, i, false), index_add(k, k->lower, i, true);
    LOG("[verify] the list of version 1.4.0's files: %d files (%s)\n", k->n, path);
    return true;
}

// ------------------------------------------------------------------ the folder
static void walk(Work *k, const char *host, const char *rel, int depth) {
    if (depth > 32 || atomic_load(&g_cancel)) return;
    DIR *d = opendir(host);
    if (!d) {
        if (k->nunread == k->cunread) k->unread = realloc(k->unread, sizeof *k->unread * (k->cunread = k->cunread ? k->cunread * 2 : 16));
        k->unread[k->nunread++] = strdup(rel);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t hn = strlen(host) + strlen(e->d_name) + 2, rn = strlen(rel) + strlen(e->d_name) + 2;
        char *h = malloc(hn), *r = malloc(rn);
        snprintf(h, hn, "%s/%s", host, e->d_name);
        snprintf(r, rn, "%s/%s", rel, e->d_name);
        struct stat st;
        if (stat(h, &st) != 0) {
            if (k->nunread == k->cunread) k->unread = realloc(k->unread, sizeof *k->unread * (k->cunread = k->cunread ? k->cunread * 2 : 16));
            k->unread[k->nunread++] = r;
            free(h);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk(k, h, r, depth + 1);
            free(h), free(r);
            continue;
        }
        if (k->nh == k->ch) k->h = realloc(k->h, sizeof *k->h * (k->ch = k->ch ? k->ch * 2 : 40000));
        Have *f = &k->h[k->nh++];
        f->rel = r, f->host = h, f->size = (uint64_t)st.st_size, f->want = -1;
    }
    closedir(d);
}

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Is the file whole as its own format says? 1 yes, 0 no (cut short), -1 a kind it can't tell (MT Framework archives,
// .arc: their entries reach no further than the file; MP4 movies: their boxes add up to the file and one is the moov)
static int whole(const char *host, uint64_t size) {
    const char *dot = strrchr(host, '.');
    bool arc = dot && !strcasecmp(dot, ".arc"), mp4 = dot && !strcasecmp(dot, ".mp4");
    if (!arc && !mp4) return -1;
    int fd = open(host, O_RDONLY | O_BINARY);
    if (fd < 0) return -1;
    int ok = 0;
    if (arc) {
        uint8_t head[12];
        if (pread(fd, head, 12, 0) == 12 && !memcmp(head, "ARC", 4)) {
            uint32_t n = head[6] | head[7] << 8;
            uint64_t need = 12 + (uint64_t)n * 80;
            uint8_t *t = need <= size ? malloc((size_t)need) : NULL;
            if (t && pread(fd, t, (size_t)need, 0) == (ssize_t)need) {
                ok = 1;
                for (uint32_t i = 0; i < n && ok; i++) {
                    const uint8_t *e = t + 12 + i * 80;
                    if ((uint64_t)rd32le(e + 76) + rd32le(e + 68) > size) ok = 0;
                }
            }
            free(t);
        }
    } else {
        uint64_t at = 0;
        bool moov = false;
        while (at + 8 <= size) {
            uint8_t b[16];
            if (pread(fd, b, 16, (off_t)at) < 8) break;
            uint64_t len = rd32be(b);
            if (len == 1) len = (uint64_t)rd32be(b + 8) << 32 | rd32be(b + 12);
            else if (len == 0) len = size - at;
            if (len < 8) break;
            if (!memcmp(b + 4, "moov", 4)) moov = true;
            at += len;
        }
        ok = at == size && moov;
    }
    close(fd);
    return ok;
}

static bool is_movie(const char *rel) { return !strncasecmp(rel, "romfs/nativeNX/movie/", 21); }

// Reads a whole file: its CRC-32 (false: it can't be read, or the check was cancelled)
static bool read_crc(const char *host, uint64_t size, uint32_t *crc, uint8_t *buf, size_t bufsize, uint64_t *done) {
    int fd = open(host, O_RDONLY | O_BINARY);
    if (fd < 0) return false;
#ifdef __ANDROID__
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    uint32_t c = 0;
    uint64_t got = 0;
    bool ok = true;
    for (;;) {
        if (atomic_load(&g_cancel)) { ok = false; break; }
        ssize_t n = read(fd, buf, bufsize);
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        c = crc32_of(c, buf, (size_t)n);
        got += (uint64_t)n;
        pthread_mutex_lock(&g_lock);
        g_state.bytes_done += (uint64_t)n;
        pthread_mutex_unlock(&g_lock);
        if (got >= size) {
            uint8_t extra;
            if (read(fd, &extra, 1) > 0) got++;  // (grew meanwhile)
            break;
        }
    }
#ifdef __ANDROID__
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);  // (not kept in the cache: the game's own reads keep theirs)
#endif
    close(fd);
    *done += got;
    *crc = c;
    return ok && got == size;
}

// ------------------------------------------------------------------ the report
typedef struct {
    VerifyLine *l;
    int n, c;
    int logged;
} Report;
static void add(Report *r, int kind, const char *text, const char *a, const char *b) {
    if (r->n == r->c) r->l = realloc(r->l, sizeof *r->l * (r->c = r->c ? r->c * 2 : 64));
    VerifyLine *v = &r->l[r->n++];
    memset(v, 0, sizeof *v);
    v->kind = kind, v->text = text;
    if (a) snprintf(v->a, sizeof v->a, "%s", a);
    if (b) snprintf(v->b, sizeof v->b, "%s", b);
    char line[700];
    snprintf(line, sizeof line, text, v->a, v->b);
    static const char *kinds[] = {"ok", "PROBLEM", "note", "file", "to do"};
    if (kind != VL_FILE) LOG("[verify] %s: %s\n", kinds[kind], line);
}
static const char *num(char *buf, uint64_t v) {
    snprintf(buf, 32, "%llu", (unsigned long long)v);
    return buf;
}
static const char *gb(char *buf, uint64_t bytes) {
    snprintf(buf, 32, "%.1f", bytes / 1e9);
    return buf;
}
// A finding's files: the first few as lines under it, all of them in the log
static void files(Report *r, char **names, int n) {
    for (int i = 0; i < n; i++) {
        if (i < FILE_EXAMPLES) add(r, VL_FILE, "· %s", names[i], NULL);
        if (r->logged++ < LOG_FILES) LOG("[verify]     %s\n", names[i]);
    }
    char c[32];
    if (n > FILE_EXAMPLES) add(r, VL_FILE, "· and %s more", num(c, (uint64_t)(n - FILE_EXAMPLES)), NULL);
}
typedef struct {
    char **v;
    int n, c;
} Names;
static void names_add(Names *s, const char *name) {
    if (s->n == s->c) s->v = realloc(s->v, sizeof *s->v * (s->c = s->c ? s->c * 2 : 64));
    s->v[s->n++] = (char *)name;
}

// Problems first, then what is right, the notes and what to do; a line's example files stay under it
static void order(Report *r) {
    static const int rank[5] = {[VL_PROBLEM] = 0, [VL_OK] = 1, [VL_INFO] = 2, [VL_FILE] = 9, [VL_TODO] = 3};
    VerifyLine *out = malloc(sizeof *out * (r->n ? r->n : 1));
    int n = 0;
    for (int want = 0; want < 4; want++)
        for (int i = 0; i < r->n; i++) {
            if (rank[r->l[i].kind] != want) continue;
            out[n++] = r->l[i];
            for (int j = i + 1; j < r->n && r->l[j].kind == VL_FILE; j++) out[n++] = r->l[j];
        }
    free(r->l);
    r->l = out, r->n = r->c = n;
}

static void publish(Report *r, int stage, int problems) {
    pthread_mutex_lock(&g_lock);
    free(g_lines);
    g_lines = r->l, g_nlines = r->n;
    g_state.stage = stage;
    g_state.problems = problems;
    pthread_mutex_unlock(&g_lock);
}

// The game code: the one running is this version's (gamelib.c refused any other); the game folder's own copy, which a
// later choice of the folder uses, is looked at too
static void check_code(Report *r, int *problems, bool *todo_code) {
    const GameLibTables *t = gamelib_tables();
    char p[1200], id[64] = "";
#ifdef _WIN32
    const char *lib = "mhgugame.dll", *idname = "mhgugame.id";
#else
    const char *lib = "libmhgugame.so", *idname = "libmhgugame.id";
#endif
    snprintf(p, sizeof p, "%s/%s", g_game_dir, idname);
    FILE *f = fopen(p, "r");
    if (f) {
        if (!fgets(id, sizeof id, f)) id[0] = 0;
        fclose(f);
        id[strcspn(id, "\r\n ")] = 0;
    }
    snprintf(p, sizeof p, "%s/%s", g_game_dir, lib);
    struct stat st;
    bool have_lib = stat(p, &st) == 0 && st.st_size > 0;
    if (have_lib && strcmp(id, GAMELIB_ID)) {
        add(r, VL_PROBLEM, "The game code in the game folder (%s) was made for another version of the app (%s).", lib,
            id[0] ? id : "?");
        ++*problems;
        *todo_code = true;
    } else if (t && t->id && !strcmp(t->id, GAMELIB_ID)) {
        add(r, VL_OK, "Game code: made for this version of the app (%s).", GAMELIB_ID, NULL);
    }
#ifdef __ANDROID__
    if (!have_lib)
        add(r, VL_INFO, "The game folder has no libmhgugame.so: the game runs the copy kept in the app. Keep one in the folder for when you choose the folder again.",
            NULL, NULL);
#endif
}

// Mods that put files over the game's (in the game folder's mods/ or the app's: modorder.h), and the ones with code
static void check_mods(Report *r, Work *k, bool *todo_mods) {
    ModEntry *m = calloc(MODS_MAX, sizeof *m);
    int n = mods_installed(m, MODS_MAX);
    for (int i = 0; i < n && !atomic_load(&g_cancel); i++) {
        if (!m[i].on) continue;
        char dir[1100], romfs[1100], exefs[1100];
        snprintf(dir, sizeof dir, "%s/%s", mods_dir(), m[i].name);
        int replaced = 0;
        if (mods_romfs_of(m[i].name, romfs, sizeof romfs)) {
            Work w = {0};
            walk(&w, romfs, "romfs", 0);
            for (int j = 0; j < w.nh; j++) {
                replaced += find_want(k, w.h[j].rel, true) >= 0;
                free(w.h[j].rel), free(w.h[j].host);
            }
            for (int j = 0; j < w.nunread; j++) free(w.unread[j]);
            free(w.h), free(w.unread);
        }
        char c[32];
        if (replaced) {
            add(r, VL_INFO, "The mod \"%s\" replaces %s of the game's files.", m[i].name, num(c, (uint64_t)replaced));
            *todo_mods = true;
        }
        if (mods_find_dir(dir, "exefs", 3, exefs, sizeof exefs)) {
            add(r, VL_INFO, "The mod \"%s\" also changes the game's code (exefs).", m[i].name, NULL);
            *todo_mods = true;
        }
    }
    free(m);
}

static void *run(void *arg) {
    (void)arg;
#ifdef __ANDROID__
    setpriority(PRIO_PROCESS, (id_t)gettid(), 10);  // (the game first)
#elif defined(_WIN32)
    plat_thread_priority(PLAT_PRIO_BACKGROUND);
#endif
    uint64_t t0 = plat_now_ns();
    crc_pick();
    Work k = {0};
    Report r = {0};
    int problems = 0;
    char c1[32], c2[32];
    LOG("[verify] checking the game folder %s\n", g_game_dir ? g_game_dir : "(none)");
    if (!load_list(&k)) {
        add(&r, VL_PROBLEM, "The list of version 1.4.0's files is missing from the app (game_files.txt): install the app again.", NULL,
            NULL);
        publish(&r, VERIFY_FAILED, 1);
        free(k.text), free(k.w), free(k.slot), free(k.lower);
        atomic_store(&g_running, 0);
        return NULL;
    }
    // ---- the quick pass: what is there, and its sizes
    char exefs[1100], romfs[1100];
    snprintf(exefs, sizeof exefs, "%s/exefs", g_game_dir);
    snprintf(romfs, sizeof romfs, "%s/romfs", g_game_dir);
    struct stat st;
    bool have_exefs = stat(exefs, &st) == 0 && S_ISDIR(st.st_mode), have_romfs = stat(romfs, &st) == 0 && S_ISDIR(st.st_mode);
    if (have_exefs) walk(&k, exefs, "exefs", 0);
    if (have_romfs) walk(&k, romfs, "romfs", 0);
    if (atomic_load(&g_cancel)) {  // (cancelled while listing: the list is partial, so nothing is told about the files)
        LOG("[verify] cancelled while listing the files\n");
        add(&r, VL_INFO, "Cancelled while the files were being listed: nothing was checked.", NULL, NULL);
        publish(&r, VERIFY_CANCELLED, 0);
        for (int i = 0; i < k.nh; i++) free(k.h[i].rel), free(k.h[i].host);
        for (int i = 0; i < k.nunread; i++) free(k.unread[i]);
        free(k.h), free(k.unread), free(k.text), free(k.w), free(k.slot), free(k.lower);
        atomic_store(&g_running, 0);
        return NULL;
    }
    int romfs_have = 0;
    for (int i = 0; i < k.nh; i++) {
        Have *h = &k.h[i];
        romfs_have += !strncmp(h->rel, "romfs/", 6);
        int w = find_want(&k, h->rel, false);
        if (w < 0) continue;
        h->want = w, k.w[w].have = i;
        Want *x = &k.w[w];
        if (h->size == x->size) x->state = WS_PENDING;
        else if (h->size > x->size) x->state = is_movie(x->path) ? WS_MOVIE : WS_DIFF;
        else if (!h->size) x->state = WS_CUT;
        else {
            int ok = whole(h->host, h->size);
            x->state = ok == 1 ? (is_movie(x->path) ? WS_MOVIE : WS_DIFF) : WS_CUT;
        }
    }
    // (a file the list has under other capitals: there, but the game won't find it)
    for (int i = 0; i < k.nh; i++) {
        if (k.h[i].want >= 0) continue;
        int w = find_want(&k, k.h[i].rel, true);
        if (w >= 0 && k.w[w].have < 0) k.w[w].have = i, k.w[w].state = WS_CASE, k.h[i].want = w;
    }
    uint64_t total = 0;
    int ntotal = 0;
    for (int i = 0; i < k.n; i++)
        if (k.w[i].state == WS_PENDING) total += k.w[i].size, ntotal++;
    pthread_mutex_lock(&g_lock);
    g_state.stage = VERIFY_READING, g_state.bytes_total = total, g_state.files_total = ntotal;
    pthread_mutex_unlock(&g_lock);
    uint64_t t1 = plat_now_ns();
    LOG("[verify] listed %d files in %.1f s; reading %d of them (%.2f GB)\n", k.nh, (t1 - t0) / 1e9, ntotal, total / 1e9);
    // ---- the slow pass: every byte
    size_t bufsize = 1 << 20;
    uint8_t *buf = malloc(bufsize);
    uint64_t read_bytes = 0;
    int read_files = 0;
    for (int i = 0; i < k.n && !atomic_load(&g_cancel); i++) {
        Want *x = &k.w[i];
        if (x->state != WS_PENDING) continue;
        uint32_t crc;
        bool ok = read_crc(k.h[x->have].host, x->size, &crc, buf, bufsize, &read_bytes);
        if (atomic_load(&g_cancel)) break;
        x->state = !ok ? WS_UNREAD : crc == x->crc ? WS_OK : is_movie(x->path) && whole(k.h[x->have].host, x->size) == 1 ? WS_MOVIE : WS_DIFF;
        read_files++;
        pthread_mutex_lock(&g_lock);
        g_state.files_done = read_files;
        g_state.seconds = (plat_now_ns() - t1) / 1e9;
        pthread_mutex_unlock(&g_lock);
    }
    free(buf);
    bool cancelled = atomic_load(&g_cancel);
    atomic_store(&g_cancel, 0);  // (taken: the report still looks at the mods, in a moment)
    double secs = (plat_now_ns() - t1) / 1e9;
    LOG("[verify] %s: %d files read, %.2f GB in %.1f s (%.0f MB/s)\n", cancelled ? "cancelled" : "read", read_files,
        read_bytes / 1e9, secs, secs > 0 ? read_bytes / 1e6 / secs : 0);

    // ---- the report
    Names miss_ex = {0}, miss_ro = {0}, cut = {0}, diff_ex = {0}, diff_ro = {0}, movie = {0}, cased = {0}, unread = {0},
          extra_ex = {0}, extra_ro = {0};
    int empty = 0, modified = 0, missing_ro = 0;
    for (int i = 0; i < k.n; i++) {
        Want *x = &k.w[i];
        bool ex = !strncmp(x->path, "exefs/", 6);
        switch (x->state) {
        case WS_MISSING: names_add(ex ? &miss_ex : &miss_ro, x->path), missing_ro += !ex; break;
        case WS_CUT:
            names_add(&cut, x->path);
            empty += !k.h[x->have].size;
            break;
        case WS_DIFF: names_add(ex ? &diff_ex : &diff_ro, x->path), modified += !ex; break;
        case WS_MOVIE: names_add(&movie, x->path); break;
        case WS_CASE: names_add(&cased, k.h[x->have].rel); break;
        case WS_UNREAD: names_add(&unread, x->path); break;
        }
    }
    for (int i = 0; i < k.nunread; i++) names_add(&unread, k.unread[i]);
    for (int i = 0; i < k.nh; i++)
        if (k.h[i].want < 0) names_add(!strncmp(k.h[i].rel, "exefs/", 6) ? &extra_ex : &extra_ro, k.h[i].rel);
    int romfs_want = 0;
    for (int i = 0; i < k.n; i++) romfs_want += !strncmp(k.w[i].path, "romfs/", 6);
    bool todo_copy = false, todo_cut = false, todo_names = false, todo_read = false, todo_code = false, todo_mods = false;

    // exefs: there, 1.4.0's (the build id main.c checks), unchanged
    if (!have_exefs) {
        add(&r, VL_PROBLEM, "exefs: the folder is missing.", NULL, NULL), problems++, todo_copy = true;
    } else {
        char p[1200];
        snprintf(p, sizeof p, "%s/main", exefs);
        static const uint8_t want[8] = {0xfb, 0x08, 0xf1, 0xd2, 0x0f, 0xd1, 0x20, 0x4f};
        uint8_t hdr[0x48] = {0};
        FILE *f = fopen(p, "rb");
        bool v140 = f && fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "NSO0", 4) && !memcmp(hdr + 0x40, want, 8);
        if (f) fclose(f);
        if (miss_ex.n) {
            add(&r, VL_PROBLEM, "Files missing from exefs: %s.", num(c1, (uint64_t)miss_ex.n), NULL), problems++, todo_copy = true;
            files(&r, miss_ex.v, miss_ex.n);
        }
        if (f && !v140) {
            char id[20];
            for (int i = 0; i < 8; i++) snprintf(id + i * 2, 3, "%02X", hdr[0x40 + i]);
            add(&r, VL_PROBLEM, "exefs is not version 1.4.0's (its main is build %s): it is another update's or the base game's.", id, NULL);
            problems++, todo_copy = true;
        } else if (diff_ex.n) {
            add(&r, VL_PROBLEM, "Files in exefs different from version 1.4.0's (a patched copy?): %s.", num(c1, (uint64_t)diff_ex.n), NULL);
            problems++, todo_copy = true;
            files(&r, diff_ex.v, diff_ex.n);
        }
    }
    // romfs: every file there, whole and unchanged
    if (!have_romfs) {
        add(&r, VL_PROBLEM, "romfs: the folder is missing.", NULL, NULL), problems++, todo_copy = true;
    } else {
        if (miss_ro.n) {
            add(&r, VL_PROBLEM, "Files missing from romfs: %s of the game's %s.", num(c1, (uint64_t)miss_ro.n), num(c2, (uint64_t)romfs_want));
            problems++, todo_copy = true;
            files(&r, miss_ro.v, miss_ro.n);
        }
        if (cut.n) {
            add(&r, VL_PROBLEM, "Files cut short (smaller than they should be): %s, %s of them empty.", num(c1, (uint64_t)cut.n),
                num(c2, (uint64_t)empty));
            problems++, todo_cut = true;
            files(&r, cut.v, cut.n);
        }
        if (diff_ro.n) {
            add(&r, VL_PROBLEM, "Files in romfs different from version 1.4.0's: %s.", num(c1, (uint64_t)diff_ro.n), NULL);
            problems++, todo_copy = true;
            files(&r, diff_ro.v, diff_ro.n);
        }
    }
    if (cased.n) {
        add(&r, VL_PROBLEM, "Files named with other capitals (the game doesn't find them): %s.", num(c1, (uint64_t)cased.n), NULL);
        problems++, todo_names = true;
        files(&r, cased.v, cased.n);
    }
    if (unread.n) {
        add(&r, VL_PROBLEM, "Files or folders that could not be read: %s.", num(c1, (uint64_t)unread.n), NULL), problems++, todo_read = true;
        files(&r, unread.v, unread.n);
    }
    // a repack, or the wrong pieces put together
    if (modified >= MODIFIED_REPACK || (movie.n && modified >= 5)) {
        add(&r, VL_PROBLEM, "This copy looks modified (a repack from a download site?): %s of the game's files are changed. The port needs a copy made from your own game, untouched.",
            num(c1, (uint64_t)modified), NULL);
        problems++, todo_copy = true;
    }
    if (have_romfs && romfs_have <= 100 && romfs_want > 1000)
        add(&r, VL_INFO, "This romfs looks like the 1.4.0 update's alone, without the base game's files.", NULL, NULL);
    else if (have_romfs && missing_ro >= (romfs_want - BASE_ONLY) - 60 && missing_ro <= (romfs_want - BASE_ONLY) + 60)
        add(&r, VL_INFO, "This romfs looks like the base game's alone, without the 1.4.0 update applied.", NULL, NULL);
    // what is right
    bool exefs_read = have_exefs;  // (read first: done unless the check was cancelled at once)
    for (int i = 0; i < k.n; i++)
        if (!strncmp(k.w[i].path, "exefs/", 6) && k.w[i].state != WS_OK) exefs_read = false;
    if (exefs_read) add(&r, VL_OK, "exefs: version 1.4.0, unchanged.", NULL, NULL);
    if (have_romfs && !miss_ro.n && !cut.n && !diff_ro.n && !cased.n && !unread.n)
        add(&r, VL_OK, cancelled ? "romfs: all %s files are there, with the right size." : "romfs: all %s files are there, whole and unchanged.",
            num(c1, (uint64_t)romfs_want), NULL);
    check_code(&r, &problems, &todo_code);
    // worth knowing
    if (movie.n) {
        add(&r, VL_INFO, "The movies were encoded again (%s of them): they play, nothing to do.", num(c1, (uint64_t)movie.n), NULL);
        files(&r, movie.v, movie.n);
    }
    if (extra_ex.n) {
        add(&r, VL_INFO, "Files in exefs the game doesn't have: %s.", num(c1, (uint64_t)extra_ex.n), NULL);
        files(&r, extra_ex.v, extra_ex.n);
    }
    if (extra_ro.n) {
        add(&r, VL_INFO, "Files in romfs the game doesn't have (a mod copied into it?): %s.", num(c1, (uint64_t)extra_ro.n), NULL);
        files(&r, extra_ro.v, extra_ro.n);
    }
    check_mods(&r, &k, &todo_mods);
    if (cancelled) {
        pthread_mutex_lock(&g_lock);
        uint64_t done = g_state.bytes_done;
        pthread_mutex_unlock(&g_lock);
        add(&r, VL_INFO, "Cancelled after reading %s of %s GB: what wasn't read was checked by its size only.", gb(c1, done), gb(c2, total));
    }
    // what to do
    if (todo_copy)
        add(&r, VL_TODO, "Make your copy again from your own game, as the README's steps say: exefs from the 1.4.0 update and romfs of the base game with 1.4.0 applied.",
            NULL, NULL);
    else if (todo_cut)
        add(&r, VL_TODO, "Copy the romfs folder to the device again: some files were cut short when they were copied.", NULL, NULL);
    if (todo_names) add(&r, VL_TODO, "Copy the files again keeping their names as they are (some tools change the capitals).", NULL, NULL);
    if (todo_read)
        add(&r, VL_TODO, "Check that the app can read the whole game folder (access to all files) and that the storage or SD card works.",
            NULL, NULL);
#ifdef __ANDROID__
    if (todo_code) add(&r, VL_TODO, "Make the game code again: System › Game code (Build the game code), or the guide's tool on a PC.", NULL, NULL);
#else
    if (todo_code) add(&r, VL_TODO, "Make the game code again with this version's tool: python build_game.py --pc <your folder>.", NULL, NULL);
#endif
    if (todo_mods && problems == 0) add(&r, VL_TODO, "If the game fails, turn the mods off (Mods & Cheats › Mods) and try again.", NULL, NULL);
    LOG("[verify] %s: %d problem(s), %.1f s in all\n", cancelled ? "cancelled" : "done", problems, (plat_now_ns() - t0) / 1e9);
    order(&r);
    publish(&r, cancelled ? VERIFY_CANCELLED : VERIFY_DONE, problems);
    if (!cancelled && !overlay_menu_open()) {  // (the menu was closed meanwhile: the result over the game)
        int lang = config_language();
        char verdict[160], text[400];
        snprintf(verdict, sizeof verdict, settings_text(lang, problems == 0 ? "All good: your game files are right"
                                                              : problems == 1 ? "1 problem found" : "%d problems found"),
                 problems);
        snprintf(text, sizeof text, "%s. %s", verdict, settings_text(lang, "The report is in System › Your game › Verify game files."));
        overlay_notice(settings_text(lang, "Verify game files"), text);
    }
    free(miss_ex.v), free(miss_ro.v), free(cut.v), free(diff_ex.v), free(diff_ro.v), free(movie.v), free(cased.v), free(unread.v),
        free(extra_ex.v), free(extra_ro.v);
    for (int i = 0; i < k.nh; i++) free(k.h[i].rel), free(k.h[i].host);
    for (int i = 0; i < k.nunread; i++) free(k.unread[i]);
    free(k.h), free(k.unread), free(k.text), free(k.w), free(k.slot), free(k.lower);
    atomic_store(&g_running, 0);
    return NULL;
}

void verify_start(void) {
    if (atomic_exchange(&g_running, 1)) return;
    atomic_store(&g_cancel, 0);
    pthread_mutex_lock(&g_lock);
    memset(&g_state, 0, sizeof g_state);
    g_state.stage = VERIFY_LISTING;
    free(g_lines);
    g_lines = NULL, g_nlines = 0;
    pthread_mutex_unlock(&g_lock);
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &a, run, NULL)) {
        atomic_store(&g_running, 0);
        set_stage(VERIFY_FAILED);
    }
    pthread_attr_destroy(&a);
}

void verify_cancel(void) {
    if (atomic_load(&g_running)) atomic_store(&g_cancel, 1);
}

VerifyState verify_state(void) {
    pthread_mutex_lock(&g_lock);
    VerifyState s = g_state;
    pthread_mutex_unlock(&g_lock);
    return s;
}

int verify_lines(VerifyLine *out, int max) {
    pthread_mutex_lock(&g_lock);
    int n = g_nlines;
    if (out)
        for (int i = 0; i < n && i < max; i++) out[i] = g_lines[i];
    pthread_mutex_unlock(&g_lock);
    return n;
}
