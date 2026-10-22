/*
 * Texture packs: replacement textures in the naming of PCSX2's texture replacement, so packs made for the game
 * under that emulator work as they are.
 *
 * A file is named <texture hash>-<palette hash>-<bits>.dds (or .png) (two parts, without the palette hash, for a texture
 * without a palette), the hashes in hexadecimal without leading zeros:
 *   texture hash   XXH3 (64 bits) over the texture's raw 256-byte blocks of GS memory, block after block in
 *                  row-major block order over the texture's rectangle: the data as it lies in GS memory
 *   palette hash   XXH3 over the palette's 32-bit entries in index order, from the lowest to the highest index
 *                  the texture uses (some files: over the whole palette; both are looked up)
 *   bits           PSM | log2(width) << 6 | log2(height) << 10 | TCC << 14
 * Found by matching a pack against the textures the game uploads (docs/port/README.md). The files are looked for
 * at any depth below the folder `textures` next to the program (or the one BT3_TEXTURES names). A pack's alpha
 * is the GS's own (0x80 = opaque), like the textures it replaces.
 *
 * Read: DDS with DXT1 / DXT3 / DXT5 data or plain 32-bit pixels, with its smaller copies (mip levels), and PNG
 * (the smaller copies are made here). Where a name exists as both, the first one found is used.
 *
 * The files are read by a thread of their own (TexPack_Get), the way PCSX2 loads them "asynchronously": a file read
 * inside a frame held the picture up for as long as the disk took, 30 to 150 ms when an ultimate's effects first
 * appear, and on a machine whose memory does not keep the pack's files cached every time the game uploads one of
 * its textures again (hundreds of times a fight). Until a file is in, the renderer draws the original texture. What
 * was read stays in memory (TEXPACK_BUDGET, the least recently used files go first). BT3_PACK_SYNC=1: each file is
 * read at once, inside the frame, as before (the same picture in every run: tests).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BT3_UWP
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <SDL3/SDL.h>
#define XXH_INLINE_ALL
#include "../../third_party/xxhash/xxhash.h"
#include "gs_internal.h"
#include "gs_texpack.h"

typedef struct Entry {
    uint64_t tex, clut;
    uint32_t bits;
    int pal;
    char *path;
    int state;        /* 0 not in memory, 1 waiting for the reader, 2 in memory (img), 3 unreadable */
    unsigned used;    /* when TexPack_Get last asked for it */
    size_t held;      /* the bytes img holds */
    TexPackImage img;
} Entry;

static Entry *sEntries;
static uint32_t sCount, sCap, *sBucket, sBuckets;

#ifdef BT3_UWP
#define TEXPACK_FILE_LIMIT (64u * 1024u * 1024u)
#define TEXPACK_DIM_LIMIT 4096u
#define TEXPACK_ENTRY_LIMIT 100000u
#endif
#define TEXPACK_BUDGET ((size_t)1024 << 20) /* the files kept in memory: one session's two fights took 289 MB */
#define QUEUE 4096                          /* files waiting for the reader (a power of two) */
static SDL_Mutex *sLock;
static SDL_Condition *sWake;
static SDL_Thread *sReader;
static uint32_t sQueue[QUEUE], sHead, sTail; /* the reader takes from sTail */
static uint32_t *sHeld, sHeldCount;          /* the entries in memory (state 2) */
static size_t sHeldBytes, sBudget;
static unsigned sTick;
static int sSync = -1;
#ifdef BT3_UWP
static TexPackStats sStats;
#endif

static uint32_t bucket_of(uint64_t tex, uint64_t clut, uint32_t bits) {
    uint64_t h = (tex ^ (clut * 0x9E3779B97F4A7C15ull) ^ bits) * 0x9FB21C651E98DF25ull;
    return (uint32_t)(h >> 40);
}

