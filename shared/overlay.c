#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"
#include "overlay.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

// Threading includes
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define SIMD_AVAILABLE 1
#define SIMD_SSE2 1
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>  // SSE2
#define SIMD_AVAILABLE 1
#define SIMD_SSE2 1
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__)
#include <arm_neon.h>
#define SIMD_AVAILABLE 1
#define SIMD_NEON 1
#else
#define SIMD_AVAILABLE 0
#endif

 // Structure for async cache generation
typedef struct {
    OverlayCache *cache;
    Overlay base_image;  // Copy of the base image for thread safety
} AsyncCacheData;

/* OverlayCache lock helpers (platform-specific). We allocate a platform-specific
   lock object and store it in cache->lock. lock_inited indicates successful init.
*/
static int overlay_cache_lock_init(OverlayCache *cache) {
    if (!cache) return 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    if (!cs) return 0;
    InitializeCriticalSection(cs);
    cache->lock = cs;
    cache->lock_inited = 1;
    return 1;
#else
    pthread_mutex_t *mtx = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!mtx) return 0;
    if (pthread_mutex_init(mtx, NULL) != 0) {
        free(mtx);
        return 0;
    }
    cache->lock = mtx;
    cache->lock_inited = 1;
    return 1;
#endif
}

static void overlay_cache_lock(OverlayCache *cache) {
    if (!cache || !cache->lock_inited) return;
#ifdef _WIN32
    EnterCriticalSection((CRITICAL_SECTION*)cache->lock);
#else
    pthread_mutex_lock((pthread_mutex_t*)cache->lock);
#endif
}

static void overlay_cache_unlock(OverlayCache *cache) {
    if (!cache || !cache->lock_inited) return;
#ifdef _WIN32
    LeaveCriticalSection((CRITICAL_SECTION*)cache->lock);
#else
    pthread_mutex_unlock((pthread_mutex_t*)cache->lock);
#endif
}

static void overlay_cache_lock_destroy(OverlayCache *cache) {
    if (!cache || !cache->lock_inited) return;
#ifdef _WIN32
    DeleteCriticalSection((CRITICAL_SECTION*)cache->lock);
    free(cache->lock);
#else
    pthread_mutex_destroy((pthread_mutex_t*)cache->lock);
    free(cache->lock);
#endif
    cache->lock = NULL;
    cache->lock_inited = 0;
}

static int finalize_image(unsigned char *data, int width, int height,
                          int max_width, int max_height, Overlay *out) {
    out->width = width;
    out->height = height;
    out->channels = 4;

    float scale_w = (float)max_width / (float)out->width;
    float scale_h = (float)max_height / (float)out->height;
    float scale = scale_w < scale_h ? scale_w : scale_h;

    if (scale != 1.0f) {
        int new_w = (int)(out->width * scale);
        int new_h = (int)(out->height * scale);
        if (new_w < 1) new_w = 1;
        if (new_h < 1) new_h = 1;

        unsigned char *resized = malloc((size_t)new_w * new_h * 4);
        if (!resized) {
            stbi_image_free(data);
            return OVERLAY_ERR_MEMORY;
        }

        if (!stbir_resize_uint8(data, out->width, out->height, 0,
                                resized, new_w, new_h, 0, 4)) {
            free(resized);
            stbi_image_free(data);
            return OVERLAY_ERR_MEMORY;
        }

        stbi_image_free(data);
        data = resized;
        out->width = new_w;
        out->height = new_h;
    }

    out->data = data;
    return OVERLAY_OK;
}

int load_overlay_image(const char *path, int max_width, int max_height, Overlay *out) {
    int w, h, channels;
    FILE *f = fopen(path, "rb");
    if (!f) return OVERLAY_ERR_NOT_FOUND;
    unsigned char *data = stbi_load_from_file(f, &w, &h, &channels, 4);
    fclose(f);
    if (!data) return OVERLAY_ERR_DECODE;
    return finalize_image(data, w, h, max_width, max_height, out);
}

int load_overlay_image_mem(const unsigned char *buffer, int len,
                           int max_width, int max_height, Overlay *out) {
    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
    if (!data) return OVERLAY_ERR_DECODE;
    return finalize_image(data, w, h, max_width, max_height, out);
}

#if SIMD_AVAILABLE

