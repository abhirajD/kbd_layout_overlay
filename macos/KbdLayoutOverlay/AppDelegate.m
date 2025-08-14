#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import "OverlayView.h"
#import "../shared/config.h"
#import "../shared/overlay.h"
#include <string.h>
#include <strings.h>

@interface AppDelegate () {
    EventHotKeyRef _hotKeyRef;
    EventHandlerRef _eventHandler;
    NSPanel *_panel;
    OverlayView *_overlayView;
    NSStatusItem *_statusItem;
    Config _cfg;
    NSString *_configPath;
    Overlay _overlay;
}
@end

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);
static void parseHotkey(const char *hotkey, UInt32 *keyCode, UInt32 *mods);

static void parseHotkey(const char *hotkey, UInt32 *keyCode, UInt32 *mods) {
    *keyCode = kVK_ANSI_Slash;
    *mods = 0;
    if (!hotkey) return;
    char buf[256];
    strncpy(buf, hotkey, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    char *token = strtok(buf, "+");
    while (token) {
        if (!strcasecmp(token, "cmd") || !strcasecmp(token, "command") ||
            !strncasecmp(token, "meta", 4)) {
            *mods |= cmdKey;
        } else if (!strcasecmp(token, "ctrl") || !strcasecmp(token, "control") ||
                   !strncasecmp(token, "control", 7)) {
            *mods |= controlKey;
        } else if (!strcasecmp(token, "alt") || !strcasecmp(token, "option") ||
                   !strcasecmp(token, "opt")) {
            *mods |= optionKey;
        } else if (!strcasecmp(token, "shift")) {
            *mods |= shiftKey;
        } else {
            if (strlen(token) == 1) {
                char c = token[0];
                if (c >= 'a' && c <= 'z') *keyCode = kVK_ANSI_A + (c - 'a');
                else if (c >= 'A' && c <= 'Z') *keyCode = kVK_ANSI_A + (c - 'A');
                else if (c >= '0' && c <= '9') *keyCode = kVK_ANSI_0 + (c - '0');
                else if (c == '/') *keyCode = kVK_ANSI_Slash;
            } else if (!strcasecmp(token, "slash")) {
                *keyCode = kVK_ANSI_Slash;
            }
        }
        token = strtok(NULL, "+");
    }
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _configPath = [@"~/Library/Preferences/kbd_layout_overlay.cfg" stringByExpandingTildeInPath];
    if (load_config([_configPath fileSystemRepresentation], &_cfg) != 0) {
        strcpy(_cfg.overlay_path, "keymap.png");
        _cfg.opacity = 1.0f;
        _cfg.invert = 0;
        _cfg.autostart = 0;
        strcpy(_cfg.hotkey, "Command+Option+Shift+Slash");
        _cfg.persistent = 0;
        save_config([_configPath fileSystemRepresentation], &_cfg);
    }
    if (!_cfg.hotkey[0]) {
        strcpy(_cfg.hotkey, "Command+Option+Shift+Slash");
    }

    [self createOverlay];
    [self setupStatusItem];

    EventTypeSpec eventTypes[2];
    eventTypes[0].eventClass = kEventClassKeyboard;
    eventTypes[0].eventKind = kEventHotKeyPressed;
    eventTypes[1].eventClass = kEventClassKeyboard;
    eventTypes[1].eventKind = kEventHotKeyReleased;
    InstallApplicationEventHandler(&hotKeyHandler, 2, eventTypes, (__bridge void *)self, &_eventHandler);

    UInt32 keyCode = 0, mods = 0;
    parseHotkey(_cfg.hotkey, &keyCode, &mods);
    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'kblo';
    hotKeyID.id = 1;
    RegisterEventHotKey(keyCode, mods, hotKeyID, GetApplicationEventTarget(), 0, &_hotKeyRef);

    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    if (_hotKeyRef) {
        UnregisterEventHotKey(_hotKeyRef);
    }
    if (_eventHandler) {
        RemoveEventHandler(_eventHandler);
    }
    free_overlay(&_overlay);
}

- (void)createOverlay {
    const char *path = NULL;
    if (_cfg.overlay_path[0]) {
        path = _cfg.overlay_path;
    } else if ([[NSFileManager defaultManager] fileExistsAtPath:@"keymap.png"]) {
        path = "keymap.png";
    } else {
        NSString *resPath = [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"];
        path = resPath ? [resPath fileSystemRepresentation] : "keymap.png";
    }
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scaleFactor = [screen backingScaleFactor];
    int max_w = (int)([screen frame].size.width * scaleFactor);
    int max_h = (int)([screen frame].size.height * scaleFactor);
    if (load_overlay_image(path, max_w, max_h, &_overlay) != 0) {
        NSLog(@"Failed to load %s", path);
        return;
    }
    apply_opacity_inversion(&_overlay, _cfg.opacity, _cfg.invert);

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
    [_overlayView setImageData:_overlay.data width:_overlay.width height:_overlay.height];
    [_panel setContentView:_overlayView];
}

- (NSScreen *)targetScreen {
    NSScreen *screen = nil;
    NSRunningApplication *activeApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (activeApp) {
        CFArrayRef windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
                                                          kCGWindowListExcludeDesktopElements,
                                                          kCGNullWindowID);
        NSArray *windows = CFBridgingRelease(windowList);
        for (NSDictionary *window in windows) {
            if ([window[(id)kCGWindowOwnerPID] intValue] != activeApp.processIdentifier) {
                continue;
            }
            if ([window[(id)kCGWindowLayer] intValue] != 0) {
                continue;
            }
            CGRect bounds;
            CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)window[(id)kCGWindowBounds], &bounds);
            for (NSScreen *s in [NSScreen screens]) {
                if (CGRectIntersectsRect(bounds, NSRectToCGRect([s frame]))) {
                    screen = s;
                    break;
                }
            }
            if (screen) break;
        }
    }
    if (!screen) {
        NSPoint mouse = [NSEvent mouseLocation];
        for (NSScreen *s in [NSScreen screens]) {
            if (NSPointInRect(mouse, [s frame])) {
                screen = s;
                break;
            }
        }
    }
    return screen ?: [NSScreen mainScreen];
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
    NSMenu *menu = [[NSMenu alloc] init];
    NSMenuItem *startItem = [[NSMenuItem alloc] initWithTitle:@"Start at login" action:@selector(toggleAutostart:) keyEquivalent:@""];
    [startItem setTarget:self];
    [startItem setState:_cfg.autostart ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:startItem];
    NSMenuItem *invertItem = [[NSMenuItem alloc] initWithTitle:@"Invert colors" action:@selector(toggleInvert:) keyEquivalent:@""];
    [invertItem setTarget:self];
    [invertItem setState:_cfg.invert ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:invertItem];
    NSMenuItem *opacityItem = [[NSMenuItem alloc] initWithTitle:@"Cycle opacity" action:@selector(cycleOpacity:) keyEquivalent:@""];
    [opacityItem setTarget:self];
    [menu addItem:opacityItem];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(quit:) keyEquivalent:@""];
    [quitItem setTarget:self];
    [menu addItem:quitItem];
    _statusItem.menu = menu;
}

