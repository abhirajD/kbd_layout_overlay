#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"
#include "overlay.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Default embedded keymap - simple 1x1 white pixel for MVP */
static const unsigned char default_keymap[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x08, 0x06, 0x00, 0x00, 0x00, 0xE0, 0x77, 0x3D, 0xF8, 0x00, 0x00, 0x00,
    0x15, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0xED, 0xC1, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x82, 0x20, 0xFF, 0xAF, 0x6E, 0x48, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x57, 0x02, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

const unsigned char *get_default_keymap(int *size) {
    if (size) *size = sizeof(default_keymap);
    return default_keymap;
}

static int finalize_image(unsigned char *data, int width, int height,
                          int max_width, int max_height, Overlay *out) {
    out->width = width;
    out->height = height;
    out->channels = 4;

    float scale_w = (float)max_width / (float)width;
    float scale_h = (float)max_height / (float)height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    if (scale < 1.0f) {
        int new_w = (int)(width * scale);
        int new_h = (int)(height * scale);
        if (new_w < 1) new_w = 1;
        if (new_h < 1) new_h = 1;

        unsigned char *resized = malloc((size_t)new_w * new_h * 4);
        if (!resized) {
            free(data);
            return -1;
        }

        if (!stbir_resize_uint8(data, width, height, 0,
                                resized, new_w, new_h, 0, 4)) {
            free(resized);
            free(data);
            return -1;
        }

        free(data);
        data = resized;
        out->width = new_w;
        out->height = new_h;
    }

    out->data = data;
    return 0;
}

int load_overlay(const char *path, int max_width, int max_height, Overlay *out) {
    if (!path || !out) return -1;
    
    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) return -1;
    
    return finalize_image(data, w, h, max_width, max_height, out);
}

int load_overlay_mem(const unsigned char *buffer, int len, 
                     int max_width, int max_height, Overlay *out) {
    if (!buffer || !out) return -1;
    
    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
    if (!data) return -1;
    
    return finalize_image(data, w, h, max_width, max_height, out);
}

void apply_effects(Overlay *img, float opacity, int invert) {
    if (!img || !img->data) return;
    
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
}

void free_overlay(Overlay *img) {
    if (img && img->data) {
        free(img->data);
        img->data = NULL;
    }
}