#ifdef SIMD_SSE2
static void apply_opacity_inversion_simd(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    
    unsigned char *data = img->data;
    size_t total_bytes = (size_t)img->width * img->height * 4;
    size_t simd_bytes = (total_bytes / 16) * 16; // Process 16 bytes at a time
    
    if (invert) {
        // SIMD inversion: process 16 bytes at once
        __m128i all_255 = _mm_set1_epi8((char)255);
        __m128i alpha_mask = _mm_set_epi32(0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);
        
        for (size_t i = 0; i < simd_bytes; i += 16) {
            __m128i pixels = _mm_loadu_si128((__m128i*)(data + i));
            
            // Save original alpha channels
            __m128i original_alpha = _mm_and_si128(pixels, alpha_mask);
            
            // Invert all bytes
            __m128i inverted = _mm_sub_epi8(all_255, pixels);
            
            // Restore original alpha (don't invert alpha)
            __m128i inverted_rgb = _mm_andnot_si128(alpha_mask, inverted);
            pixels = _mm_or_si128(inverted_rgb, original_alpha);
            
            _mm_storeu_si128((__m128i*)(data + i), pixels);
        }
        
        // Handle remaining bytes
        for (size_t i = simd_bytes; i < total_bytes; i += 4) {
            unsigned char *px = &data[i];
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
            // px[3] unchanged (alpha)
        }
    }
    
    // Apply opacity (scalar is simpler and fast enough for alpha channel only)
    if (opacity != 1.0f) {
        for (size_t i = 3; i < total_bytes; i += 4) {
            data[i] = (unsigned char)(data[i] * opacity);
        }
    }
}
#endif

#ifdef SIMD_NEON
static void apply_opacity_inversion_simd(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    
    unsigned char *data = img->data;
    size_t total_bytes = (size_t)img->width * img->height * 4;
    size_t simd_bytes = (total_bytes / 16) * 16; // Process 16 bytes at a time
    
    if (invert) {
        // NEON inversion: process 16 bytes at once
        uint8x16_t all_255 = vdupq_n_u8(255);
        uint32x4_t alpha_mask = vdupq_n_u32(0xFF000000);
        
        for (size_t i = 0; i < simd_bytes; i += 16) {
            uint8x16_t pixels = vld1q_u8(data + i);
            
            // Save original alpha channels
            uint32x4_t pixels_u32 = vreinterpretq_u32_u8(pixels);
            uint32x4_t original_alpha = vandq_u32(pixels_u32, alpha_mask);
            
            // Invert all bytes
            uint8x16_t inverted = vsubq_u8(all_255, pixels);
            
            // Restore original alpha (don't invert alpha)
            uint32x4_t inverted_u32 = vreinterpretq_u32_u8(inverted);
            uint32x4_t inverted_rgb = vbicq_u32(inverted_u32, alpha_mask);
            uint32x4_t result = vorrq_u32(inverted_rgb, original_alpha);
            
            vst1q_u8(data + i, vreinterpretq_u8_u32(result));
        }
        
        // Handle remaining bytes
        for (size_t i = simd_bytes; i < total_bytes; i += 4) {
            unsigned char *px = &data[i];
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
            // px[3] unchanged (alpha)
        }
    }
    
    // Apply opacity (scalar is simpler and fast enough for alpha channel only)
    if (opacity != 1.0f) {
        for (size_t i = 3; i < total_bytes; i += 4) {
            data[i] = (unsigned char)(data[i] * opacity);
        }
    }
}
#endif

#endif

static void apply_opacity_inversion_scalar(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    size_t total = (size_t)img->width * img->height * 4;
    for (size_t i = 0; i < total; i += 4) {
        unsigned char *px = &img->data[i];
        if (invert) {
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
        }
        px[3] = (unsigned char)(px[3] * opacity);
    }
}

void apply_opacity_inversion(Overlay *img, float opacity, int invert) {
#if SIMD_AVAILABLE
    apply_opacity_inversion_simd(img, opacity, invert);
#else
    apply_opacity_inversion_scalar(img, opacity, invert);
#endif
}

