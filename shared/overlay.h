#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>

#ifdef _WIN32
    #include <windows.h>
    typedef CRITICAL_SECTION overlay_mutex_t;
#else
    #include <pthread.h>
    typedef pthread_mutex_t overlay_mutex_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
    OVERLAY_OK = 0,
    OVERLAY_ERROR_NULL_PARAM = -1,
    OVERLAY_ERROR_FILE_NOT_FOUND = -2,
    OVERLAY_ERROR_DECODE_FAILED = -3,
    OVERLAY_ERROR_OUT_OF_MEMORY = -4,
    OVERLAY_ERROR_RESIZE_FAILED = -5
} OverlayError;

typedef struct {
    unsigned char *data;
    int width;
    int height;
    int channels; /* 4 for RGBA */
    int cached_effects; /* Bitmask: 1=opacity cached, 2=invert cached */
    float cached_opacity;
    int cached_invert;
} Overlay;

/* Load overlay image - returns OverlayError */
OverlayError load_overlay(const char *path, int max_width, int max_height, Overlay *out);

/* Load from memory buffer */
OverlayError load_overlay_mem(const unsigned char *buffer, int len, int max_width, int max_height, Overlay *out);

/* Apply opacity and inversion effects */
void apply_effects(Overlay *img, float opacity, int invert);

/* Free overlay resources */
void free_overlay(Overlay *img);

/* Get embedded default keymap */
const unsigned char *get_default_keymap(int *size);

/* Thread safety functions */
void overlay_mutex_init(overlay_mutex_t *mutex);
void overlay_mutex_lock(overlay_mutex_t *mutex);
void overlay_mutex_unlock(overlay_mutex_t *mutex);
void overlay_mutex_destroy(overlay_mutex_t *mutex);

/* Simple cache for precomputed variations (opacity / invert) */
#define MAX_CACHED_VARIATIONS 16
typedef struct {
    Overlay variations[MAX_CACHED_VARIATIONS];
    float opacity_levels[MAX_CACHED_VARIATIONS];
    int invert_flags[MAX_CACHED_VARIATIONS];
    int count;
    overlay_mutex_t lock;
} OverlayCache;

/* Cache management */
int init_overlay_cache(OverlayCache *cache, const Overlay *base_image); /* synchronous */
int init_overlay_cache_async(OverlayCache *cache, const Overlay *base_image); /* background populate */
const Overlay *get_cached_variation(OverlayCache *cache, float opacity, int invert);
void free_overlay_cache(OverlayCache *cache);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_H */
