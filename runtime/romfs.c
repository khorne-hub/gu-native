// Virtual RomFS image (romfs.h). Layout as a real image: a 0x50-byte header, file data from 0x200 (each file
// 16-byte aligned), then the directory hash table, directory entries, file hash table and file entries. The game's
// RomFS code walks the tables itself, finding entries by hash, so their order only has to be consistent.
#include "romfs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arcmerge.h"
#include "rt.h"
#include "verify.h"

#define EMPTY 0xFFFFFFFFu
#define DATA_START 0x200u

typedef struct Dir Dir;
typedef struct {
    char *name, *path;  // path: the host file
    char *orig;         // the game's own file, once a mod has replaced it
    uint64_t size, data_off;
    uint32_t off;
    Dir *parent;
    char *rpath;        // path inside the RomFS
    _Atomic bool listed;  // (its size from the list kept between starts: checked when the game first opens it)
} File;
struct Dir {
    char *name;
    Dir *parent;
    Dir **dirs;
    File **files;
    int ndirs, nfiles, cdirs, cfiles;
    uint32_t off;
    bool gone;  // (scan_par) it could not be listed: taken out of its parent once the scan is over
    bool mt;    // (the top LIST_DEPTH levels) its time of change as it was listed: mt_s, mt_ns
    int64_t mt_s, mt_ns;
};

// (the list kept between starts, below) folders this deep have their time of change kept with it: the game's 79
#define LIST_DEPTH 3
static void dir_mtime(Dir *d, int fd, const char *path) {
    struct stat st;
    if ((fd >= 0 ? fstat(fd, &st) : stat(path, &st)) != 0) return;
    d->mt = true, d->mt_s = (int64_t)st.st_mtime;
#ifdef _WIN32
    d->mt_ns = 0;
#else
    d->mt_ns = (int64_t)st.st_mtim.tv_nsec;
#endif
}

static int by_name_dir(const void *a, const void *b) { return strcmp((*(Dir *const *)a)->name, (*(Dir *const *)b)->name); }
static int by_name_file(const void *a, const void *b) { return strcmp((*(File *const *)a)->name, (*(File *const *)b)->name); }

static void add_dir(Dir *d, Dir *c) {
    if (d->ndirs == d->cdirs) d->dirs = realloc(d->dirs, sizeof *d->dirs * (d->cdirs = d->cdirs ? d->cdirs * 2 : 8));
    d->dirs[d->ndirs++] = c;
    c->parent = d;
}
static void add_file(Dir *d, File *f) {
    if (d->nfiles == d->cfiles) d->files = realloc(d->files, sizeof *d->files * (d->cfiles = d->cfiles ? d->cfiles * 2 : 16));
    d->files[d->nfiles++] = f;
    f->parent = d;
}

// Entries a scan couldn't read (stat or opendir refused: the game then misses those files), the first few logged
// (from several threads at once: scan_par)
static _Atomic int g_unread;
static void unread(const char *p) {
    int err = errno;
    if (atomic_fetch_add(&g_unread, 1) < 5) LOG("[romfs] WARNING: can't read %s: %s\n", p, strerror(err));
}

static Dir *scan(const char *path, const char *name, int depth);

// (scan) One entry of dir, at host path p (malloc'd: a file keeps it): a folder scanned in turn, a file added, else dropped
static void scan_entry(Dir *dir, char *p, const char *name, bool is_dir, bool is_file, uint64_t size, int depth) {
    if (is_dir) {
        Dir *c = scan(p, name, depth + 1);
        free(p);
        if (c) add_dir(dir, c);
    } else if (is_file) {
        File *f = calloc(1, sizeof *f);
        f->name = strdup(name), f->path = p, f->size = size;
        add_file(dir, f);
    } else {
        free(p);
    }
}

static char *join(const char *path, const char *name) {
    size_t n = strlen(path) + strlen(name) + 2;
    char *p = malloc(n);
    snprintf(p, n, "%s/%s", path, name);
    return p;
}

#ifdef _WIN32
// (Windows) Each entry's size as its folder lists it (plat_list_dir), no file opened. A stat per file opened all of the
// game's 35,531, and on a hard disk whose cache had let their records go that was a disk read each: the game sat between
// "GetService fsp-srv" and the first [romfs] line at 0.4 % CPU, looking hung, from under 2 minutes to over 6 (PC tests of
// 1.0.1 on a shared hard disk; 1.0's scan was the same). Listed, only the 2,321 folders are read, and main.c's
// plat_count_files has just listed them. A link or other reparse point is still stat'ed: it counts as what it points at.
typedef struct { Dir *dir; const char *path; int depth; } ScanAt;
static void scan_listed(const char *name, int kind, uint64_t size, void *arg) {
    ScanAt *at = arg;
    if (name[0] == '.') return;
    char *p = join(at->path, name);
    if (kind == PLAT_DIR_LINK) {
        struct stat st;
        if (stat(p, &st) != 0) { unread(p), free(p); return; }
        scan_entry(at->dir, p, name, S_ISDIR(st.st_mode), S_ISREG(st.st_mode), (uint64_t)st.st_size, at->depth);
    } else {
        scan_entry(at->dir, p, name, kind == PLAT_DIR_FOLDER, kind == PLAT_DIR_FILE, size, at->depth);
    }
}
#else
// (Android, Linux) The folder tree read by several threads at once (scan_par). On Android's shared storage each
// folder's listing goes through the system's FUSE daemon, and that is where a scan's time went: on an RG406V listing
// the game's 2,321 folders alone took 11.4 s, with a stat of every file 11.8 s, with a stat by the whole path (1.0.1's
// scan) 12.9 s, warm or not (a test program on the phone, the shell's same /storage/emulated/0). The daemon answers
// several at once: 7.9 s with 2 threads, 5.5 with 4, 2.9 with 8, 1.9 with 16, the same files, folders and bytes. A job
// is one folder: its worker lists it, stats what the listing doesn't say is a folder (fstatat on the listing's own
// descriptor: one lookup, not the path's every part), and adds its files and its folders (each a new job) to that
// folder's own lists, so no two threads touch one Dir. One that can't be listed is marked and taken out at the end,
// as scan() leaves it out. The tree is scan()'s: the same entries (sort_tree orders them by name), sizes and paths.
// MHGU_PERF_ROMFS=0: one thread, 1.0.1's walk; =<n>: n threads (2 to 32).
#define SCAN_THREADS 8
static bool g_scan_one;  // (romfs_check: 1.0.1's walk, one thread)
static int scan_threads(void) {
    static int n;
    if (!n) {
        const char *e = getenv("MHGU_PERF_ROMFS");
        n = !e ? SCAN_THREADS : atoi(e) <= 1 ? 1 : atoi(e) > 32 ? 32 : atoi(e);
        if (e) LOG("[perf] MHGU_PERF_ROMFS=%s: the game's folder listed by %d thread(s)\n", e, n);
    }
    return n;
}