static int find(uint64_t tex, uint64_t clut, uint32_t bits, int pal) {
    uint32_t h = bucket_of(tex, clut, bits) & (sBuckets - 1);
    while (sBucket[h] != 0) {
        const Entry *e = &sEntries[sBucket[h] - 1];
        if (e->tex == tex && e->clut == clut && e->bits == bits && e->pal == pal) {
            return (int)sBucket[h] - 1;
        }
        h = (h + 1) & (sBuckets - 1);
    }
    return -1;
}

/* "1dd4c76113969303-56e3d4469d2ad392-00005e54.dds" */
static void add(const char *dir, const char *name) {
    size_t n = strlen(name);
    size_t stem_end;
    char part[3][20];
    int parts = 0, len = 0;
    size_t i;
    Entry e;

    if (n < 8 || (SDL_strcasecmp(name + n - 4, ".dds") != 0 && SDL_strcasecmp(name + n - 4, ".png") != 0)) {
        return;
    }
    stem_end = n - 4;
    if (stem_end > 4 && SDL_strncasecmp(name + stem_end - 4, ".jpg", 4) == 0) {
        stem_end -= 4;
    } else if (stem_end > 5 && SDL_strncasecmp(name + stem_end - 5, ".jpeg", 5) == 0) {
        stem_end -= 5;
    }
    for (i = 0; i < stem_end; i++) {
        char c = name[i];
        if (c == '-') {
            if (len == 0 || parts == 2) {
                return;
            }
            part[parts++][len] = '\0';
            len = 0;
        } else if (((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) && len < 16) {
            part[parts][len++] = c;
        } else {
            return; /* not one of these names */
        }
    }
    if (len == 0 || parts == 0) {
        return;
    }
    part[parts++][len] = '\0';
    e.tex = strtoull(part[0], NULL, 16);
    e.pal = parts == 3;
    e.clut = e.pal ? strtoull(part[1], NULL, 16) : 0;
    e.bits = (uint32_t)strtoul(part[parts - 1], NULL, 16);
    e.state = 0;
    e.used = 0;
    e.held = 0;
    memset(&e.img, 0, sizeof(e.img));
    e.path = malloc(strlen(dir) + n + 1);
    if (e.path == NULL) {
        return;
    }
    strcpy(e.path, dir);
    strcat(e.path, name);
#ifdef BT3_UWP
    if (sCount >= TEXPACK_ENTRY_LIMIT) {
        free(e.path);
        return;
    }
#endif
    if (sCount == sCap) {
        uint32_t cap = sCap ? sCap * 2 : 4096;
        Entry *entries = realloc(sEntries, cap * sizeof(Entry));
        if (entries == NULL) {
            free(e.path);
            return;
        }
        sEntries = entries;
        sCap = cap;
    }
    sEntries[sCount++] = e;
}

#ifdef BT3_UWP
/* UWP directory access uses the FromApp dirent shim. */
typedef struct ScanStats {
    unsigned entries, folders, rejected, openError;
} ScanStats;

static void walk_uwp(const char *dir, unsigned depth, ScanStats *stats) {
    DIR *search = opendir(dir);
    struct dirent *item;

    if (search == NULL) {
        if (depth == 0) stats->openError = GetLastError();
        return;
    }
    while ((item = readdir(search)) != NULL) {
        size_t n;
        char *path;

        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        stats->entries++;
        n = strlen(dir) + strlen(item->d_name) + 2;
        path = malloc(n);
        if (path == NULL) {
            break;
        }
        snprintf(path, n, "%s%s", dir, item->d_name);
        if (item->is_dir) {
            stats->folders++;
            if (depth < 16) {
                strcat(path, "/");
                walk_uwp(path, depth + 1, stats);
            }
        } else {
            uint32_t before = sCount;
            add(dir, item->d_name);
            if (sCount == before) stats->rejected++;
        }
        free(path);
    }
    closedir(search);
}
#else
static SDL_EnumerationResult SDLCALL walk(void *userdata, const char *dir, const char *name) {
    size_t n = strlen(dir) + strlen(name) + 2;
    char *path = malloc(n);
    SDL_PathInfo info;

    (void)userdata;
    snprintf(path, n, "%s%s", dir, name);
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        strcat(path, "/");
        SDL_EnumerateDirectory(path, walk, NULL);
    } else {
        add(dir, name);
    }
    free(path);
    return SDL_ENUM_CONTINUE;
}
#endif

