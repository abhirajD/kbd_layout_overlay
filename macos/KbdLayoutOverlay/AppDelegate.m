#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import "OverlayView.h"
#import "../shared/config.h"
#import "../shared/overlay.h"
#import "../shared/hotkey.h"
#import "../shared/monitor.h"
#import "../shared/error.h"
#include <string.h>
#include <strings.h>
#include "../keymap_data.h"

@interface AppDelegate () {
    EventHotKeyRef _hotKeyRef;
    EventHandlerRef _eventHandler;
    NSPanel *_panel;
    OverlayView *_overlayView;
    NSStatusItem *_statusItem;
    Config _cfg;
    NSString *_configPath;
    Overlay _overlay;
    OverlayCache _cache;
    
    // Cached CGImages for each overlay variation
    NSMutableDictionary<NSString*, NSValue*> *_cgImageCache;
}
- (BOOL)isPersistent;
@end

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);
static void parseHotkeyCarbon(const char *hotkey, UInt32 *keyCode, UInt32 *mods);
static void onHotKeyPressed(AppDelegate *self);
static void onHotKeyReleased(AppDelegate *self);

static void parseHotkeyCarbon(const char *hotkey, UInt32 *keyCode, UInt32 *mods) {
    hotkey_t hk;
    parse_hotkey(hotkey, &hk);

    *mods = 0;
    if (hk.mods & HOTKEY_MOD_SUPER) *mods |= cmdKey;
    if (hk.mods & HOTKEY_MOD_CTRL) *mods |= controlKey;
    if (hk.mods & HOTKEY_MOD_ALT) *mods |= optionKey;
    if (hk.mods & HOTKEY_MOD_SHIFT) *mods |= shiftKey;

    if (hk.key >= 'A' && hk.key <= 'Z') *keyCode = kVK_ANSI_A + (hk.key - 'A');
    else if (hk.key >= '0' && hk.key <= '9') *keyCode = kVK_ANSI_0 + (hk.key - '0');
    else if (hk.key == '/') *keyCode = kVK_ANSI_Slash;
    else *keyCode = kVK_ANSI_Slash;
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self loadConfiguration];
    [self createOverlay];
    [self setupStatusItem];
    [self registerHotkey];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    if (_hotKeyRef) {
        UnregisterEventHotKey(_hotKeyRef);
    }
    if (_eventHandler) {
        RemoveEventHandler(_eventHandler);
    }
    [self cleanupCGImageCache];
    free_overlay_cache(&_cache);
    free_overlay(&_overlay);
    free_config(&_cfg);
}

- (void)createOverlay {
    const char *path = NULL;
    int triedLocal = 0;
    if (_cfg.overlay_path[0]) {
        path = _cfg.overlay_path;
    } else if ([[NSFileManager defaultManager] fileExistsAtPath:@"keymap.png"]) {
        path = "keymap.png";
        triedLocal = 1;
    } else {
        NSString *resPath = [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"];
        path = resPath ? [resPath fileSystemRepresentation] : "keymap.png";
    }
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scaleFactor = [screen backingScaleFactor];
    int max_w = (int)([screen frame].size.width * scaleFactor);
    int max_h = (int)([screen frame].size.height * scaleFactor);
    int r = load_overlay_image(path, max_w, max_h, &_overlay);
    if (r != OVERLAY_OK) {
        NSString *reason = (r == OVERLAY_ERR_NOT_FOUND) ? @"Overlay image not found" : @"Failed to decode overlay image";
        NSString *msg = [NSString stringWithFormat:@"%@: %s", reason, path];
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:msg];
        [alert runModal];
        if (!_cfg.overlay_path[0] && triedLocal) {
            NSString *resPath = [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"];
            if (resPath) {
                const char *res = [resPath fileSystemRepresentation];
                r = load_overlay_image(res, max_w, max_h, &_overlay);
            }
        }
        // If still failed, try embedded fallback
        if (r != OVERLAY_OK) {
            r = load_overlay_image_mem(keymap_png, keymap_png_len, max_w, max_h, &_overlay);
            if (r != OVERLAY_OK) {
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:@"Failed to load embedded overlay image"];
                [alert runModal];
                return;
            }
        }
    }
    
    // Initialize cache asynchronously for faster startup
    if (init_overlay_cache_async(&_cache, &_overlay) != OVERLAY_OK) {
        // Fallback to synchronous cache if async fails
        if (init_overlay_cache(&_cache, &_overlay) != OVERLAY_OK) {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Failed to initialize image cache"];
            [alert runModal];
            return;
        }
    }
    
    // Initialize CGImage cache for even faster performance
    [self initCGImageCache];

    CGFloat width = (CGFloat)_overlay.width / scaleFactor;
    CGFloat height = (CGFloat)_overlay.height / scaleFactor;
    NSRect rect = NSMakeRect(0, 0, width, height);

    _panel = [[NSPanel alloc] initWithContentRect:rect
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]];
    [_panel setLevel:NSStatusWindowLevel];
    [_panel setIgnoresMouseEvents:YES];

    _overlayView = [[OverlayView alloc] initWithFrame:rect];
    [self updateOverlayFromCache];
    [_panel setContentView:_overlayView];
}