typedef struct ScanJob { Dir *dir; char *path; int depth; struct ScanJob *next; } ScanJob;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    ScanJob *head;
    int busy;  // (folders being listed: they may still add jobs)
} ScanQueue;

static void scan_push(ScanQueue *q, Dir *dir, char *path, int depth) {
    ScanJob *j = malloc(sizeof *j);
    j->dir = dir, j->path = path, j->depth = depth;
    pthread_mutex_lock(&q->lock);
    j->next = q->head, q->head = j;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

static void scan_job(ScanQueue *q, ScanJob *j) {
    int fd = open(j->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dd = fd >= 0 ? fdopendir(fd) : NULL;
    if (!dd) {
        if (j->depth) unread(j->path);  // (a top folder that isn't there is told where it is opened)
        if (fd >= 0) close(fd);
        j->dir->gone = true;
        return;
    }
    if (j->depth <= LIST_DEPTH) dir_mtime(j->dir, fd, NULL);  // (before its entries are read)
    struct dirent *e;
    while ((e = readdir(dd))) {
        if (e->d_name[0] == '.') continue;
        char *p = join(j->path, e->d_name);
        bool is_dir = e->d_type == DT_DIR, is_file = false;
        uint64_t size = 0;
        if (!is_dir) {  // (a file, a link: what it points at, as stat; a file system without types)
            struct stat st;
            if (fstatat(dirfd(dd), e->d_name, &st, 0) != 0) { unread(p), free(p); continue; }
            is_dir = S_ISDIR(st.st_mode), is_file = S_ISREG(st.st_mode), size = (uint64_t)st.st_size;
        }
        if (is_dir) {
            if (j->depth + 1 > 32) { free(p); continue; }  // (scan's depth rule)
            Dir *c = calloc(1, sizeof *c);
            c->name = strdup(e->d_name);
            add_dir(j->dir, c);
            scan_push(q, c, p, j->depth + 1);
        } else if (is_file) {
            File *f = calloc(1, sizeof *f);
            f->name = strdup(e->d_name), f->path = p, f->size = size;
            add_file(j->dir, f);
        } else {
            free(p);
        }
    }
    closedir(dd);
}

static void *scan_worker(void *arg) {
    ScanQueue *q = arg;
    pthread_mutex_lock(&q->lock);
    for (;;) {
        while (!q->head && q->busy) pthread_cond_wait(&q->cond, &q->lock);
        if (!q->head) break;  // (nothing queued and nothing being listed: done)
        ScanJob *j = q->head;
        q->head = j->next, q->busy++;
        pthread_mutex_unlock(&q->lock);
        scan_job(q, j);
        free(j->path), free(j);
        pthread_mutex_lock(&q->lock);
        if (!--q->busy && !q->head) pthread_cond_broadcast(&q->cond);
    }
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return NULL;
}

static void scan_prune(Dir *d) {  // (folders that couldn't be listed out, as scan() leaves them out)
    int k = 0;
    for (int i = 0; i < d->ndirs; i++) {
        Dir *c = d->dirs[i];
        if (c->gone) { free(c->name), free(c->dirs), free(c->files), free(c); continue; }
        scan_prune(c);
        d->dirs[k++] = c;
    }
    d->ndirs = k;
}

static Dir *scan_par(const char *path, const char *name, int threads) {
    ScanQueue q = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, 0};
    Dir *root = calloc(1, sizeof *root);
    root->name = strdup(name);
    scan_push(&q, root, strdup(path), 0);
    pthread_t th[32];
    int n = 0;
    while (n < threads - 1 && pthread_create(&th[n], NULL, scan_worker, &q) == 0) n++;
    scan_worker(&q);  // (this thread lists too)
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    pthread_cond_destroy(&q.cond), pthread_mutex_destroy(&q.lock);
    if (root->gone) { free(root->name), free(root); return NULL; }
    scan_prune(root);
    return root;
}
#endif

static Dir *scan(const char *path, const char *name, int depth) {
    if (depth > 32) return NULL;
#ifndef _WIN32
    if (!depth && !g_scan_one && scan_threads() > 1) return scan_par(path, name, scan_threads());
#endif
#ifdef _WIN32
    Dir *dir = calloc(1, sizeof *dir);
    dir->name = strdup(name);
    if (depth <= LIST_DEPTH) dir_mtime(dir, -1, path);  // (before its entries are read)
    ScanAt at = {dir, path, depth};
    if (!plat_list_dir(path, scan_listed, &at)) {
        if (depth) unread(path);  // (a top folder that isn't there is told where it is opened)
        free(dir->name), free(dir);
        return NULL;
    }
    return dir;
#else
    DIR *dd = opendir(path);
    if (!dd) {
        if (depth) unread(path);  // (a top folder that isn't there is told where it is opened)
        return NULL;
    }
    Dir *dir = calloc(1, sizeof *dir);
    dir->name = strdup(name);
    if (depth <= LIST_DEPTH) dir_mtime(dir, dirfd(dd), NULL);  // (before its entries are read)
    struct dirent *e;
    while ((e = readdir(dd))) {
        if (e->d_name[0] == '.') continue;
        char *p = join(path, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) { unread(p), free(p); continue; }
        scan_entry(dir, p, e->d_name, S_ISDIR(st.st_mode), S_ISREG(st.st_mode), (uint64_t)st.st_size, depth);
    }
    closedir(dd);
    return dir;
#endif
}

// overlay `src` onto `dst`: its files replace or join dst's, its folders merge. Names match in any case (a mod's
// nativenx/ is the game's nativeNX/: Windows doesn't tell them apart either); the game's own spelling stays.
static int merge(Dir *dst, Dir *src) {
    int changed = 0;
    for (int i = 0; i < src->nfiles; i++) {
        File *f = src->files[i];
        int k = 0;
        while (k < dst->nfiles && strcasecmp(dst->files[k]->name, f->name)) k++;
        if (k < dst->nfiles) {
            File *d = dst->files[k];
            d->listed = false;  // (the mod's file, or both's archive: listed now, its size known)
            size_t nl = strlen(f->name);
            char merged[1200];
            if (d->orig && nl > 4 && !strcasecmp(f->name + nl - 4, ".arc") &&
                arc_merge(d->orig, d->path, f->path, merged, sizeof merged)) {  // (two mods' archives: both's changes)
                struct stat st;
                if (!stat(merged, &st)) {
                    free(d->path);
                    d->path = strdup(merged);
                    d->size = (uint64_t)st.st_size;
                    changed++;
                    continue;
                }
            }
            if (!d->orig) d->orig = strdup(d->path);
            free(d->path);
            d->path = strdup(f->path);
            d->size = f->size;
        } else {
            File *n = calloc(1, sizeof *n);
            n->name = strdup(f->name), n->path = strdup(f->path), n->size = f->size;
            add_file(dst, n);
        }
        changed++;
    }
    for (int i = 0; i < src->ndirs; i++) {
        Dir *s = src->dirs[i];
        int k = 0;
        while (k < dst->ndirs && strcasecmp(dst->dirs[k]->name, s->name)) k++;
        if (k == dst->ndirs) {
            Dir *n = calloc(1, sizeof *n);
            n->name = strdup(s->name);
            add_dir(dst, n);
        }
        changed += merge(dst->dirs[k], s);
    }
    return changed;
}

static void sort_tree(Dir *d) {
    if (d->ndirs) qsort(d->dirs, d->ndirs, sizeof *d->dirs, by_name_dir);
    if (d->nfiles) qsort(d->files, d->nfiles, sizeof *d->files, by_name_file);
    for (int i = 0; i < d->ndirs; i++) sort_tree(d->dirs[i]);
}

typedef struct { Dir **dirs; File **files; int nd, nf, cd, cf; } Lists;
static void flatten(Dir *d, Lists *l) {
    for (int i = 0; i < d->nfiles; i++) {
        if (l->nf == l->cf) l->files = realloc(l->files, sizeof *l->files * (l->cf = l->cf ? l->cf * 2 : 256));
        l->files[l->nf++] = d->files[i];
    }
    for (int i = 0; i < d->ndirs; i++) {
        if (l->nd == l->cd) l->dirs = realloc(l->dirs, sizeof *l->dirs * (l->cd = l->cd ? l->cd * 2 : 64));
        l->dirs[l->nd++] = d->dirs[i];
    }
    for (int i = 0; i < d->ndirs; i++) flatten(d->dirs[i], l);
}

static uint32_t name_hash(uint32_t parent, const char *name) {
    uint32_t h = parent ^ 123456789u;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) h = ((h >> 5) | (h << 27)) ^ *p;
    return h;
}
static uint32_t bucket_count(uint32_t n) {
    if (n < 3) return 3;
    if (n < 19) return n | 1;
    uint32_t c = n;
    while (c % 2 == 0 || c % 3 == 0 || c % 5 == 0 || c % 7 == 0 || c % 11 == 0 || c % 13 == 0 || c % 17 == 0) c++;
    return c;
}
static uint32_t align4(uint32_t x) { return (x + 3) & ~3u; }
static uint64_t align16(uint64_t x) { return (x + 15) & ~15ull; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

// ------------------------------------------------------------------ the image
// One image: the game's (romfs_*), or a DLC's (romfs_image_*: services/dlc.c)
struct RomfsImage {
    uint8_t hdr[DATA_START];
    uint8_t *tables;          // dir hash, dir meta, file hash, file meta (contiguous)
    uint64_t tables_off, tables_size, size;
    File **files;             // by data offset
    int nfiles;
};
static RomfsImage g_main;

static char *rel_path(File *f) {
    char *parts[40];
    int n = 0;
    for (Dir *d = f->parent; d && d->parent && n < 40; d = d->parent) parts[n++] = d->name;
    size_t len = strlen(f->name) + 1;
    for (int i = 0; i < n; i++) len += strlen(parts[i]) + 1;
    char *s = malloc(len);
    s[0] = 0;
    for (int i = n - 1; i >= 0; i--) { strcat(s, parts[i]); strcat(s, "/"); }
    strcat(s, f->name);
    return s;
}

static double g_short_gb;  // (the game's own files, when they look cut short: their size, and the empty ones)
static int g_short_empty;
static bool g_short_size;  // (short by their size, its movies left out)

// (build) the game's own files, before a mod is merged: their bytes, and those of its movies (nativeNX/movie) apart
static void own_bytes(const Dir *d, int depth, bool movie, uint64_t *all, uint64_t *movies) {
    for (int i = 0; i < d->nfiles; i++) {
        *all += d->files[i]->size;
        if (movie) *movies += d->files[i]->size;
    }
    for (int i = 0; i < d->ndirs; i++)
        own_bytes(d->dirs[i], depth + 1, movie || (depth == 1 && !strcasecmp(d->dirs[i]->name, "movie")), all, movies);
}

// (a mod's files, before it is merged) all of them and its textures (.tex: the game's own format, the one texture packs
// replace; 1.0's S26): how many and their bytes, for the log. A texture pack's memory is the player's call: drawn, each
// texture takes GPU memory as its own format does (BC on a Mali decoded on the CPU to 4-8 times its size, textures.c)
static void mod_bytes(const Dir *d, int *files, uint64_t *bytes, int *tex, uint64_t *tex_bytes) {
    for (int i = 0; i < d->nfiles; i++) {
        const char *n = d->files[i]->name;
        size_t len = strlen(n);
        (*files)++, *bytes += d->files[i]->size;
        if (len > 4 && !strcasecmp(n + len - 4, ".tex")) (*tex)++, *tex_bytes += d->files[i]->size;
    }
    for (int i = 0; i < d->ndirs; i++) mod_bytes(d->dirs[i], files, bytes, tex, tex_bytes);
}

// ---- main.c's count of the game's files at the start (romfs_prescan): that scan's tree is the one build() takes, so
// the folder is listed once, not twice (up to 1.0.1 the count listed every folder, and the game's first fsp-srv call
// listed them all again: a Moto g06 spent 8.5 s on the one and 7.5 on the other). 1.0.2: a desktop too (it counted
// with plat_count_files, then listed again). listed: the tree is the list kept at a start before (below), whose
// listing took list_ms then; its folders checked in ms.
static struct { char *dir; Dir *root; double ms, list_ms; int unread, checked; bool listed; } g_pre;

static int count_tree(const Dir *d) {
    int n = d->nfiles;
    for (int i = 0; i < d->ndirs; i++) n += count_tree(d->dirs[i]);
    return n;
}

static void free_tree(Dir *d) {
    if (!d) return;
    for (int i = 0; i < d->nfiles; i++) free(d->files[i]->name), free(d->files[i]->path), free(d->files[i]);
    for (int i = 0; i < d->ndirs; i++) free_tree(d->dirs[i]);
    free(d->name), free(d->dirs), free(d->files), free(d);
}

// ---- The game's file list kept between starts (1.0.2). On a phone's SD card each folder listed and each file's size
// asked go through Android's FUSE daemon to a slow card: a Galaxy A15 took 248 s (166 s another time) to list the
// 35,531 files, the screen black all along, where its internal storage takes about 1.4 s. A listing that took
// LIST_SLOW_MS or more, with nothing it couldn't read, is kept (cache/romfs_list.bin, in the app's own folder) and the
// next start takes it instead, once the folders whose time of change it has (LIST_DEPTH levels: the game's 79) are
// found unchanged. A file added, removed or renamed in one of them changes it; a file copied over one of the same name,
// or one deeper down, may not (copied from a PC, a card's folders often keep their time), so the list is also dropped,
// and the next start lists the folder again:
//  - when the game first opens a file whose size isn't the list's (an fstat of the open file; the player is told to
//    start again: romfs_list_changed, overlay.cpp);
//  - when the size check against version 1.4.0's (verify.c) finds a file cut short in a start that took the list: a
//    copy repaired over the old one is listed anew, and the check is never left with a list's sizes for long;
//  - when the player runs Verify game files.
// MHGU_ROMFS_LIST=0: neither taken nor kept; =2: kept however fast the listing was (a test).
#define LIST_FILE "cache/romfs_list.bin"
#define LIST_MAGIC "MHGU romfs list1"  // (16 bytes)
#define LIST_SLOW_MS 5000
static int list_mode(void) {
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("MHGU_ROMFS_LIST");
        m = !e ? 1 : atoi(e) <= 0 ? 0 : atoi(e) >= 2 ? 2 : 1;
        if (e) LOG("[romfs] MHGU_ROMFS_LIST=%s: the list kept between starts %s\n", e,
                   m == 0 ? "neither taken nor kept" : m == 2 ? "kept however fast the listing was" : "as usual");
    }
    return m;
}
static bool g_from_list;  // (this start's tree is the kept list)
static double g_listed_ms;  // (this start listed the folder: how long that took)
static _Atomic int g_list_changed;  // (a file's size isn't the list's: 1 being told, 2 g_list_changed_file set)
static char g_list_changed_file[200];