- (void)toggleAutostart:(id)sender {
    _cfg.autostart = !_cfg.autostart;
    [sender setState:_cfg.autostart ? NSControlStateValueOn : NSControlStateValueOff];
    [self setAutostart:_cfg.autostart];
    save_config([_configPath fileSystemRepresentation], &_cfg);
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

- (void)toggleInvert:(id)sender {
    _cfg.invert = !_cfg.invert;
    [sender setState:_cfg.invert ? NSControlStateValueOn : NSControlStateValueOff];
    apply_opacity_inversion(&_overlay, _cfg.opacity, _cfg.invert);
    [_overlayView setImageData:_overlay.data width:_overlay.width height:_overlay.height];
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
    apply_opacity_inversion(&_overlay, _cfg.opacity, _cfg.invert);
    [_overlayView setImageData:_overlay.data width:_overlay.width height:_overlay.height];
    save_config([_configPath fileSystemRepresentation], &_cfg);
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

@end

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData) {
    AppDelegate *self = (__bridge AppDelegate *)userData;
    EventHotKeyID hkCom;
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hkCom), NULL, &hkCom);
    if (hkCom.id != 1) return noErr;
    UInt32 kind = GetEventKind(event);
    if (kind == kEventHotKeyPressed) {
        if (self->_cfg.persistent) {
            [self togglePanel];
        } else {
            [self showPanel];
        }
    } else if (kind == kEventHotKeyReleased) {
        if (!self->_cfg.persistent) {
            [self hidePanel];
        }
    }
    return noErr;
}