// For debugging: test performance difference
void benchmark_image_processing(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    
    // Create a copy for testing
    size_t data_size = (size_t)img->width * img->height * 4;
    unsigned char *backup = malloc(data_size);
    if (!backup) return;
    
    memcpy(backup, img->data, data_size);
    
#if SIMD_AVAILABLE
    // Test SIMD version
    clock_t start = clock();
    for (int i = 0; i < 10; i++) {
        memcpy(img->data, backup, data_size);
        apply_opacity_inversion_simd(img, opacity, invert);
    }
    clock_t simd_time = clock() - start;
    
    // Test scalar version
    start = clock();
    for (int i = 0; i < 10; i++) {
        memcpy(img->data, backup, data_size);
        apply_opacity_inversion_scalar(img, opacity, invert);
    }
    clock_t scalar_time = clock() - start;
    
    printf("SIMD: %lu ms, Scalar: %lu ms, Speedup: %.1fx\n", 
           simd_time * 1000 / CLOCKS_PER_SEC,
           scalar_time * 1000 / CLOCKS_PER_SEC,
           (double)scalar_time / simd_time);
#endif
    
    // Restore original data
    memcpy(img->data, backup, data_size);
    free(backup);
}

const unsigned char *get_overlay_buffer(const Overlay *img, int *width, int *height) {
    if (!img || !img->data) return NULL;
    if (width) *width = img->width;
    if (height) *height = img->height;
    return img->data;
}

void free_overlay(Overlay *img) {
    if (img && img->data) {
        stbi_image_free(img->data);
        img->data = NULL;
    }
}

static int duplicate_overlay_into(Overlay *dst, const Overlay *src) {
    if (!dst || !src || !src->data) return 0;
    
    size_t data_size = (size_t)src->width * src->height * 4;
    dst->data = malloc(data_size);
    if (!dst->data) return 0;
    
    memcpy(dst->data, src->data, data_size);
    dst->width = src->width;
    dst->height = src->height;
    dst->channels = src->channels;
    
    return 1;
}

// Background thread function for cache generation
#ifdef _WIN32
static unsigned __stdcall async_cache_thread(void *param) {
#else
static void *async_cache_thread(void *param) {
#endif
    AsyncCacheData *data = (AsyncCacheData *)param;
    OverlayCache *cache = data->cache;
    
    // Prepare a local cache to build variations off-thread, then publish atomically
    OverlayCache local_cache;
    memset(&local_cache, 0, sizeof(OverlayCache));
    local_cache.base_width = data->base_image.width;
    local_cache.base_height = data->base_image.height;
    
    // Precompute common variations: 4 opacity levels × 2 invert states = 8 variations
    float opacity_levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
    int invert_states[] = {0, 1};
    
    for (int o = 0; o < 4 && local_cache.count < MAX_CACHED_VARIATIONS; o++) {
        for (int i = 0; i < 2 && local_cache.count < MAX_CACHED_VARIATIONS; i++) {
            if (!duplicate_overlay_into(&local_cache.variations[local_cache.count], &data->base_image)) {
                continue;
            }
            
            apply_opacity_inversion(&local_cache.variations[local_cache.count], opacity_levels[o], invert_states[i]);
            
            local_cache.opacity_levels[local_cache.count] = opacity_levels[o];
            local_cache.invert_flags[local_cache.count] = invert_states[i];
            local_cache.count++;
        }
    }
    
    // Publish local cache into shared cache in one atomic-like step.
    // Acquire lock to prevent concurrent readers from seeing a partially-updated cache.
    if (cache->lock_inited) overlay_cache_lock(cache);

    memcpy(cache->variations, local_cache.variations, sizeof(Overlay) * local_cache.count);
    memcpy(cache->opacity_levels, local_cache.opacity_levels, sizeof(float) * local_cache.count);
    memcpy(cache->invert_flags, local_cache.invert_flags, sizeof(int) * local_cache.count);
    cache->base_width = local_cache.base_width;
    cache->base_height = local_cache.base_height;
    cache->count = local_cache.count;
    cache->async_generation_complete = 1;

    if (cache->lock_inited) overlay_cache_unlock(cache);
    
    // Do not free the variation data - ownership now belongs to shared cache.
    // Clean up base image copy
    free_overlay(&data->base_image);
    free(data);
    
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int init_overlay_cache(OverlayCache *cache, const Overlay *base_image) {
    if (!cache || !base_image) return OVERLAY_ERR_MEMORY;
    
    memset(cache, 0, sizeof(OverlayCache));
    cache->base_width = base_image->width;
    cache->base_height = base_image->height;
    cache->async_generation_complete = 0;

    /* Initialize lock for protecting readers/writers. If lock init fails,
       treat as an out-of-memory condition. */
    if (!overlay_cache_lock_init(cache)) {
        return OVERLAY_ERR_MEMORY;
    }
    
    // Synchronous version - generate all variations immediately
    float opacity_levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
    int invert_states[] = {0, 1};
    
    for (int o = 0; o < 4 && cache->count < MAX_CACHED_VARIATIONS; o++) {
        for (int i = 0; i < 2 && cache->count < MAX_CACHED_VARIATIONS; i++) {
            if (!duplicate_overlay_into(&cache->variations[cache->count], base_image)) {
                continue;
            }
            
            apply_opacity_inversion(&cache->variations[cache->count], opacity_levels[o], invert_states[i]);
            
            cache->opacity_levels[cache->count] = opacity_levels[o];
            cache->invert_flags[cache->count] = invert_states[i];
            cache->count++;
        }
    }
    
    cache->async_generation_complete = 1;
    return OVERLAY_OK;
}

int init_overlay_cache_async(OverlayCache *cache, const Overlay *base_image) {
    if (!cache || !base_image) return OVERLAY_ERR_MEMORY;
    
    memset(cache, 0, sizeof(OverlayCache));
    cache->base_width = base_image->width;
    cache->base_height = base_image->height;
    cache->async_generation_complete = 0;

    /* Initialize lock for protecting readers/writers. If lock init fails,
       fallback to synchronous init to avoid leaving callers without a usable cache. */
    if (!overlay_cache_lock_init(cache)) {
        return init_overlay_cache(cache, base_image);
    }
    
    // Create data for background thread
    AsyncCacheData *async_data = malloc(sizeof(AsyncCacheData));
    if (!async_data) return OVERLAY_ERR_MEMORY;
    
    async_data->cache = cache;
    
    // Create a deep copy of the base image for thread safety
    if (!duplicate_overlay_into(&async_data->base_image, base_image)) {
        free(async_data);
        return OVERLAY_ERR_MEMORY;
    }
    
    // Start background thread
#ifdef _WIN32
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, async_cache_thread, async_data, 0, NULL);
    if (thread) {
        CloseHandle(thread); // We don't need to wait for it
        return OVERLAY_OK;
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, async_cache_thread, async_data) == 0) {
        pthread_detach(thread); // We don't need to wait for it
        return OVERLAY_OK;
    }
#endif
    
    // Fallback to synchronous if thread creation fails
    free_overlay(&async_data->base_image);
    free(async_data);
    return init_overlay_cache(cache, base_image);
}

