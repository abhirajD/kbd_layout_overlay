#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"
#include "overlay.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

const unsigned char *get_default_keymap(int *size) {
#ifdef EMBED_KEYMAP
    /* Use embedded keymap data generated at build time */
    #include "keymap_embedded.h"
    if (size) *size = embedded_keymap_size;
    return embedded_keymap_data;
#else
    /* No embedded keymap - use external file only */
    if (size) *size = 0;
    return NULL;
#endif
}

/* Consistent memory allocation wrapper */
static void* overlay_alloc(size_t size) {
    return malloc(size);
}

static void overlay_free(void* ptr) {
    if (ptr) free(ptr);
}

static OverlayError finalize_image(unsigned char *data, int width, int height,
                                   int max_width, int max_height, Overlay *out) {
    if (!data || !out) {
        overlay_free(data);
        return OVERLAY_ERROR_NULL_PARAM;
    }

    out->width = width;
    out->height = height;
    out->channels = 4;
    out->cached_effects = 0;
    out->cached_opacity = 1.0f;
    out->cached_invert = 0;

    float scale_w = (float)max_width / (float)width;
    float scale_h = (float)max_height / (float)height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    if (scale < 1.0f) {
        int new_w = (int)(width * scale);
        int new_h = (int)(height * scale);
        if (new_w < 1) new_w = 1;
        if (new_h < 1) new_h = 1;

        unsigned char *resized = overlay_alloc((size_t)new_w * new_h * 4);
        if (!resized) {
            overlay_free(data);
            return OVERLAY_ERROR_OUT_OF_MEMORY;
        }

        if (!stbir_resize_uint8(data, width, height, 0,
                                resized, new_w, new_h, 0, 4)) {
            overlay_free(resized);
            overlay_free(data);
            return OVERLAY_ERROR_RESIZE_FAILED;
        }

        overlay_free(data);
        data = resized;
        out->width = new_w;
        out->height = new_h;
    }

    out->data = data;
    return OVERLAY_OK;
}

OverlayError load_overlay(const char *path, int max_width, int max_height, Overlay *out) {
    if (!path || !out) return OVERLAY_ERROR_NULL_PARAM;
    
    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) return OVERLAY_ERROR_FILE_NOT_FOUND;
    
    return finalize_image(data, w, h, max_width, max_height, out);
}

OverlayError load_overlay_mem(const unsigned char *buffer, int len, 
                              int max_width, int max_height, Overlay *out) {
    if (!buffer || !out) return OVERLAY_ERROR_NULL_PARAM;
    
    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
    if (!data) return OVERLAY_ERROR_DECODE_FAILED;
    
    return finalize_image(data, w, h, max_width, max_height, out);
}

void apply_effects(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    
    /* Check if effects are already applied */
    int effects_mask = (opacity != 1.0f ? 1 : 0) | (invert ? 2 : 0);
    if (img->cached_effects == effects_mask && 
        img->cached_opacity == opacity && 
        img->cached_invert == invert) {
        return; /* Effects already applied */
    }
    
    size_t total = (size_t)img->width * img->height * 4;
    for (size_t i = 0; i < total; i += 4) {
        unsigned char *px = &img->data[i];
        if (invert) {
            px[0] = 255 - px[0]; /* R */
            px[1] = 255 - px[1]; /* G */
            px[2] = 255 - px[2]; /* B */
        }
        px[3] = (unsigned char)(px[3] * opacity); /* A */
    }
    
    /* Update cache */
    img->cached_effects = effects_mask;
    img->cached_opacity = opacity;
    img->cached_invert = invert;
}

/* Non-destructive apply: copy src pixels into dst buffer and apply effects there.
   dst must be preallocated to src->width * src->height * src->channels (4).
   Returns 1 on success, 0 on failure (e.g., null params). */