void TexPack_Init(void) {
    const char *root = getenv("BT3_TEXTURES") != NULL ? getenv("BT3_TEXTURES") : "textures";
    size_t n = strlen(root);
    char *dir = malloc(n + 2);
    uint32_t i, dup = 0;
#ifdef BT3_UWP
    struct stat info;
    ScanStats stats = {0};
    uint64_t scan_start;
#else
    SDL_PathInfo info;
#endif

#ifdef BT3_UWP
    { extern void PortUwp_Trace(const char *message); PortUwp_Trace("Texture pack: checking folder"); }
#endif
    if (root[0] == '\0' ||
#ifdef BT3_UWP
        stat(root, &info) != 0 || (info.st_mode & S_IFMT) != S_IFDIR
#else
        !SDL_GetPathInfo(root, &info) || info.type != SDL_PATHTYPE_DIRECTORY
#endif
    ) {
#ifdef BT3_UWP
        { extern void PortUwp_Trace(const char *message); PortUwp_Trace("Texture pack: folder unavailable; skipping"); }
#endif
        free(dir);
        return;
    }
    if (dir == NULL) {
        return;
    }
#ifdef BT3_UWP
    { extern void PortUwp_Trace(const char *message); PortUwp_Trace("Texture pack: scanning folder"); }
#endif
    strcpy(dir, root);
    if (dir[n - 1] != '/' && dir[n - 1] != '\\') {
        strcat(dir, "/");
    }
#ifdef BT3_UWP
    scan_start = SDL_GetTicksNS();
    walk_uwp(dir, 0, &stats);
#else
    SDL_EnumerateDirectory(dir, walk, NULL);
#endif
    free(dir);
#ifdef BT3_UWP
    {
        char message[192];
        extern void PortUwp_Trace(const char *message);
        snprintf(message, sizeof(message),
                 "Texture pack: scan complete in %.1f ms (%u files; %u entries, %u folders, %u rejected, open error %u)",
                 (double)(SDL_GetTicksNS() - scan_start) / 1e6,
                 sCount, stats.entries, stats.folders, stats.rejected, stats.openError);
        PortUwp_Trace(message);
    }
#endif
    if (sCount == 0) {
        return;
    }
    for (sBuckets = 1024; sBuckets < sCount * 2; sBuckets *= 2) {
    }
    sBucket = calloc(sBuckets, sizeof(uint32_t));
    if (sBucket == NULL) {
        sCount = 0;
        return;
    }
    for (i = 0; i < sCount; i++) {
        const Entry *e = &sEntries[i];
        uint32_t h = bucket_of(e->tex, e->clut, e->bits) & (sBuckets - 1);
        if (find(e->tex, e->clut, e->bits, e->pal) >= 0) {
            dup++; /* the same name in two folders of the pack: the first one found is used */
            continue;
        }
        while (sBucket[h] != 0) {
            h = (h + 1) & (sBuckets - 1);
        }
        sBucket[h] = i + 1;
    }
    fprintf(stderr, "bt3: texture pack: %u replacement textures in %s (%u more with a name already seen)\n", sCount - dup, root, dup);
}

int TexPack_Count(void) {
    return (int)sCount;
}

const char *TexPack_Path(int id) {
    return id >= 0 && (uint32_t)id < sCount ? sEntries[id].path : NULL;
}

