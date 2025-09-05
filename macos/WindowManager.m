#import "WindowManager.h"
#import "OverlayWindow.h"
#import <Cocoa/Cocoa.h>
#import "../shared/config.h"
#import "../shared/overlay.h"
#import "../shared/log.h"

@interface WindowManager () {
    OverlayWindow *_panel;
    NSImageView *_imageView;
    NSImage *_overlayImage;
    BOOL _visible;
    Config _config;
    Overlay _overlay;
    unsigned char *_previewBuffer;
    size_t _previewBufferSize;
    float _lastScale;
    int _lastCustomWidth;
    int _lastCustomHeight;
    int _lastUseCustom;
}
@end

@implementation WindowManager

- (instancetype)initWithConfig:(Config)config {
    self = [super init];
    if (self) {
        _config = config;
        _visible = NO;
        _previewBuffer = NULL;
        _previewBufferSize = 0;
        _lastScale = -1.0f;
        _lastCustomWidth = -1;
        _lastCustomHeight = -1;
        _lastUseCustom = -1;
    }
    return self;
}

- (void)dealloc {
    [self cleanup];
}

- (void)cleanup {
    if (_previewBuffer) {
        free(_previewBuffer);
        _previewBuffer = NULL;
        _previewBufferSize = 0;
    }
    free_overlay(&_overlay);
}

- (BOOL)createOverlay {
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    int max_w = 0;
    int max_h = 0;

    if (_config.use_custom_size) {
        max_w = _config.custom_width_px > 0 ? _config.custom_width_px : (int)([screen frame].size.width * scale * _config.scale);
        max_h = _config.custom_height_px > 0 ? _config.custom_height_px : (int)([screen frame].size.height * scale * _config.scale);
    } else {
        max_w = (int)([screen frame].size.width * scale * _config.scale);
        max_h = (int)([screen frame].size.height * scale * _config.scale);
    }

    OverlayError result = load_overlay_mem(NULL, 0, max_w, max_h, &_overlay);
    if (result != OVERLAY_OK) {
        logger_log("Failed to create overlay: %d", result);
        return NO;
    }

    apply_effects(&_overlay, _config.opacity, _config.invert);
    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;

    [self createOverlayWindow];
    return YES;
}

- (void)createOverlayWindow {
    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    NSSize size = NSMakeSize(_overlay.width / scale, _overlay.height / scale);

    /* Compute initial position based on persisted config */
    NSScreen *screen = [NSScreen mainScreen];
    NSRect screenFrame = [screen visibleFrame];
    NSRect rect = NSMakeRect(0, 0, size.width, size.height);
    switch (_config.position_mode) {
        case 0: /* Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMidY(screenFrame) - size.height / 2.0;
            break;
        case 1: /* Top-Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMaxY(screenFrame) - size.height - (_config.position_y);
            break;
        case 2: /* Bottom-Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMinY(screenFrame) + (_config.position_y);
            break;
        case 3: /* Custom */
        default:
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0 + _config.position_x;
            rect.origin.y = NSMinY(screenFrame) + _config.position_y;
            break;
    }

    /* Use OverlayWindow for proper non-activating behavior */
    _panel = [[OverlayWindow alloc] initWithContentRect:rect
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];

    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]];
    [_panel setHasShadow:NO];

    /* Window level follows persisted always_on_top flag */
    if (_config.always_on_top) {
        [_panel setLevel:NSScreenSaverWindowLevel];
    } else {
        [_panel setLevel:NSNormalWindowLevel];
    }

    /* Click-through */
    if (_config.click_through) {
        [_panel setIgnoresMouseEvents:YES];
    } else {
        [_panel setIgnoresMouseEvents:NO];
    }

    /* Opacity */
    [_panel setAlphaValue:_config.opacity];

    /* Set proper collection behavior for multi-space support */
    [_panel setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                   NSWindowCollectionBehaviorFullScreenAuxiliary |
                                   NSWindowCollectionBehaviorIgnoresCycle)];

    /* Simple image view */
    _imageView = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    [_imageView setImage:_overlayImage];
    [_panel setContentView:_imageView];
}

- (void)showOverlay {
    if (_visible || !_panel) return;

    /* Use fixed center position for testing */
    NSScreen *screen = [NSScreen mainScreen];
    NSRect screenFrame = [screen visibleFrame];
    NSRect panelFrame = [_panel frame];

    /* Center the panel on screen */
    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
    panelFrame.origin.y = NSMidY(screenFrame) - NSHeight(panelFrame) / 2.0;

    [_panel setFrame:panelFrame display:YES];
    [_panel orderFrontRegardless];
    _visible = YES;
}

- (void)hideOverlay {
    if (!_visible || !_panel) return;

    [_panel orderOut:nil];
    _visible = NO;
}

- (void)toggleOverlay {
    if (_visible) {
        [self hideOverlay];
    } else {
        [self showOverlay];
    }
}

- (void)updateOverlayImage {
    /* Ensure we run on main thread when updating AppKit objects */
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateOverlayImage];
        });
        return;
    }

    /* Allocate or resize preview buffer as needed */
    size_t pixelCount = (size_t)_overlay.width * (size_t)_overlay.height * 4;
    if (_previewBufferSize < pixelCount || !_previewBuffer) {
        _previewBuffer = realloc(_previewBuffer, pixelCount);
        _previewBufferSize = pixelCount;
    }
    if (!_previewBuffer) {
        logger_log("Failed to allocate preview buffer");
        return;
    }

    /* Copy original pixels into preview buffer and apply effects to the copy */
    memcpy(_previewBuffer, _overlay.data, pixelCount);
    Overlay tmp;
    tmp.data = _previewBuffer;
    tmp.width = _overlay.width;
    tmp.height = _overlay.height;
    tmp.channels = _overlay.channels;
    tmp.cached_effects = 0;
    tmp.cached_opacity = 0.0f;
    tmp.cached_invert = 0;

    apply_effects(&tmp, _config.opacity, _config.invert);

    /* Create NSImage from preview buffer */
    unsigned char *planes[1] = { _previewBuffer };
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:planes
                      pixelsWide:tmp.width
                      pixelsHigh:tmp.height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:tmp.width * 4
                    bitsPerPixel:32];

    NSImage *image = [[NSImage alloc] init];
    [image addRepresentation:bitmap];

    // Scale for screen
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    [image setSize:NSMakeSize(tmp.width / scale, tmp.height / scale)];

    // Update window content
    [_imageView setImage:image];

    // Update image reference
    _overlayImage = image;
}

- (void)reloadOverlayIfNeeded {
    if (fabsf(_config.scale - _lastScale) < 0.001f &&
        _config.custom_width_px == _lastCustomWidth &&
        _config.custom_height_px == _lastCustomHeight &&
        _config.use_custom_size == _lastUseCustom) {
        return; /* No changes */
    }

    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;

    free_overlay(&_overlay);
    [self createOverlayWindow];
    [self updateOverlayImage];
}

- (BOOL)isVisible {
    return _visible;
}

- (void)updateConfig:(Config)newConfig {
    _config = newConfig;
}

@end
