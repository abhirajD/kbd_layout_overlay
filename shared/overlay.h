#ifndef OVERLAY_H
#define OVERLAY_H

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

int load_overlay_image(const char *path, int max_width, int max_height, Overlay *out);
void apply_opacity_inversion(Overlay *img, float opacity, int invert);
const unsigned char *get_overlay_buffer(const Overlay *img, int *width, int *height);
void free_overlay(Overlay *img);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_H */
