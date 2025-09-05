#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Color representation
typedef struct {
    uint8_t r, g, b, a;
} Color;

// Rectangle structure
typedef struct {
    int x, y, width, height;
} Rect;

// Point structure
typedef struct {
    int x, y;
} Point;

// Size structure
typedef struct {
    int width, height;
} Size;

// Image/pixel buffer
typedef struct {
    uint8_t* data;
    int width, height;
    int channels;  // 3 for RGB, 4 for RGBA
    size_t data_size;
} ImageBuffer;

// Graphics context (opaque)
typedef struct GraphicsContext GraphicsContext;

// Font handle (opaque)
typedef struct FontHandle FontHandle;

// Texture handle (opaque)
typedef struct TextureHandle TextureHandle;

// Graphics system interface
typedef struct {
    // Context management
    GraphicsContext* (*create_context)(void* native_window);
    void (*destroy_context)(GraphicsContext* context);
    bool (*make_current)(GraphicsContext* context);
    void (*swap_buffers)(GraphicsContext* context);
    void (*set_viewport)(GraphicsContext* context, int x, int y, int width, int height);

    // Drawing operations
    void (*clear)(GraphicsContext* context, Color color);
    void (*draw_rect)(GraphicsContext* context, Rect rect, Color color);
    void (*draw_line)(GraphicsContext* context, Point start, Point end, Color color);
    void (*draw_circle)(GraphicsContext* context, Point center, int radius, Color color);

    // Image/texture operations
    TextureHandle* (*create_texture)(GraphicsContext* context, const ImageBuffer* image);
    void (*destroy_texture)(TextureHandle* texture);
    void (*draw_texture)(GraphicsContext* context, TextureHandle* texture, Rect src_rect, Rect dst_rect);
    void (*update_texture)(GraphicsContext* context, TextureHandle* texture, const ImageBuffer* image);

    // Text rendering
    FontHandle* (*load_font)(GraphicsContext* context, const char* font_path, int size);
    void (*unload_font)(FontHandle* font);
    void (*draw_text)(GraphicsContext* context, FontHandle* font, const char* text,
                     Point position, Color color);

    // Blending and effects
    void (*set_blend_mode)(GraphicsContext* context, int blend_mode);
    void (*set_alpha)(GraphicsContext* context, float alpha);
    void (*push_transform)(GraphicsContext* context);
    void (*pop_transform)(GraphicsContext* context);
    void (*translate)(GraphicsContext* context, float x, float y);
    void (*scale)(GraphicsContext* context, float x, float y);
    void (*rotate)(GraphicsContext* context, float angle);

    // Utility functions
    Size (*get_context_size)(GraphicsContext* context);
    bool (*is_context_valid)(GraphicsContext* context);
} GraphicsSystem;

// Blend modes
typedef enum {
    BLEND_MODE_NONE,
    BLEND_MODE_ALPHA,
    BLEND_MODE_ADDITIVE,
    BLEND_MODE_MULTIPLY,
    BLEND_MODE_SCREEN
} BlendMode;

// Get platform-specific graphics system implementation
GraphicsSystem* get_graphics_system(void);

// Image buffer utility functions
ImageBuffer* create_image_buffer(int width, int height, int channels);
void destroy_image_buffer(ImageBuffer* buffer);
bool load_image_from_file(const char* path, ImageBuffer* buffer);
bool save_image_to_file(const char* path, const ImageBuffer* buffer);
void copy_image_buffer(const ImageBuffer* src, ImageBuffer* dst);
void resize_image_buffer(const ImageBuffer* src, ImageBuffer* dst, int new_width, int new_height);

// Color utility functions
Color color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
Color color_rgb(uint8_t r, uint8_t g, uint8_t b);
Color color_from_hex(uint32_t hex);

#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_H