typedef struct { uint8_t *p; size_t n, cap; bool bad; } LBuf;
static void lput(LBuf *b, const void *v, size_t n) {
    if (b->bad) return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 1 << 20;
        while (c < b->n + n) c *= 2;
        uint8_t *q = realloc(b->p, c);
        if (!q) { b->bad = true; return; }
        b->p = q, b->cap = c;
    }
    memcpy(b->p + b->n, v, n), b->n += n;
}
static void lput_str(LBuf *b, const char *s) {
    size_t n = strlen(s);
    if (n > 0xFFFF) { b->bad = true; return; }
    uint16_t n16 = (uint16_t)n;
    lput(b, &n16, 2), lput(b, s, n);
}
// a folder: its name, time of change (if kept), files (name, size), then its folders the same way
static void lput_dir(LBuf *b, const Dir *d) {
    uint8_t mt = d->mt;
    uint32_t nf = (uint32_t)d->nfiles, nd = (uint32_t)d->ndirs;
    lput_str(b, d->name);
    lput(b, &mt, 1), lput(b, &d->mt_s, 8), lput(b, &d->mt_ns, 8), lput(b, &nf, 4), lput(b, &nd, 4);
    for (int i = 0; i < d->nfiles; i++) lput_str(b, d->files[i]->name), lput(b, &d->files[i]->size, 8);
    for (int i = 0; i < d->ndirs; i++) lput_dir(b, d->dirs[i]);
}

static void list_save(const Dir *root, const char *base_dir, double ms) {
    LBuf b = {0};
    lput(&b, LIST_MAGIC, 16), lput_str(&b, base_dir), lput(&b, &ms, 8);
    lput_dir(&b, root);
    lput(&b, "END.", 4);
    mkdir("cache", 0755);
    FILE *f = b.bad ? NULL : fopen(LIST_FILE ".tmp", "wb");
    bool ok = f && fwrite(b.p, 1, b.n, f) == b.n;
    if (f && fclose(f)) ok = false;
#ifdef _WIN32
    if (ok) remove(LIST_FILE);  // (rename doesn't replace a file there)
#endif
    ok = ok && rename(LIST_FILE ".tmp", LIST_FILE) == 0;
    if (!ok) remove(LIST_FILE ".tmp");
    LOG("[romfs] listing the game folder took %.0f ms: %s (%.1f MB), for the next starts\n", ms,
        ok ? "its list is kept in " LIST_FILE : "its list could NOT be kept in " LIST_FILE, b.n / 1048576.0);
    free(b.p);
}

typedef struct { const uint8_t *p, *end; bool bad; } LRd;
static bool lget(LRd *r, void *v, size_t n) {
    if (r->bad || (size_t)(r->end - r->p) < n) return r->bad = true, false;
    memcpy(v, r->p, n), r->p += n;
    return true;
}
static char *lget_str(LRd *r) {
    uint16_t n;
    if (!lget(r, &n, 2) || (size_t)(r->end - r->p) < n) return r->bad = true, NULL;
    char *s = malloc((size_t)n + 1);
    memcpy(s, r->p, n), s[n] = 0, r->p += n;
    return s;
}
typedef struct { char **path; const Dir **dir; int n, cap; } LChecks;
// (path: the folder's host path, base_dir for the top one)
static Dir *lget_dir(LRd *r, const char *parent, int depth, LChecks *ck) {
    Dir *d = calloc(1, sizeof *d);
    uint8_t mt = 0;
    uint32_t nf = 0, nd = 0;
    d->name = lget_str(r);
    lget(r, &mt, 1), lget(r, &d->mt_s, 8), lget(r, &d->mt_ns, 8), lget(r, &nf, 4), lget(r, &nd, 4);
    if (r->bad || !d->name || depth > 32 || nf > 1000000 || nd > 100000) {
        r->bad = true;
        return d;
    }
    char *path = depth ? join(parent, d->name) : strdup(parent);
    d->mt = mt;
    for (uint32_t i = 0; i < nf && !r->bad; i++) {
        File *f = calloc(1, sizeof *f);
        f->name = lget_str(r);
        lget(r, &f->size, 8);
        f->path = f->name ? join(path, f->name) : NULL;
        f->listed = true;
        add_file(d, f);
    }
    for (uint32_t i = 0; i < nd && !r->bad; i++) add_dir(d, lget_dir(r, path, depth + 1, ck));
    if (d->mt && !r->bad) {
        if (ck->n == ck->cap) ck->path = realloc(ck->path, sizeof *ck->path * (ck->cap = ck->cap ? ck->cap * 2 : 128)),
                              ck->dir = realloc(ck->dir, sizeof *ck->dir * ck->cap);
        ck->path[ck->n] = path, ck->dir[ck->n++] = d;
    } else {
        free(path);
    }
    return d;
}

