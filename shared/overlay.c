#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "overlay.h"
#include <stdlib.h>

int load_overlay_image(const char *path, Overlay *out) {
    out->data = stbi_load(path, &out->width, &out->height, &out->channels, 4);
    if (!out->data) return -1;
    out->channels = 4;
    return 0;
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
