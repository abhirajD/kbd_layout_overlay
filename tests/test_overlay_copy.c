#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "overlay.h"

int main(void) {
    Overlay img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 1;
    img.channels = 4;

    size_t sz = (size_t)img.width * img.height * 4;
    img.data = (unsigned char *)malloc(sz);
    if (!img.data) {
        fprintf(stderr, "malloc failed\n");
        return 2;
    }

    /* Pixel 0: R=10, G=20, B=30, A=128 */
    img.data[0] = 10; /* R */
    img.data[1] = 20; /* G */
    img.data[2] = 30; /* B */
    img.data[3] = 128; /* A */

    /* Pixel 1: R=50, G=60, B=70, A=255 */
    img.data[4] = 50;
    img.data[5] = 60;
    img.data[6] = 70;
    img.data[7] = 255;

    unsigned char *dst = (unsigned char *)malloc(sz);
    if (!dst) {
        fprintf(stderr, "malloc failed for dst\n");
        free_overlay(&img);
        return 2;
    }
    memset(dst, 0, sz);

    /* Apply invert + 50% opacity onto dst buffer */
    int res = apply_effects_copy(&img, dst, 0.5f, 1);
    assert(res == 1);

    /* Source must be unchanged */
    assert(img.data[0] == 10);
    assert(img.data[1] == 20);
    assert(img.data[2] == 30);
    assert(img.data[3] == 128);

    assert(img.data[4] == 50);
    assert(img.data[5] == 60);
    assert(img.data[6] == 70);
    assert(img.data[7] == 255);

    /* Check transformed dst pixel 0:
       R = 255 - 10 = 245
       G = 255 - 20 = 235
       B = 255 - 30 = 225
       A = (unsigned char)(128 * 0.5) = 64
    */
    assert(dst[0] == (unsigned char)(255 - 10));
    assert(dst[1] == (unsigned char)(255 - 20));
    assert(dst[2] == (unsigned char)(255 - 30));
    assert(dst[3] == (unsigned char)(128 * 0.5f));

    /* Check transformed dst pixel 1:
       R = 255 - 50 = 205
       G = 255 - 60 = 195
       B = 255 - 70 = 185
       A = (unsigned char)(255 * 0.5) = 127 (truncated)
    */
    assert(dst[4] == (unsigned char)(255 - 50));
    assert(dst[5] == (unsigned char)(255 - 60));
    assert(dst[6] == (unsigned char)(255 - 70));
    assert(dst[7] == (unsigned char)(255 * 0.5f));

    free(dst);
    free_overlay(&img);

    printf("test_overlay_copy: OK\n");
    return 0;
}