// The kept folders' times of change against the folder's now, by up to 8 threads (each one a lookup through FUSE)
typedef struct { const LChecks *ck; _Atomic int next, changed; } LCheckRun;
static void *list_check_worker(void *arg) {
    LCheckRun *c = arg;
    for (int i; !atomic_load(&c->changed) && (i = atomic_fetch_add(&c->next, 1)) < c->ck->n;) {
        Dir now = {0};
        dir_mtime(&now, -1, c->ck->path[i]);
        if (!now.mt || now.mt_s != c->ck->dir[i]->mt_s || now.mt_ns != c->ck->dir[i]->mt_ns) {
            int none = 0;
            atomic_compare_exchange_strong(&c->changed, &none, i + 1);
        }
    }
    return NULL;
}

// The list kept for base_dir, its folders unchanged: its tree (each file's path and size); else NULL
static Dir *list_load(const char *base_dir, double *list_ms, int *checked) {
    FILE *f = fopen(LIST_FILE, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (n > 0 && n < (64l << 20) && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n)) &&
        fread(buf, 1, (size_t)n, f) != (size_t)n)
        free(buf), buf = NULL;
    fclose(f);
    if (!buf) return NULL;
    LRd r = {buf, buf + n, false};
    char magic[16], end[4];
    LChecks ck = {0};
    Dir *root = NULL;
    char *dir = NULL;
    const char *why = NULL;
    if (!lget(&r, magic, 16) || memcmp(magic, LIST_MAGIC, 16)) why = "another version's";
    else if (!(dir = lget_str(&r)) || strcmp(dir, base_dir)) why = "another folder's";
    else if (!lget(&r, list_ms, 8) || !(root = lget_dir(&r, base_dir, 0, &ck)) || r.bad || !lget(&r, end, 4) ||
             memcmp(end, "END.", 4) || r.p != r.end)
        why = "not whole";
    free(dir), free(buf);
    if (!why) {
        LCheckRun run = {&ck, 0, 0};
        pthread_t th[7];
        int nt = 0;
        while (nt < 7 && nt < ck.n / 8 && pthread_create(&th[nt], NULL, list_check_worker, &run) == 0) nt++;
        list_check_worker(&run);
        for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
        int c = atomic_load(&run.changed);
        if (c) {
            LOG("[romfs] the game folder changed since its list was kept (%s): it is listed again\n", ck.path[c - 1]);
            why = "";
        }
        *checked = ck.n;
    } else {
        LOG("[romfs] the list of the game's files kept at a start before is %s: the folder is listed again\n", why);
    }
    for (int i = 0; i < ck.n; i++) free(ck.path[i]);
    free(ck.path), free(ck.dir);
    if (why) {
        free_tree(root);
        remove(LIST_FILE);
        return NULL;
    }
    return root;
}

// (romfs_read's first open of a file from the kept list) its size now against the list's
static void list_check_size(File *f, int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0 || (uint64_t)st.st_size == f->size) return;
    LOG("[romfs] WARNING: %s has %lld bytes, the list kept at a start before %llu: the list is dropped, the next start "
        "lists the folder again\n", f->rpath, (long long)st.st_size, (unsigned long long)f->size);
    if (atomic_exchange(&g_list_changed, 1)) return;
    snprintf(g_list_changed_file, sizeof g_list_changed_file, "%s", f->rpath);
    remove(LIST_FILE);
    atomic_store(&g_list_changed, 2);
}

void romfs_list_forget(bool only_if_taken, const char *why) {
    if (only_if_taken && !g_from_list) return;
    if (remove(LIST_FILE) == 0)
        LOG("[romfs] the list of the game's files kept between starts is dropped (%s): the next start lists the folder "
            "again\n", why);
}

double romfs_list_slow_ms(void) { return g_listed_ms; }

bool romfs_list_changed(char *file, size_t size) {
    if (atomic_load(&g_list_changed) != 2) return false;
    if (file && size) snprintf(file, size, "%s", g_list_changed_file);
    return true;
}

int romfs_prescan(const char *base_dir) {
#ifndef _WIN32
    if (scan_threads() <= 1) return plat_count_files(base_dir);  // (MHGU_PERF_ROMFS=0: counted now, scanned again later)
#endif
    int u0 = g_unread, checked = 0;
    double list_ms = 0;
    uint64_t t0 = plat_now_ns();
    Dir *root = list_mode() ? list_load(base_dir, &list_ms, &checked) : NULL;
    bool listed = root != NULL;
    if (!root) root = scan(base_dir, "", 0);
    if (!root) return 0;
    g_pre.ms = (plat_now_ns() - t0) / 1e6;
    g_pre.dir = strdup(base_dir), g_pre.root = root, g_pre.unread = g_unread - u0;
    g_pre.listed = listed, g_pre.list_ms = list_ms, g_pre.checked = checked;
    if (!listed && list_mode() && !g_pre.unread && (g_pre.ms >= LIST_SLOW_MS || list_mode() == 2))
        list_save(root, base_dir, g_pre.ms);
    else if (!listed)
        remove(LIST_FILE);  // (a folder listed fast, or not whole: no list; one kept for another folder goes)
    return count_tree(root);
}

