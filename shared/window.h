#ifndef WINDOW_H
#define WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cross-platform window abstraction for overlay display */

typedef struct OverlayWindow OverlayWindow;

/* Window creation and lifecycle */
OverlayWindow* create_overlay_window(int width, int height);
void destroy_overlay_window(OverlayWindow* window);

/* Window visibility */
void show_overlay_window(OverlayWindow* window);
void hide_overlay_window(OverlayWindow* window);

/* Window positioning */
void set_overlay_position(OverlayWindow* window, int x, int y);

/* Window properties */
void set_overlay_opacity(OverlayWindow* window, float opacity);
void set_overlay_click_through(OverlayWindow* window, int enabled);
void set_overlay_always_on_top(OverlayWindow* window, int enabled);

/* Window content */
void update_overlay_content(OverlayWindow* window, const unsigned char* rgba_data,
                           int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */
