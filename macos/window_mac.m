#import "window_mac.h"
#import "OverlayWindow.h"
#import <Cocoa/Cocoa.h>

struct OverlayWindow {
    NSWindow *window;
    NSImageView *imageView;
    NSImage *overlayImage;
    int width;
    int height;
};

OverlayWindow* create_overlay_window(int width, int height) {
    OverlayWindow *overlay = calloc(1, sizeof(OverlayWindow));
    if (!overlay) return NULL;

    overlay->width = width;
    overlay->height = height;

    // Get screen dimensions for scaling
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    NSSize size = NSMakeSize(width / scale, height / scale);

    // Create window rect (will be positioned later)
    NSRect rect = NSMakeRect(0, 0, size.width, size.height);

    // Create OverlayWindow for proper non-activating behavior
    overlay->window = [[OverlayWindow alloc] initWithContentRect:rect
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];

    if (!overlay->window) {
        free(overlay);
        return NULL;
    }

    // Configure window properties
    [overlay->window setOpaque:NO];
    [overlay->window setBackgroundColor:[NSColor clearColor]];
    [overlay->window setHasShadow:NO];

    // Set proper collection behavior for multi-space support
    [overlay->window setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                           NSWindowCollectionBehaviorFullScreenAuxiliary |
                                           NSWindowCollectionBehaviorIgnoresCycle)];

    // Create image view
    overlay->imageView = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    [overlay->window setContentView:overlay->imageView];

    return overlay;
}

void destroy_overlay_window(OverlayWindow* window) {
    if (!window) return;

    [window->window close];
    [window->imageView release];
    [window->overlayImage release];
    [window->window release];

    free(window);
}

void show_overlay_window(OverlayWindow* window) {
    if (!window || !window->window) return;

    [window->window orderFrontRegardless];
}

void hide_overlay_window(OverlayWindow* window) {
    if (!window || !window->window) return;

    [window->window orderOut:nil];
}

void set_overlay_position(OverlayWindow* window, int x, int y) {
    if (!window || !window->window) return;

    NSRect frame = [window->window frame];
    frame.origin.x = x;
    frame.origin.y = y;

    [window->window setFrame:frame display:YES];
}

void set_overlay_opacity(OverlayWindow* window, float opacity) {
    if (!window || !window->window) return;

    [window->window setAlphaValue:opacity];
}

void set_overlay_click_through(OverlayWindow* window, int enabled) {
    if (!window || !window->window) return;

    if (enabled) {
        [window->window setIgnoresMouseEvents:YES];
    } else {
        [window->window setIgnoresMouseEvents:NO];
    }
}

void set_overlay_always_on_top(OverlayWindow* window, int enabled) {
    if (!window || !window->window) return;

    if (enabled) {
        [window->window setLevel:NSScreenSaverWindowLevel];
    } else {
        [window->window setLevel:NSNormalWindowLevel];
    }
}

void update_overlay_content(OverlayWindow* window, const unsigned char* rgba_data,
                           int width, int height) {
    if (!window || !rgba_data) return;

    // Create NSBitmapImageRep from RGBA data
    unsigned char *planes[1] = {(unsigned char*)rgba_data};
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:planes
                      pixelsWide:width
                      pixelsHigh:height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:width * 4
                    bitsPerPixel:32];

    // Create NSImage
    NSImage *image = [[NSImage alloc] init];
    [image addRepresentation:bitmap];

    // Scale for screen
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    [image setSize:NSMakeSize(width / scale, height / scale)];

    // Update window content
    [window->imageView setImage:image];

    // Clean up old image
    if (window->overlayImage) {
        [window->overlayImage release];
    }
    window->overlayImage = image;

    [bitmap release];
}