int apply_effects_copy(const Overlay *src, unsigned char *dst, float opacity, int invert) {
    if (!src || !src->data || !dst) return 0;
    size_t total = (size_t)src->width * src->height * 4;
    memcpy(dst, src->data, total);
    Overlay tmp;
    tmp.data = dst;
    tmp.width = src->width;
    tmp.height = src->height;
    tmp.channels = src->channels;
    tmp.cached_effects = 0;
    tmp.cached_opacity = 0.0f;
    tmp.cached_invert = 0;
    apply_effects(&tmp, opacity, invert);
    return 1;
}

void free_overlay(Overlay *img) {
    if (img && img->data) {
        overlay_free(img->data);
        img->data = NULL;
        img->cached_effects = 0;
    }
}

/* Thread safety implementation */
void overlay_mutex_init(overlay_mutex_t *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    InitializeCriticalSection(mutex);
#else
    pthread_mutex_init(mutex, NULL);
#endif
}

void overlay_mutex_lock(overlay_mutex_t *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

void overlay_mutex_unlock(overlay_mutex_t *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void overlay_mutex_destroy(overlay_mutex_t *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

/* Simple helpers and cache implementation */

/* Duplicate an overlay (deep copy of pixel data). Returns 1 on success. */
static int duplicate_overlay_into(Overlay *dst, const Overlay *src) {
    if (!dst || !src || !src->data) return 0;

    size_t data_size = (size_t)src->width * src->height * 4;
    dst->data = (unsigned char *)overlay_alloc(data_size);
    if (!dst->data) return 0;

    memcpy(dst->data, src->data, data_size);
    dst->width = src->width;
    dst->height = src->height;
    dst->channels = src->channels;
    dst->cached_effects = src->cached_effects;
    dst->cached_opacity = src->cached_opacity;
    dst->cached_invert = src->cached_invert;
    return 1;
}

/* Async thread data */
typedef struct {
    OverlayCache *cache;
    Overlay base_image; /* deep copy */
} AsyncCacheData;

/* Generate variations into provided cache (non-threaded). Returns number generated. */
static int generate_variations(OverlayCache *cache, const Overlay *base_image) {
    if (!cache || !base_image || !base_image->data) return 0;

    cache->count = 0;

    float opacity_levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
    int invert_states[] = {0, 1};

    for (int o = 0; o < 4 && cache->count < MAX_CACHED_VARIATIONS; o++) {
        for (int i = 0; i < 2 && cache->count < MAX_CACHED_VARIATIONS; i++) {
            Overlay *slot = &cache->variations[cache->count];
            if (!duplicate_overlay_into(slot, base_image)) {
                continue;
            }
            apply_effects(slot, opacity_levels[o], invert_states[i]);
            cache->opacity_levels[cache->count] = opacity_levels[o];
            cache->invert_flags[cache->count] = invert_states[i];
            cache->count++;
        }
    }

    return cache->count;
}

/* Background thread routine to populate cache */
#ifdef _WIN32
static unsigned __stdcall async_cache_thread(void *param) {
#else
static void *async_cache_thread(void *param) {
#endif
    AsyncCacheData *d = (AsyncCacheData *)param;
    if (!d) {
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    OverlayCache *cache = d->cache;
    Overlay local_base;
    memset(&local_base, 0, sizeof(Overlay));
    if (!duplicate_overlay_into(&local_base, &d->base_image)) {
        free_overlay(&d->base_image);
        overlay_free(d);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    /* Build local cache then publish under lock */
    OverlayCache local_cache;
    memset(&local_cache, 0, sizeof(OverlayCache));
    generate_variations(&local_cache, &local_base);

    /* Publish */
    overlay_mutex_lock(&cache->lock);
    /* free any existing variations in cache */
    for (int i = 0; i < cache->count; i++) {
        free_overlay(&cache->variations[i]);
    }
    /* copy newly generated variations into shared cache */
    for (int i = 0; i < local_cache.count && i < MAX_CACHED_VARIATIONS; i++) {
        /* move ownership: duplicate already allocated data pointer to cache slot */
        cache->variations[i] = local_cache.variations[i];
        cache->opacity_levels[i] = local_cache.opacity_levels[i];
        cache->invert_flags[i] = local_cache.invert_flags[i];
    }
    cache->count = local_cache.count;
    overlay_mutex_unlock(&cache->lock);

    /* Clean up local_base and AsyncCacheData container */
    free_overlay(&local_base);
    free_overlay(&d->base_image);
    overlay_free(d);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* Synchronous cache initialization */
int init_overlay_cache(OverlayCache *cache, const Overlay *base_image) {
    if (!cache || !base_image) return OVERLAY_ERROR_NULL_PARAM;

    memset(cache, 0, sizeof(OverlayCache));
    overlay_mutex_init(&cache->lock);

    if (!duplicate_overlay_into(&cache->variations[0], base_image)) {
        overlay_mutex_destroy(&cache->lock);
        return OVERLAY_ERROR_OUT_OF_MEMORY;
    }
    /* Generate all variations into cache */
    generate_variations(cache, base_image);
    return OVERLAY_OK;
}

/* Asynchronous cache initialization */
int init_overlay_cache_async(OverlayCache *cache, const Overlay *base_image) {
    if (!cache || !base_image) return OVERLAY_ERROR_NULL_PARAM;

    memset(cache, 0, sizeof(OverlayCache));
    overlay_mutex_init(&cache->lock);

    AsyncCacheData *d = (AsyncCacheData *)overlay_alloc(sizeof(AsyncCacheData));
    if (!d) {
        overlay_mutex_destroy(&cache->lock);
        return OVERLAY_ERROR_OUT_OF_MEMORY;
    }
    d->cache = cache;
    memset(&d->base_image, 0, sizeof(Overlay));
    if (!duplicate_overlay_into(&d->base_image, base_image)) {
        overlay_free(d);
        overlay_mutex_destroy(&cache->lock);
        return OVERLAY_ERROR_OUT_OF_MEMORY;
    }

#ifdef _WIN32
    unsigned thread_id = 0;
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, async_cache_thread, d, 0, &thread_id);
    if (h) {
        CloseHandle(h);
        return OVERLAY_OK;
    }
#else
    pthread_t thr;
    if (pthread_create(&thr, NULL, async_cache_thread, d) == 0) {
        pthread_detach(thr);
        return OVERLAY_OK;
    }
#endif

    /* fallback to synchronous if thread creation fails */
    free_overlay(&d->base_image);
    overlay_free(d);
    /* destroy mutex before fallback to synchronous init (re-init in that call) */
    overlay_mutex_destroy(&cache->lock);
    return init_overlay_cache(cache, base_image);
}

/* Lookup cached variation; returns pointer to internal Overlay or NULL */
const Overlay *get_cached_variation(OverlayCache *cache, float opacity, int invert) {
    if (!cache) return NULL;
    overlay_mutex_lock(&cache->lock);
    if (cache->count == 0) {
        overlay_mutex_unlock(&cache->lock);
        return NULL;
    }

    const Overlay *result = NULL;
    for (int i = 0; i < cache->count; i++) {
        if (cache->invert_flags[i] == invert && fabsf(cache->opacity_levels[i] - opacity) < 0.001f) {
            result = &cache->variations[i];
            break;
        }
    }

    if (!result) {
        float best_diff = 2.0f;
        int best_idx = -1;
        for (int i = 0; i < cache->count; i++) {
            if (cache->invert_flags[i] != invert) continue;
            float diff = fabsf(cache->opacity_levels[i] - opacity);
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }
        if (best_idx >= 0) result = &cache->variations[best_idx];
    }

    overlay_mutex_unlock(&cache->lock);
    return result;
}

/* Free cache and contained overlays */
void free_overlay_cache(OverlayCache *cache) {
    if (!cache) return;
    overlay_mutex_lock(&cache->lock);
    for (int i = 0; i < cache->count; i++) {
        free_overlay(&cache->variations[i]);
    }
    cache->count = 0;
    overlay_mutex_unlock(&cache->lock);
    overlay_mutex_destroy(&cache->lock);
}
