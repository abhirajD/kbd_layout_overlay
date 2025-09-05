#import "ImageManager.h"
#import <Cocoa/Cocoa.h>
#import "../shared/config.h"
#import "../shared/overlay.h"
#import "../shared/log.h"

@interface ImageManager () {
    Config _config;
    Overlay _overlay;
    unsigned char *_originalImageData;
    int _originalImageSize;
    float _lastScale;
    int _lastCustomWidth;
    int _lastCustomHeight;
    int _lastUseCustom;
}
@end

@implementation ImageManager

- (instancetype)initWithConfig:(Config)config {
    self = [super init];
    if (self) {
        _config = config;
        _originalImageData = NULL;
        _originalImageSize = 0;
        _lastScale = -1.0f;
        _lastCustomWidth = -1;
        _lastCustomHeight = -1;
        _lastUseCustom = -1;
    }
    return self;
}

- (void)dealloc {
    if (_originalImageData) {
        free(_originalImageData);
        _originalImageData = NULL;
    }
    free_overlay(&_overlay);
}

- (BOOL)loadOverlay {
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

    if (!_originalImageData) {
        /* Try multiple locations for keymap.png */
        NSArray *searchPaths = @[
            @"keymap.png",
            @"assets/keymap.png",
            @"../assets/keymap.png",
            [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"] ?: @"",
        ];

        for (NSString *pathStr in searchPaths) {
            if ([pathStr length] == 0) continue;
            NSData *d = [NSData dataWithContentsOfFile:pathStr];
            if (d) {
                _originalImageSize = (int)[d length];
                _originalImageData = malloc(_originalImageSize);
                if (_originalImageData) {
                    memcpy(_originalImageData, [d bytes], _originalImageSize);
                    logger_log("Loaded overlay from: %s", [pathStr UTF8String]);
                    break;
                }
            }
        }

        if (!_originalImageData) {
            int size;
            const unsigned char *data = get_default_keymap(&size);
            if (data && size > 0) {
                _originalImageData = malloc(size);
                if (_originalImageData) {
                    memcpy(_originalImageData, data, size);
                    _originalImageSize = size;
                    logger_log("Using embedded keymap (build-time)");
                }
            }
        }
    }

    if (!_originalImageData || _originalImageSize <= 0) {
        logger_log("Failed to load overlay image");
        return NO;
    }

    OverlayError result = load_overlay_mem(_originalImageData, _originalImageSize,
                                           max_w, max_h, &_overlay);

    if (result != OVERLAY_OK) {
        logger_log("Failed to process overlay: %d", result);
        return NO;
    }

    apply_effects(&_overlay, _config.opacity, _config.invert);
    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;

    return YES;
}

- (BOOL)reloadOverlayIfNeeded {
    if (fabsf(_config.scale - _lastScale) < 0.001f &&
        _config.custom_width_px == _lastCustomWidth &&
        _config.custom_height_px == _lastCustomHeight &&
        _config.use_custom_size == _lastUseCustom) {
        return YES; /* No changes needed */
    }

    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;

    free_overlay(&_overlay);
    return [self loadOverlay];
}

- (const unsigned char *)getOverlayData {
    return _overlay.data;
}

- (int)getOverlayWidth {
    return _overlay.width;
}

- (int)getOverlayHeight {
    return _overlay.height;
}

- (void)updateConfig:(Config)newConfig {
    _config = newConfig;
}

@end