#ifndef _WIN32
// (MHGU_ROMFS_CHECK=1, a test) the tree the threads gave against 1.0.1's one-thread walk of the same folder, before any
// mod is merged: every folder by its path and every file by its host path and size, both sorted, compared line by line
typedef struct { char **v; int n, cap; } Lines;
static void tree_lines(const Dir *d, const char *at, Lines *l) {
    for (int i = 0; i < d->nfiles + d->ndirs; i++) {
        size_t n = 1300;
        char *s = malloc(n);
        if (i < d->nfiles) snprintf(s, n, "%s\t%llu", d->files[i]->path, (unsigned long long)d->files[i]->size);
        else snprintf(s, n, "%s%s/", at, d->dirs[i - d->nfiles]->name);
        if (l->n == l->cap) l->v = realloc(l->v, sizeof *l->v * (l->cap = l->cap ? l->cap * 2 : 4096));
        l->v[l->n++] = s;
        if (i >= d->nfiles) tree_lines(d->dirs[i - d->nfiles], s, l);
    }
}
static int by_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void romfs_check(const Dir *root, const char *base_dir) {
    g_scan_one = true;
    uint64_t t0 = plat_now_ns();
    Dir *one = scan(base_dir, "", 0);
    double ms = (plat_now_ns() - t0) / 1e6;
    g_scan_one = false;
    Lines a = {0}, b = {0};
    tree_lines(root, "", &a);
    if (one) tree_lines(one, "", &b);
    qsort(a.v, a.n, sizeof *a.v, by_str), qsort(b.v, b.n, sizeof *b.v, by_str);
    int differ = a.n != b.n;
    for (int i = 0; i < a.n && i < b.n; i++)
        if (strcmp(a.v[i], b.v[i]) && differ++ < 3) LOG("[romfs] check: %s | %s\n", a.v[i], b.v[i]);
    LOG("[romfs] check: the threads' tree %s one thread's (%d and %d entries; one thread listed it in %.0f ms)\n",
        differ ? "DIFFERS from" : "is the same as", a.n, b.n, ms);
    for (int i = 0; i < a.n; i++) free(a.v[i]);
    for (int i = 0; i < b.n; i++) free(b.v[i]);
    free(a.v), free(b.v);  // (the one-thread tree itself is left: a test)
}
#endif

