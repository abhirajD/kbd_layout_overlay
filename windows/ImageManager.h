#ifndef IMAGE_MANAGER_H
#define IMAGE_MANAGER_H

#include "../shared/config.h"
#include "../shared/overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

int image_manager_init(Config *config);
void image_manager_cleanup(void);
int image_manager_load_overlay(void);
int image_manager_reload_if_needed(void);
void image_manager_apply_effects(void);
Overlay* image_manager_get_overlay(void);
int image_manager_get_dimensions(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_MANAGER_H
