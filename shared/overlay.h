#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    int width;
    int height;
    int channels; /* should be 4 for RGBA */
} Overlay;

#define MAX_CACHED_VARIATIONS 16
typedef struct {
    Overlay variations[MAX_CACHED_VARIATIONS];
    float opacity_levels[MAX_CACHED_VARIATIONS];
    int invert_flags[MAX_CACHED_VARIATIONS];
    int count;
    int base_width, base_height;
    int async_generation_complete;  // 0 = still generating, 1 = done

    /* Platform-opaque lock pointer for protecting async cache access.
       Implemented in overlay.c using CRITICAL_SECTION (Windows) or
       pthread_mutex_t (POSIX). */
    void *lock;
    int lock_inited;
} OverlayCache;

enum {
    OVERLAY_OK = 0,
    OVERLAY_ERR_MEMORY = -1,
    OVERLAY_ERR_NOT_FOUND = -2,
    OVERLAY_ERR_DECODE = -3
};

/* Load an image and scale it to fit within max_width/max_height while
   preserving aspect ratio. */
int load_overlay_image(const char *path, int max_width, int max_height, Overlay *out);
int load_overlay_image_mem(const unsigned char *buffer, int len,
                           int max_width, int max_height, Overlay *out);
void apply_opacity_inversion(Overlay *img, float opacity, int invert);
const unsigned char *get_overlay_buffer(const Overlay *img, int *width, int *height);
void free_overlay(Overlay *img);

/* Cache management functions */
int init_overlay_cache(OverlayCache *cache, const Overlay *base_image);
int init_overlay_cache_async(OverlayCache *cache, const Overlay *base_image); // Non-blocking version
const Overlay *get_cached_variation(OverlayCache *cache, float opacity, int invert);
void free_overlay_cache(OverlayCache *cache);

/* Performance testing */
void benchmark_image_processing(Overlay *img, float opacity, int invert);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_H */
