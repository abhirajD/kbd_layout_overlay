#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "overlay.h"
#include <stdlib.h>
#include <string.h>

int load_overlay_image(const char *path, int max_width, int max_height, Overlay *out) {
    unsigned char *data = stbi_load(path, &out->width, &out->height, &out->channels, 4);
    if (!data) return -1;
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
        float inv = 1.0f / scale;
        for (int y = 0; y < new_h; ++y) {
            int src_y = (int)(y * inv);
            if (src_y >= out->height) src_y = out->height - 1;
            for (int x = 0; x < new_w; ++x) {
                int src_x = (int)(x * inv);
                if (src_x >= out->width) src_x = out->width - 1;
                memcpy(&resized[(y * new_w + x) * 4],
                       &data[(src_y * out->width + src_x) * 4], 4);
            }
        }
        stbi_image_free(data);
        data = resized;
        out->width = new_w;
        out->height = new_h;
    }

    out->data = data;
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