static bool build(RomfsImage *im, const char *base_dir, const char *const *overlays, int noverlays, bool game) {
    int unread0 = g_unread;
    uint64_t t0 = plat_now_ns();
    Dir *root;
    double scan_ms;
    const bool pre = game && g_pre.root && !strcmp(g_pre.dir, base_dir);  // (scanned at the start: romfs_prescan)
    if (pre) root = g_pre.root, scan_ms = g_pre.ms, unread0 -= g_pre.unread, g_pre.root = NULL;
    else root = scan(base_dir, "", 0), scan_ms = (plat_now_ns() - t0) / 1e6;
    if (!root) { LOG("[romfs] cannot read %s\n", base_dir); return false; }
    uint64_t own = 0, movies = 0;
#ifndef _WIN32
    if (game && getenv("MHGU_ROMFS_CHECK")) romfs_check(root, base_dir);  // (a test: the threads' tree against one thread's)
#endif
    if (game) own_bytes(root, 0, false, &own, &movies);
    int unread_own = g_unread - unread0;
    for (int i = 0; i < noverlays; i++) {
        Dir *o = scan(overlays[i], "", 0);
        if (!o) continue;
        int files = 0, tex = 0;
        uint64_t bytes = 0, tex_bytes = 0;
        mod_bytes(o, &files, &bytes, &tex, &tex_bytes);
        int n = merge(root, o);
        LOG("[romfs] mod %s: %d files (%.1f MB)", overlays[i], n, bytes / 1048576.0);
        if (tex) LOG(", %d of them textures (%.1f MB as files; the GPU memory they take grows as they are drawn: the [mem] lines)",
                     tex, tex_bytes / 1048576.0);
        LOG("\n");
    }
    sort_tree(root);
    Lists l = {0};
    l.dirs = malloc(sizeof *l.dirs * (l.cd = 64));
    l.dirs[l.nd++] = root;
    flatten(root, &l);

    uint32_t dsize = 0, fsize = 0;
    for (int i = 0; i < l.nd; i++) l.dirs[i]->off = dsize, dsize += 0x18 + align4((uint32_t)strlen(l.dirs[i]->name));
    for (int i = 0; i < l.nf; i++) l.files[i]->off = fsize, fsize += 0x20 + align4((uint32_t)strlen(l.files[i]->name));
    uint64_t data = 0;
    for (int i = 0; i < l.nf; i++) {
        data = align16(data);
        l.files[i]->data_off = data;
        l.files[i]->rpath = rel_path(l.files[i]);
        data += l.files[i]->size;
    }
    uint32_t nbd = bucket_count((uint32_t)l.nd), nbf = bucket_count((uint32_t)l.nf);
    uint64_t dhash_off = (DATA_START + data + 3) & ~3ull;
    uint64_t dmeta_off = dhash_off + 4ull * nbd, fhash_off = dmeta_off + dsize, fmeta_off = fhash_off + 4ull * nbf;
    im->tables_off = dhash_off;
    im->tables_size = fmeta_off + fsize - dhash_off;
    im->tables = calloc(1, im->tables_size);
    uint32_t *dbuckets = (uint32_t *)im->tables, *fbuckets = (uint32_t *)(im->tables + (fhash_off - dhash_off));
    uint8_t *dtab = im->tables + (dmeta_off - dhash_off), *ftab = im->tables + (fmeta_off - dhash_off);
    for (uint32_t i = 0; i < nbd; i++) dbuckets[i] = EMPTY;
    for (uint32_t i = 0; i < nbf; i++) fbuckets[i] = EMPTY;
    for (int i = 0; i < l.nd; i++) {
        Dir *d = l.dirs[i];
        uint8_t *e = dtab + d->off;
        uint32_t parent = d->parent ? d->parent->off : 0, nl = (uint32_t)strlen(d->name), sib = EMPTY;
        if (d->parent)
            for (int k = 0; k + 1 < d->parent->ndirs; k++)
                if (d->parent->dirs[k] == d) sib = d->parent->dirs[k + 1]->off;
        uint32_t h = name_hash(parent, d->name) % nbd;
        put32(e, parent);
        put32(e + 4, sib);
        put32(e + 8, d->ndirs ? d->dirs[0]->off : EMPTY);
        put32(e + 12, d->nfiles ? d->files[0]->off : EMPTY);
        put32(e + 16, dbuckets[h]);
        put32(e + 20, nl);
        memcpy(e + 0x18, d->name, nl);
        dbuckets[h] = d->off;
    }
    for (int i = 0; i < l.nf; i++) {
        File *f = l.files[i];
        uint8_t *e = ftab + f->off;
        uint32_t nl = (uint32_t)strlen(f->name), sib = EMPTY;
        for (int k = 0; k + 1 < f->parent->nfiles; k++)
            if (f->parent->files[k] == f) sib = f->parent->files[k + 1]->off;
        uint32_t h = name_hash(f->parent->off, f->name) % nbf;
        put32(e, f->parent->off);
        put32(e + 4, sib);
        put64(e + 8, f->data_off);
        put64(e + 16, f->size);
        put32(e + 24, fbuckets[h]);
        put32(e + 28, nl);
        memcpy(e + 0x20, f->name, nl);
        fbuckets[h] = f->off;
    }
    uint64_t hv[10] = {0x50, dhash_off, 4ull * nbd, dmeta_off, dsize, fhash_off, 4ull * nbf, fmeta_off, fsize, DATA_START};
    memcpy(im->hdr, hv, sizeof hv);
    im->size = fmeta_off + fsize;
    im->files = l.files;  // (flatten order is data order)
    im->nfiles = l.nf;
    free(l.dirs);
    if (pre && g_pre.listed)
        LOG("[romfs] %d files, %d folders, image %.1f GB (the list kept at a start before, its %d folders checked: %.0f ms; "
            "listing the folder took %.0f ms then)\n", l.nf, l.nd, im->size / 1e9, g_pre.checked, scan_ms, g_pre.list_ms);
    else
#ifdef _WIN32
        LOG("[romfs] %d files, %d folders, image %.1f GB (its folder listed in %.0f ms%s)\n", l.nf, l.nd, im->size / 1e9,
            scan_ms, pre ? ", as the game started" : "");
#else
        LOG("[romfs] %d files, %d folders, image %.1f GB (its folder listed in %.0f ms by %d thread%s%s)\n", l.nf, l.nd,
            im->size / 1e9, scan_ms, scan_threads(), scan_threads() > 1 ? "s" : "", pre ? ", as the game started" : "");
#endif
    if (game) g_from_list = pre && g_pre.listed, g_listed_ms = g_from_list ? 0 : scan_ms;
    if (!game) return true;
    // (version 1.4.0's is 12.3 GB, 1.4 of them its movies, and none of its files is empty. A copy cut short, with every
    // file there but some of them empty or short, starts and then fails in ways that look like the port's: a black
    // picture, a call to nothing, the game closing in one place (#106). The player is told once the game shows:
    // romfs_incomplete, overlay.cpp. Measured on the game's own folder, before the mods (a mod's files, smaller or larger,
    // moved the image's size either way), and without its movies: copies with them encoded again, smaller, play right
    // (a romfs of 11.0 GB, #171).)
    int empty = 0;
    size_t bl = strlen(base_dir);
    for (int i = 0; i < l.nf; i++)
        if (!l.files[i]->size && !strncmp(l.files[i]->path, base_dir, bl) && l.files[i]->path[bl] == '/' &&
            empty++ < 3)  // (a mod's may be empty)
            LOG("[romfs] WARNING: empty file %s\n", l.files[i]->rpath);
    bool short_own = own - movies < 10650000000ull;  // (the game's 10.92 GB but its movies, 2.5 % short)
    LOG("[romfs] the game's own files: %.2f GB, %.2f of them its movies%s\n", own / 1e9, movies / 1e9,
        movies < 1350000000ull && !short_own ? " (smaller than the game's 1.39: encoded again, they play)" : "");
    if (short_own) LOG("[romfs] WARNING: smaller than the game's 12.3 GB: some files are probably incomplete\n");
    if (empty) LOG("[romfs] WARNING: %d empty file(s): the copy is incomplete\n", empty);
    if (unread_own) LOG("[romfs] WARNING: %d file(s) or folder(s) of the game's could not be read\n", unread_own);
    if (short_own || empty) g_short_gb = own / 1e9, g_short_empty = empty, g_short_size = short_own;
    {  // (each of them against version 1.4.0's size, on a thread of its own: verify.c verify_quick_start)
        const char **rel = malloc(sizeof *rel * (l.nf ? l.nf : 1)), **host = malloc(sizeof *host * (l.nf ? l.nf : 1));
        uint64_t *sz = malloc(sizeof *sz * (l.nf ? l.nf : 1));
        int n = 0;
        for (int i = 0; rel && host && sz && i < l.nf; i++)
            if (!strncmp(l.files[i]->path, base_dir, bl) && l.files[i]->path[bl] == '/')  // (the game's own, not a mod's)
                rel[n] = l.files[i]->rpath, host[n] = l.files[i]->path, sz[n] = l.files[i]->size, n++;
        const char *nq = getenv("MHGU_ROMFS_NOQUICK");  // (=1: not checked)
        if (rel && host && sz && !(nq && atoi(nq) > 0)) verify_quick_start(rel, host, sz, n);
        free(rel), free(host), free(sz);
    }
    const char *t = getenv("MHGU_ROMFS_SHORT");  // (test: the notice as if the copy were that many GB, e.g. 11.5)
    if (t && atof(t) > 0) g_short_gb = atof(t), g_short_size = true;
    return true;
}