const Overlay *get_cached_variation(OverlayCache *cache, float opacity, int invert) {
    if (!cache) return NULL;

    if (cache->lock_inited) overlay_cache_lock(cache);
    
    // If async generation isn't complete and we have no variations yet,
    // we'll return NULL and let the caller fall back to real-time processing
    if (cache->count == 0) {
        if (cache->lock_inited) overlay_cache_unlock(cache);
        return NULL;
    }
    
    // Look for exact match first
    const Overlay *result = NULL;
    for (int i = 0; i < cache->count; i++) {
        if (fabs(cache->opacity_levels[i] - opacity) < 0.001f && 
            cache->invert_flags[i] == invert) {
            result = &cache->variations[i];
            break;
        }
    }
    
    // No exact match found - return closest match with correct invert flag
    if (!result) {
        float closest_diff = 2.0f;
        int closest_idx = -1;
        
        for (int i = 0; i < cache->count; i++) {
            if (cache->invert_flags[i] == invert) {
                float diff = fabs(cache->opacity_levels[i] - opacity);
                if (diff < closest_diff) {
                    closest_diff = diff;
                    closest_idx = i;
                }
            }
        }
        
        if (closest_idx >= 0) result = &cache->variations[closest_idx];
    }

    if (cache->lock_inited) overlay_cache_unlock(cache);
    return result;
}

void free_overlay_cache(OverlayCache *cache) {
    if (!cache) return;

    if (cache->lock_inited) overlay_cache_lock(cache);
    
    for (int i = 0; i < cache->count; i++) {
        if (cache->variations[i].data) {
            stbi_image_free(cache->variations[i].data);
            cache->variations[i].data = NULL;
        }
    }
    
    cache->count = 0;

    if (cache->lock_inited) {
        overlay_cache_unlock(cache);
        overlay_cache_lock_destroy(cache);
    }
}