- (NSMenu *)buildStatusMenu {
    NSMenu *menu = [[NSMenu alloc] init];
    NSMenuItem *startItem = [[NSMenuItem alloc] initWithTitle:@"Start at login" action:@selector(toggleAutostart:) keyEquivalent:@""];
    [startItem setTarget:self];
    [startItem setState:_cfg.autostart ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:startItem];
    NSMenuItem *persistentItem = [[NSMenuItem alloc] initWithTitle:@"Persistent mode" action:@selector(togglePersistent:) keyEquivalent:@""];
    [persistentItem setTarget:self];
    [persistentItem setState:_cfg.persistent ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:persistentItem];
    NSMenuItem *invertItem = [[NSMenuItem alloc] initWithTitle:@"Invert colors" action:@selector(toggleInvert:) keyEquivalent:@""];
    [invertItem setTarget:self];
    [invertItem setState:_cfg.invert ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:invertItem];
    NSMenuItem *opacityItem = [[NSMenuItem alloc] initWithTitle:@"Cycle opacity" action:@selector(cycleOpacity:) keyEquivalent:@""];
    [opacityItem setTarget:self];
    [menu addItem:opacityItem];
    // Show current monitor status
    NSString *monitorTitle;
    if (_cfg.monitor == 0) {
        monitorTitle = @"Cycle monitor (Auto)";
    } else {
        monitorTitle = [NSString stringWithFormat:@"Cycle monitor (%d)", _cfg.monitor];
    }
    NSMenuItem *monitorItem = [[NSMenuItem alloc] initWithTitle:monitorTitle action:@selector(cycleMonitor:) keyEquivalent:@""];
    [monitorItem setTarget:self];
    [menu addItem:monitorItem];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(quit:) keyEquivalent:@""];
    [quitItem setTarget:self];
    [menu addItem:quitItem];
    return menu;
}

- (NSScreen *)targetScreen {
    MonitorInfo info;
    int result = -1;
    
    if (_cfg.monitor == 0) {
        // Auto: use active monitor
        result = get_active_monitor(&info);
    } else if (_cfg.monitor == 1) {
        // Primary monitor
        result = get_primary_monitor(&info);
    } else {
        // Specific monitor (2, 3, etc.)
        result = get_monitor_info(_cfg.monitor, &info);
    }
    
    if (result != 0) {
        // Fallback to primary monitor
        if (get_primary_monitor(&info) != 0) {
            return [NSScreen mainScreen];
        }
    }
    
    // Convert MonitorInfo to NSScreen
    NSArray *screens = [NSScreen screens];
    for (NSScreen *screen in screens) {
        NSRect frame = [screen frame];
        if ((int)frame.origin.x == info.x && (int)frame.origin.y == info.y &&
            (int)frame.size.width == info.width && (int)frame.size.height == info.height) {
            return screen;
        }
    }
    
    return [NSScreen mainScreen];
}

- (void)showPanel {
    if ([_panel isVisible]) return;
    NSScreen *screen = [self targetScreen];
    NSRect frame = _panel.frame;
    NSRect screenFrame = [screen visibleFrame];
    frame.origin.x = NSMidX(screenFrame) - NSWidth(frame) / 2.0;
    frame.origin.y = NSMinY(screenFrame);
    [_panel setFrame:frame display:NO];
    [_panel orderFront:nil];
}

- (void)hidePanel {
    if ([_panel isVisible]) {
        [_panel orderOut:nil];
    }
}

- (void)togglePanel {
    if ([_panel isVisible]) {
        [self hidePanel];
    } else {
        [self showPanel];
    }
}

- (void)setupStatusItem {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    _statusItem.button.title = @"KLO";
    _statusItem.menu = [self buildStatusMenu];
}

