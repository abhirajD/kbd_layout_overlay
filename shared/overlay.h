#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    int width;
    int height;
    int channels; /* 4 for RGBA */
} Overlay;

/* Load overlay image - returns 0 on success, -1 on error */
int load_overlay(const char *path, int max_width, int max_height, Overlay *out);

/* Load from memory buffer */
int load_overlay_mem(const unsigned char *buffer, int len, int max_width, int max_height, Overlay *out);

/* Apply opacity and inversion effects */
void apply_effects(Overlay *img, float opacity, int invert);

/* Free overlay resources */
void free_overlay(Overlay *img);

/* Get embedded default keymap */
const unsigned char *get_default_keymap(int *size);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_H */