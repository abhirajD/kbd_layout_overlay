#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"
#include "overlay.h"
#include <stdlib.h>

static int finalize_image(unsigned char *data, int width, int height,
                          int max_width, int max_height, Overlay *out) {
    out->width = width;
    out->height = height;
    out->channels = 4;

    float scale_w = (float)max_width / (float)out->width;
    float scale_h = (float)max_height / (float)out->height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale < 1.0f) {
        int new_w = (int)(out->width * scale);
        int new_h = (int)(out->height * scale);
        if (new_w < 1) new_w = 1;
        if (new_h < 1) new_h = 1;

        unsigned char *resized = malloc((size_t)new_w * new_h * 4);
        if (!resized) {
            stbi_image_free(data);
            return -1;
        }

        if (!stbir_resize_uint8(data, out->width, out->height, 0,
                                resized, new_w, new_h, 0, 4)) {
            free(resized);
            stbi_image_free(data);
            return -1;
        }

        stbi_image_free(data);
        data = resized;
        out->width = new_w;
        out->height = new_h;
    }

    out->data = data;
    return 0;
}

int load_overlay_image(const char *path, int max_width, int max_height, Overlay *out) {
    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) return -1;
    return finalize_image(data, w, h, max_width, max_height, out);
}

int load_overlay_image_mem(const unsigned char *buffer, int len,
                           int max_width, int max_height, Overlay *out) {
    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(buffer, len, &w, &h, &channels, 4);
    if (!data) return -1;
    return finalize_image(data, w, h, max_width, max_height, out);
}

void apply_opacity_inversion(Overlay *img, float opacity, int invert) {
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