- (void)toggleAutostart:(id)sender {
    _cfg.autostart = !_cfg.autostart;
    [sender setState:_cfg.autostart ? NSControlStateValueOn : NSControlStateValueOff];
    [self setAutostart:_cfg.autostart];
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (void)togglePersistent:(id)sender {
    _cfg.persistent = !_cfg.persistent;
    [sender setState:_cfg.persistent ? NSControlStateValueOn : NSControlStateValueOff];
    // Hide overlay if switching to persistent mode while visible
    if (_cfg.persistent && [_panel isVisible]) {
        [self hidePanel];
    }
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

- (void)toggleInvert:(id)sender {
    _cfg.invert = !_cfg.invert;
    [sender setState:_cfg.invert ? NSControlStateValueOn : NSControlStateValueOff];
    [self updateOverlayFromCache];
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (void)cycleOpacity:(id)sender {
    float levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
    int count = sizeof(levels) / sizeof(levels[0]);
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (_cfg.opacity <= levels[i] + 0.001f) {
            next = (i + 1) % count;
            break;
        }
    }
    _cfg.opacity = levels[next];
    [self updateOverlayFromCache];
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (void)cycleMonitor:(id)sender {
    // Cycle through monitors: 0=auto, 1=primary, 2=secondary, etc.
    int monitor_count = get_monitor_count();
    _cfg.monitor = (_cfg.monitor + 1) % (monitor_count + 1); // +1 for auto mode
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (BOOL)isPersistent {
    return _cfg.persistent;
}

- (NSString *)cacheKeyForOpacity:(float)opacity invert:(int)invert {
    return [NSString stringWithFormat:@"%.3f_%d", opacity, invert];
}

- (void)initCGImageCache {
    _cgImageCache = [[NSMutableDictionary alloc] init];
    
    // If async cache generation is complete, pre-generate CGImages
    if (_cache.async_generation_complete) {
        [self generateCGImagesFromCache];
    }
    // If not complete, CGImages will be generated on-demand in updateOverlayFromCache
}

static void provider_release(void *info, const void *data, size_t size) {
    (void)data;
    (void)size;
    if (info) free(info);
}

- (void)generateCGImagesFromCache {
    // Pre-generate CGImages for all cached variations
    for (int i = 0; i < _cache.count; i++) {
        const Overlay *variation = &_cache.variations[i];
        float opacity = _cache.opacity_levels[i];
        int invert = _cache.invert_flags[i];
        
        size_t data_size = (size_t)variation->width * variation->height * 4;
        void *owned_buf = malloc(data_size);
        if (!owned_buf) continue;
        memcpy(owned_buf, variation->data, data_size);
        
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider = CGDataProviderCreateWithData(owned_buf, owned_buf,
            data_size, provider_release);
        if (!provider) {
            CGColorSpaceRelease(colorSpace);
            free(owned_buf);
            continue;
        }
        
        CGImageRef cgImage = CGImageCreate(variation->width, variation->height, 8, 32,
            variation->width * 4, colorSpace,
            kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast,
            provider, NULL, false, kCGRenderingIntentDefault);
        
        if (cgImage) {
            NSString *key = [self cacheKeyForOpacity:opacity invert:invert];
            _cgImageCache[key] = [NSValue valueWithPointer:cgImage];
        }
        
        CGColorSpaceRelease(colorSpace);
        CGDataProviderRelease(provider);
    }
}

- (void)cleanupCGImageCache {
    for (NSValue *value in [_cgImageCache allValues]) {
        CGImageRef cgImage = (CGImageRef)[value pointerValue];
        if (cgImage) {
            CGImageRelease(cgImage);
        }
    }
    [_cgImageCache removeAllObjects];
}

- (void)updateOverlayFromCache {
    NSString *key = [self cacheKeyForOpacity:_cfg.opacity invert:_cfg.invert];
    NSValue *value = _cgImageCache[key];
    
    if (value) {
        // Use pre-generated CGImage - super fast!
        CGImageRef cgImage = (CGImageRef)[value pointerValue];
        [_overlayView setPrecomputedImage:cgImage];
    } else {
        // Check if async cache generation completed since last time
        if (_cache.async_generation_complete && [_cgImageCache count] == 0) {
            // Cache is ready but CGImages not generated yet - do it now
            [self generateCGImagesFromCache];
            
            // Try again with the newly generated CGImage
            value = _cgImageCache[key];
            if (value) {
                CGImageRef cgImage = (CGImageRef)[value pointerValue];
                [_overlayView setPrecomputedImage:cgImage];
                return;
            }
        }
        
        // Fallback to cached overlay or original image
        const Overlay *cached = get_cached_variation(&_cache, _cfg.opacity, _cfg.invert);
        if (cached) {
            [_overlayView setImageData:cached->data width:cached->width height:cached->height];
        } else {
            // Cache not ready yet - use original image temporarily
            [_overlayView setImageData:_overlay.data width:_overlay.width height:_overlay.height];
        }
    }
}

- (void)setAutostart:(BOOL)enable {
    NSString *src = [[NSBundle mainBundle] pathForResource:@"com.example.kbdlayoutoverlay" ofType:@"plist"]; 
    NSString *dst = [@"~/Library/LaunchAgents/com.example.kbdlayoutoverlay.plist" stringByExpandingTildeInPath];
    if (enable) {
        [[NSFileManager defaultManager] copyItemAtPath:src toPath:dst error:nil];
        NSTask *task = [[NSTask alloc] init];
        [task setLaunchPath:@"/bin/launchctl"];
        [task setArguments:@[@"load", dst]];
        [task launch];
    } else {
        NSTask *task = [[NSTask alloc] init];
        [task setLaunchPath:@"/bin/launchctl"];
        [task setArguments:@[@"unload", dst]];
        [task launch];
        [[NSFileManager defaultManager] removeItemAtPath:dst error:nil];
    }
}

/* macOS-specific error handler */
static void macos_error_handler(const klo_error_context_t *ctx) {
    NSString *message = [NSString stringWithFormat:@"%s\n\nFile: %s:%d\nFunction: %s",
                        ctx->message, ctx->file, ctx->line, ctx->function];
    
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Keyboard Layout Overlay Error"];
    [alert setInformativeText:message];
    [alert setAlertStyle:NSAlertStyleCritical];
    [alert runModal];
}

// Load configuration from disk or create defaults if missing
- (void)loadConfiguration {
    _configPath = [@"~/Library/Preferences/kbd_layout_overlay.cfg" stringByExpandingTildeInPath];
    
    /* Initialize error handling first */
    klo_error_init(macos_error_handler);
    
    /* Initialize with defaults first */
    klo_error_t err = init_config(&_cfg);
    if (err != KLO_OK) {
        klo_log(KLO_LOG_FATAL, "Failed to initialize configuration: %s", klo_error_get_message());
        [NSApp terminate:nil];
        return;
    }
    
    /* Override with macOS-specific default hotkey */
    err = set_config_string(&_cfg.hotkey, "Command+Option+Shift+Slash");
    if (err != KLO_OK) {
        klo_log(KLO_LOG_FATAL, "Failed to set default hotkey: %s", klo_error_get_message());
        [NSApp terminate:nil];
        return;
    }
    
    /* Try to load from file, keep defaults if failed */
    err = load_config([_configPath fileSystemRepresentation], &_cfg);
    if (err != KLO_OK) {
        /* File doesn't exist or is corrupted, save defaults */
        klo_log(KLO_LOG_INFO, "Creating default configuration file");
        save_config([_configPath fileSystemRepresentation], &_cfg);
    }
    
    /* Validate hotkey field */
    if (!_cfg.hotkey || !_cfg.hotkey[0]) {
        err = set_config_string(&_cfg.hotkey, "Command+Option+Shift+Slash");
        if (err != KLO_OK) {
            klo_log(KLO_LOG_FATAL, "Failed to restore default hotkey: %s", klo_error_get_message());
            [NSApp terminate:nil];
            return;
        }
    }
}

// Register a global hotkey using the configured shortcut
- (void)registerHotkey {
    EventTypeSpec eventTypes[2];
    eventTypes[0].eventClass = kEventClassKeyboard;
    eventTypes[0].eventKind = kEventHotKeyPressed;
    eventTypes[1].eventClass = kEventClassKeyboard;
    eventTypes[1].eventKind = kEventHotKeyReleased;
    InstallApplicationEventHandler(&hotKeyHandler, 2, eventTypes, (__bridge void *)self, &_eventHandler);

    UInt32 keyCode = 0, mods = 0;
    parseHotkeyCarbon(_cfg.hotkey, &keyCode, &mods);
    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'kblo';
    hotKeyID.id = 1;
    RegisterEventHotKey(keyCode, mods, hotKeyID, GetApplicationEventTarget(), 0, &_hotKeyRef);
}

@end

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData) {
    AppDelegate *self = (__bridge AppDelegate *)userData;
    EventHotKeyID hkCom;
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hkCom), NULL, &hkCom);
    if (hkCom.id != 1) return noErr;
    UInt32 kind = GetEventKind(event);
    if (kind == kEventHotKeyPressed) {
        onHotKeyPressed(self);
    } else if (kind == kEventHotKeyReleased) {
        onHotKeyReleased(self);
    }
    return noErr;
}

static void onHotKeyPressed(AppDelegate *self) {
    if ([self isPersistent]) {
        [self togglePanel];
    } else {
        [self showPanel];
    }
}

static void onHotKeyReleased(AppDelegate *self) {
    if (![self isPersistent]) {
        [self hidePanel];
    }
}
