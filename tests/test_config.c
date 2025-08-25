#include "../shared/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

static int float_eq(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

int main(void) {
    const char *path = "/tmp/klo_test_config.json";

    /* Test 1: save and load roundtrip */
    {
        Config c = get_default_config();
        c.auto_hide = 2.0f;
        c.position_mode = 1;
        c.click_through = 1;
        c.monitor_index = 1;
        const char *hk = "Command+Option+K";
        strncpy(c.hotkey, hk, sizeof(c.hotkey)-1);
        c.hotkey[sizeof(c.hotkey)-1] = '\0';

        if (!save_config(&c, path)) {
            fprintf(stderr, "Test1: save_config failed\n");
            return 2;
        }

        Config out = get_default_config();
        int r = load_config(&out, path);
        if (r <= 0) {
            fprintf(stderr, "Test1: load_config failed (r=%d)\n", r);
            unlink(path);
            return 3;
        }

        if (!float_eq(out.auto_hide, 2.0f) || out.position_mode != 1 || out.click_through != 1 || out.monitor_index != 1 || strcmp(out.hotkey, hk) != 0) {
            fprintf(stderr, "Test1: mismatch after load: auto_hide=%.3f pos=%d click=%d monitor=%d hotkey=%s\n",
                    out.auto_hide, out.position_mode, out.click_through, out.monitor_index, out.hotkey);
            unlink(path);
            return 4;
        }

        unlink(path);
        printf("Test1: passed\n");
    }

    /* Test 2: legacy migration - persistent -> auto_hide == 0.0 */
    {
        Config m = get_default_config();
        m.persistent = 1;
        m.auto_hide = 0.8f; /* intentionally non-zero; migration should override when loading */
        if (!save_config(&m, path)) {
            fprintf(stderr, "Test2: save_config failed\n");
            return 10;
        }

        Config out = get_default_config();
        int r = load_config(&out, path);
        if (r <= 0) {
            fprintf(stderr, "Test2: load_config failed (r=%d)\n", r);
            unlink(path);
            return 11;
        }

        if (!float_eq(out.auto_hide, 0.0f)) {
            fprintf(stderr, "Test2: migration failed - expected auto_hide==0.0 got %.3f (persistent=%d)\n", out.auto_hide, out.persistent);
            unlink(path);
            return 12;
        }

        unlink(path);
        printf("Test2: passed\n");
    }

    printf("All config tests passed\n");
    return 0;
}