bool romfs_incomplete(double *gb, int *empty) {
    if (gb) *gb = g_short_gb;
    if (empty) *empty = g_short_size ? 0 : g_short_empty;  // (short by size: the size is what the notice tells)
    return g_short_gb > 0;
}

bool romfs_open(const char *base_dir, const char *const *overlays, int noverlays) {
    return build(&g_main, base_dir, overlays, noverlays, true);
}

RomfsImage *romfs_image_open(const char *dir) {
    RomfsImage *im = calloc(1, sizeof *im);
    if (!build(im, dir, NULL, 0, false)) { free(im); return NULL; }
    return im;
}

uint64_t romfs_size(void) { return g_main.size; }
uint64_t romfs_image_size(const RomfsImage *im) { return im->size; }

// ---- host file descriptors (a small cache: the image has tens of thousands of files)
#define FDC 64
static struct { File *f; int fd; uint64_t used; } fdc[FDC];
static uint64_t fdc_clock;
static pthread_mutex_t fdc_lock = PTHREAD_MUTEX_INITIALIZER;

static void fdc_take(void) {
    if (HITCH_ON) hitch_mutex_lock(&fdc_lock, HC_FILE_LOCK);  // ((debug) MHGU_HITCHLOG: the waits, opens and reads timed)
    else pthread_mutex_lock(&fdc_lock);
}
static int fdc_find(File *f, int *oldest) {  // (fdc_lock held) f's slot, or -1 and the least used one
    int slot = -1, lru = 0;
    for (int i = 0; i < FDC; i++) {
        if (fdc[i].f == f) { slot = i; break; }
        if (fdc[i].used < fdc[lru].used) lru = i;
    }
    if (oldest) *oldest = lru;
    return slot;
}

static int64_t read_file(File *f, uint8_t *dst, uint64_t off, uint64_t n) {
    fdc_take();
    int oldest, slot = fdc_find(f, NULL), fd;
    if (slot >= 0) {
        fdc[slot].used = ++fdc_clock;
        fd = fdc[slot].fd;
        pthread_mutex_unlock(&fdc_lock);
    } else {
        // (opened with the lock let go: on a hard disk an open takes 20-30 ms, and every other file read - another game
        // thread's, the music's - waited that long for it; MHGU_HITCHLOG saw one wait 61 ms)
        pthread_mutex_unlock(&fdc_lock);
        uint64_t t0 = HITCH_ON ? plat_now_ns() : 0;
        int mine = open(f->path, O_RDONLY | O_BINARY);
        if (mine < 0) LOG("[romfs] cannot open %s\n", f->path);
        else if (f->listed && atomic_exchange(&f->listed, false)) list_check_size(f, mine);  // (once a file)
        if (HITCH_ON) hitch_add_label(HC_FILE_OPEN, plat_now_ns() - t0, 0, f->rpath, -1);
        fdc_take();
        slot = fdc_find(f, &oldest);
        if (slot >= 0) {  // (another thread opened it meanwhile: its descriptor, ours closed)
            fdc[slot].used = ++fdc_clock;
            fd = fdc[slot].fd;
            pthread_mutex_unlock(&fdc_lock);
            if (mine >= 0) close(mine);
        } else {
            slot = oldest;
            if (fdc[slot].f && fdc[slot].fd >= 0) close(fdc[slot].fd);
            fdc[slot].f = f, fdc[slot].fd = mine, fdc[slot].used = ++fdc_clock;
            fd = mine;
            pthread_mutex_unlock(&fdc_lock);  // (pread doesn't move the file position: safe while others use it)
        }
    }
    if (fd < 0) return -1;
    if (!HITCH_ON) return plat_pread(fd, dst, n, (off_t)off);  // (dst: an IPC buffer in guest memory, platform.h)
    uint64_t t0 = plat_now_ns();
    int64_t r = plat_pread(fd, dst, n, (off_t)off);
    hitch_add_label(HC_FILE_READ, plat_now_ns() - t0, n, f->rpath, -1);
    return r;
}

static int file_at(const RomfsImage *im, uint64_t rel) {  // index of the file whose padded data range holds rel, or -1
    int lo = 0, hi = im->nfiles - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (im->files[mid]->data_off <= rel) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

const char *romfs_file_at(uint64_t off) {
    if (off < DATA_START) return NULL;
    int i = file_at(&g_main, off - DATA_START);
    return i >= 0 && off - DATA_START < g_main.files[i]->data_off + g_main.files[i]->size ? g_main.files[i]->rpath : NULL;
}

const char *romfs_host_path(const char *rpath) {
    while (*rpath == '/') rpath++;
    for (int i = 0; i < g_main.nfiles; i++)  // (rare: movies)
        if (!strcasecmp(g_main.files[i]->rpath, rpath)) return g_main.files[i]->path;
    return NULL;
}

uint64_t romfs_image_read(const RomfsImage *im, uint8_t *dst, uint64_t off, uint64_t size) {
    if (off >= im->size) return 0;
    if (off + size > im->size) size = im->size - off;
    uint64_t done = 0;
    while (done < size) {
        uint64_t o = off + done, n = size - done;
        if (o < DATA_START) {  // header
            if (n > DATA_START - o) n = DATA_START - o;
            memcpy(dst + done, im->hdr + o, n);
        } else if (o >= im->tables_off) {  // tables
            memcpy(dst + done, im->tables + (o - im->tables_off), n);
        } else {  // file data (and the padding between files)
            if (o + n > im->tables_off) n = im->tables_off - o;
            uint64_t rel = o - DATA_START;
            int i = file_at(im, rel);
            File *f = i >= 0 ? im->files[i] : NULL;
            if (f && rel < f->data_off + f->size) {
                uint64_t in = rel - f->data_off, avail = f->size - in;
                if (n > avail) n = avail;
                int64_t r = read_file(f, dst + done, in, n);
                if (r < 0) r = 0;
                if ((uint64_t)r < n) memset(dst + done + r, 0, n - r);
            } else {  // padding up to the next file
                uint64_t next = i + 1 < im->nfiles ? im->files[i + 1]->data_off : im->tables_off - DATA_START;
                if (n > next - rel) n = next - rel;
                memset(dst + done, 0, n);
            }
        }
        done += n;
    }
    return done;
}

uint64_t romfs_read(uint8_t *dst, uint64_t off, uint64_t size) { return romfs_image_read(&g_main, dst, off, size); }