int TexPack_Lookup(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t tcc, uint32_t cbp, uint32_t cpsm, uint32_t *maxAlpha) {
    uint32_t bw, bh, x, y, i, lo = 255, hi = 0, bits, wl = 0, hl = 0;
    XXH3_state_t st;
    uint64_t tex;
    int pal = psm == 0x13 || psm == 0x14;

    *maxAlpha = 255;
    if (sCount == 0) {
        return -1;
    }
    if (psm == 0x13) { bw = 16; bh = 16; }
    else if (psm == 0x14) { bw = 32; bh = 16; }
    else if (psm == 0x00) { bw = 8; bh = 8; }
    else { return -1; }
    if (tw < bw || th < bh) {
        return -1; /* smaller than a block: hashed another way there, which is not worked out */
    }
    XXH3_64bits_reset(&st);
    for (y = 0; y < th; y += bh) {
        for (x = 0; x < tw; x += bw) {
            const uint8_t *p = Gs_BlockPtr(tbp, tbw, psm, x, y);
            XXH3_64bits_update(&st, p, 256);
            if (psm == 0x13) {
                for (i = 0; i < 256; i++) {
                    if (p[i] < lo) { lo = p[i]; }
                    if (p[i] > hi) { hi = p[i]; }
                }
            } else if (psm == 0x14) {
                for (i = 0; i < 256; i++) {
                    uint32_t a = p[i] & 15, b = p[i] >> 4;
                    if (a < lo) { lo = a; }
                    if (b < lo) { lo = b; }
                    if (a > hi) { hi = a; }
                    if (b > hi) { hi = b; }
                }
            }
        }
    }
    tex = XXH3_64bits_digest(&st);
    while ((1u << wl) < tw) { wl++; }
    while ((1u << hl) < th) { hl++; }
    bits = psm | wl << 6 | hl << 10 | (tcc & 1) << 14;
    if (!pal) {
        return find(tex, 0, bits, 0);
    }
    if (cpsm != 0) {
        return -1; /* 16-bit palettes are expanded with TEXA there: not seen in this game */
    }
    {
        uint32_t clut[256], count = psm == 0x13 ? 256 : 16;
        int id;
        for (i = 0; i < count; i++) {
            /* index order: an 8-bit palette is stored with entries 8..15 and 16..23 of every 32 exchanged */
            uint32_t k = psm == 0x13 ? ((i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1)) : i;
            clut[i] = psm == 0x13 ? Gs_VramRead(cbp, 1, cpsm, k & 15, k >> 4) : Gs_VramRead(cbp, 1, cpsm, i & 7, i >> 3);
        }
        /* the largest alpha the ORIGINAL has (over the palette entries it uses): see the header of this function */
        *maxAlpha = 0;
        for (i = lo; i <= hi; i++) {
            if (clut[i] >> 24 > *maxAlpha) {
                *maxAlpha = clut[i] >> 24;
            }
        }
        id = find(tex, XXH3_64bits(&clut[lo], (hi - lo + 1) * 4), bits, 1);
        if (id < 0) {
            id = find(tex, XXH3_64bits(clut, count * 4), bits, 1);
        }
        if (getenv("BT3_TEX_LOG") != NULL) { /* which textures a pack has and which it has not, by the name it would need */
            fprintf(stderr, "texpack: %s %ux%u %u-bit %llx-%llx-%08x%s%s\n", id >= 0 ? "found  " : "missing", tw, th, psm == 0x13 ? 8u : 4u,
                    (unsigned long long)tex, (unsigned long long)XXH3_64bits(&clut[lo], (hi - lo + 1) * 4), bits, id >= 0 ? " " : "",
                    id >= 0 ? sEntries[id].path : "");
        }
        return id;
    }
}

/* A PNG: decoded by SDL, and its smaller copies (mip levels) made here, each the average of 2 x 2 pixels of the one
   before. */
