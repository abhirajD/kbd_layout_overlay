#ifndef IMAGE_MANAGER_H
#define IMAGE_MANAGER_H

struct Config;
struct Overlay;

#ifdef __cplusplus
extern "C" {
#endif

int image_manager_init(struct Config *config);
void image_manager_cleanup(void);
int image_manager_load_overlay(void);
int image_manager_reload_if_needed(void);
void image_manager_apply_effects(void);
struct Overlay* image_manager_get_overlay(void);
int image_manager_get_dimensions(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_MANAGER_H
