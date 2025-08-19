#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "overlay.h"

int main(void) {
    Overlay img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 2;
    img.channels = 4;

    size_t sz = (size_t)img.width * img.height * 4;
    img.data = (unsigned char *)malloc(sz);
    if (!img.data) {
        fprintf(stderr, "malloc failed\n");
        return 2;
    }

    /* Populate pixels: R=100, G=110, B=120, A=200 */
    for (size_t i = 0; i < sz; i += 4) {
        img.data[i + 0] = 100; /* R */
        img.data[i + 1] = 110; /* G */
        img.data[i + 2] = 120; /* B */
        img.data[i + 3] = 200; /* A */
    }

    /* Apply invert + 50% opacity */
    apply_effects(&img, 0.5f, 1);

    /* After invert: R=155, G=145, B=135. Alpha = 200 * 0.5 = 100 */
    assert(img.data[0] == 155);
    assert(img.data[1] == 145);
    assert(img.data[2] == 135);
    assert(img.data[3] == 100);

    free_overlay(&img);

    printf("test_overlay: OK\n");
    return 0;
}