#ifdef BT3_UWP
static uint8_t *load_file_uwp(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;

    *size = 0;
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        (unsigned long)length > TEXPACK_FILE_LIMIT || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = SDL_malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        SDL_free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static uint32_t png_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
#endif

static int load_png(const char *path, TexPackImage *img) {
    SDL_Surface *png, *rgba;
    uint32_t w, h, total = 0, lw, lh, at = 0, x, y, c;
    int level, levels = 0;
    uint8_t *f;

#ifdef BT3_UWP
    size_t size;
    uint8_t *bytes = load_file_uwp(path, &size);
    SDL_IOStream *io;
    if (bytes == NULL) {
        return 0;
    }
    if (size < 24 || memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        png_be32(bytes + 16) == 0 || png_be32(bytes + 20) == 0 ||
        png_be32(bytes + 16) > TEXPACK_DIM_LIMIT || png_be32(bytes + 20) > TEXPACK_DIM_LIMIT) {
        SDL_free(bytes);
        return 0;
    }
    io = SDL_IOFromConstMem(bytes, size);
    png = io != NULL ? SDL_LoadPNG_IO(io, true) : NULL;
    SDL_free(bytes);
#else
    png = SDL_LoadPNG(path);
#endif
    if (png == NULL) {
        return 0;
    }
    rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (rgba == NULL || rgba->w <= 0 || rgba->h <= 0 || rgba->w > 8192 || rgba->h > 8192) {
        SDL_DestroySurface(rgba);
        return 0;
    }
    w = (uint32_t)rgba->w;
    h = (uint32_t)rgba->h;
    for (lw = w, lh = h; levels < TEXPACK_MAX_LEVELS; lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
        total += lw * lh * 4;
        levels++;
        if (lw == 1 && lh == 1) {
            break;
        }
    }
    f = SDL_malloc(total);
    if (f == NULL) {
        SDL_DestroySurface(rgba);
        return 0;
    }
    for (y = 0; y < h; y++) {
        memcpy(f + y * w * 4, (const uint8_t *)rgba->pixels + y * (uint32_t)rgba->pitch, w * 4);
    }
    SDL_DestroySurface(rgba);
    img->file = f;
    img->w = w;
    img->h = h;
    img->format = 0;
    img->levels = levels;
    for (level = 0, lw = w, lh = h; level < levels; level++) {
        img->data[level] = f + at;
        img->bytes[level] = lw * lh * 4;
        if (level + 1 < levels) {
            const uint8_t *src = f + at;
            uint32_t nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
            uint8_t *dst = f + at + lw * lh * 4;
            for (y = 0; y < nh; y++) {
                for (x = 0; x < nw; x++) {
                    uint32_t x0 = x * 2 < lw ? x * 2 : lw - 1, x1 = x * 2 + 1 < lw ? x * 2 + 1 : lw - 1;
                    uint32_t y0 = y * 2 < lh ? y * 2 : lh - 1, y1 = y * 2 + 1 < lh ? y * 2 + 1 : lh - 1;
                    for (c = 0; c < 4; c++) {
                        dst[(y * nw + x) * 4 + c] = (uint8_t)((src[(y0 * lw + x0) * 4 + c] + src[(y0 * lw + x1) * 4 + c] +
                                                               src[(y1 * lw + x0) * 4 + c] + src[(y1 * lw + x1) * 4 + c] + 2) / 4);
                    }
                }
            }
            at += lw * lh * 4;
            lw = nw;
            lh = nh;
        }
    }
    return 1;
}

static int load(const char *path, TexPackImage *img) {
    size_t size = 0;
    uint8_t *f;
    uint32_t w, h, mips, flags, fourcc, bpp, blk, at = 128, level;

    memset(img, 0, sizeof(*img));
    if (strlen(path) > 4 && SDL_strcasecmp(path + strlen(path) - 4, ".png") == 0) {
        return load_png(path, img);
    }
#ifdef BT3_UWP
    f = load_file_uwp(path, &size);
#else
    f = SDL_LoadFile(path, &size);
#endif
    if (f == NULL || size < 128 || memcmp(f, "DDS ", 4) != 0) {
        SDL_free(f);
        return 0;
    }
    memcpy(&h, f + 12, 4);
    memcpy(&w, f + 16, 4);
    memcpy(&mips, f + 28, 4);
    memcpy(&flags, f + 80, 4);
    memcpy(&fourcc, f + 84, 4);
    memcpy(&bpp, f + 88, 4);
    if (w == 0 || h == 0 || w > 8192 || h > 8192
#ifdef BT3_UWP
        || w > TEXPACK_DIM_LIMIT || h > TEXPACK_DIM_LIMIT
#endif
    ) {
        SDL_free(f);
        return 0;
    }
    if (mips == 0) {
        mips = 1;
    }
    if ((flags & 4) && fourcc == 0x31545844) { img->format = 1; blk = 8; }        /* DXT1 */
    else if ((flags & 4) && fourcc == 0x33545844) { img->format = 2; blk = 16; }  /* DXT3 */
    else if ((flags & 4) && fourcc == 0x35545844) { img->format = 3; blk = 16; }  /* DXT5 */
    else if (!(flags & 4) && bpp == 32) { img->format = 0; blk = 0; }
    else {
        SDL_free(f);
        return 0;
    }
    img->file = f;
    img->w = w;
    img->h = h;
    for (level = 0; level < mips && level < TEXPACK_MAX_LEVELS; level++) {
        uint32_t lw = w >> level ? w >> level : 1, lh = h >> level ? h >> level : 1;
        uint32_t bytes = blk ? ((lw + 3) / 4) * ((lh + 3) / 4) * blk : lw * lh * 4;
        if (at + bytes > size) {
            break;
        }
        if (blk == 0) { /* plain pixels: B, G, R, A in the file unless the red mask says otherwise */
            uint32_t rmask, i;
            memcpy(&rmask, f + 92, 4);
            if (rmask != 0x000000FF) {
                for (i = 0; i < lw * lh; i++) {
                    uint8_t t = f[at + i * 4];
                    f[at + i * 4] = f[at + i * 4 + 2];
                    f[at + i * 4 + 2] = t;
                }
            }
        }
        img->data[level] = f + at;
        img->bytes[level] = bytes;
        at += bytes;
        if (lw == 1 && lh == 1) {
            level++;
            break;
        }
    }
    img->levels = (int)level;
    if (img->levels == 0) {
        SDL_free(f);
        img->file = NULL;
        return 0;
    }
    return 1;
}

static void image_free(TexPackImage *img) {
    SDL_free(img->file);
    img->file = NULL;
}

static size_t image_bytes(const TexPackImage *img) {
    size_t n = 0;
    int level;
    for (level = 0; level < img->levels; level++) {
        n += img->bytes[level];
    }
    return n;
}

/* Called with sLock held: a file read, into its entry */
static void store(uint32_t id, int ok, const TexPackImage *img) {
    Entry *e = &sEntries[id];
#ifdef BT3_UWP
    if (ok) sStats.reads++;
    else sStats.failures++;
#endif
    if (!ok) {
        e->state = 3;
        return;
    }
    e->img = *img;
    e->held = image_bytes(img);
    e->state = 2;
    sHeld[sHeldCount++] = id;
    sHeldBytes += e->held;
}

static int SDLCALL reader(void *unused) {
    (void)unused;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    for (;;) {
        uint32_t id;
        TexPackImage img;
        int ok;
#ifdef BT3_UWP
        uint64_t read_start;
#endif
        SDL_LockMutex(sLock);
        while (sHead == sTail) {
            SDL_WaitCondition(sWake, sLock);
        }
        id = sQueue[sTail++ & (QUEUE - 1)];
        SDL_UnlockMutex(sLock);
#ifdef BT3_UWP
        read_start = SDL_GetTicksNS();
#endif
        ok = load(sEntries[id].path, &img);
        SDL_LockMutex(sLock);
#ifdef BT3_UWP
        sStats.fileReadNs += SDL_GetTicksNS() - read_start;
#endif
        store(id, ok, &img);
        SDL_UnlockMutex(sLock);
    }
    return 0;
}

/* Over the budget: the files asked for longest ago leave memory, down to three quarters of it (not `keep`, the one
   just asked for). Only this thread frees them; the reader only adds. Called with sLock held. */
static void trim(uint32_t keep) {
    while (sHeldBytes > sBudget / 4 * 3 && sHeldCount > 1) {
        uint32_t i, old = sHeldCount;
        for (i = 0; i < sHeldCount; i++) {
            if (sHeld[i] != keep && (old == sHeldCount || sEntries[sHeld[i]].used < sEntries[sHeld[old]].used)) {
                old = i;
            }
        }
        if (old == sHeldCount) {
            return;
        }
        {
            Entry *e = &sEntries[sHeld[old]];
            image_free(&e->img);
            sHeldBytes -= e->held;
            e->held = 0;
            e->state = 0;
#ifdef BT3_UWP
            sStats.evictions++;
#endif
            sHeld[old] = sHeld[--sHeldCount];
        }
    }
}

int TexPack_Get(int id, TexPackImage *img) {
    Entry *e;
    int r;

    if (id < 0 || (uint32_t)id >= sCount) {
        return -1;
    }
    if (sSync < 0) {
        const char *mb = getenv("BT3_PACK_MB");
        sSync = getenv("BT3_PACK_SYNC") != NULL && atoi(getenv("BT3_PACK_SYNC")) != 0;
        sBudget = mb != NULL && atoi(mb) > 0 ? (size_t)atoi(mb) << 20 : TEXPACK_BUDGET;
        sHeld = malloc(sCount * sizeof(uint32_t));
        sLock = SDL_CreateMutex();
        sWake = SDL_CreateCondition();
        if (!sSync && sHeld != NULL && sLock != NULL && sWake != NULL) {
            sReader = SDL_CreateThread(reader, "bt3-texpack", NULL);
        }
        if (sReader == NULL) {
            sSync = 1;
        }
    }
    if (sHeld == NULL || sLock == NULL || sWake == NULL) {
        return -1;
    }
    e = &sEntries[id];
    SDL_LockMutex(sLock);
    e->used = ++sTick;
    if (e->state == 0 && sSync) {
        TexPackImage read;
#ifdef BT3_UWP
        uint64_t read_start = SDL_GetTicksNS();
#endif
        int ok = load(e->path, &read);
#ifdef BT3_UWP
        sStats.fileReadNs += SDL_GetTicksNS() - read_start;
#endif
        store((uint32_t)id, ok, &read);
    } else if (e->state == 0 && sHead - sTail < QUEUE) {
        sQueue[sHead++ & (QUEUE - 1)] = (uint32_t)id;
        e->state = 1;
#ifdef BT3_UWP
        sStats.queued++;
#endif
        SDL_SignalCondition(sWake);
    }
    r = e->state == 2 ? 1 : e->state == 3 ? -1 : 0;
    if (r == 1) {
#ifdef BT3_UWP
        sStats.cacheHits++;
#endif
        *img = e->img;
        trim((uint32_t)id);
    }
    SDL_UnlockMutex(sLock);
    return r;
}

#ifdef BT3_UWP
void TexPack_ReadStats(TexPackStats *stats) {
    memset(stats, 0, sizeof(*stats));
    if (sLock == NULL) return;
    SDL_LockMutex(sLock);
    *stats = sStats;
    stats->heldBytes = sHeldBytes;
    stats->pending = sHead - sTail;
    stats->async = sReader != NULL;
    memset(&sStats, 0, sizeof(sStats));
    SDL_UnlockMutex(sLock);
}
#endif
