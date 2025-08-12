#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import "OverlayView.h"
#import "../shared/config.h"
#import "../shared/overlay.h"
#include <string.h>

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);

@implementation AppDelegate {
    EventHotKeyRef _hotKeyRef;
    EventHandlerRef _eventHandler;
    NSPanel *_panel;
    OverlayView *_overlayView;
    NSStatusItem *_statusItem;
    Config _cfg;
    NSString *_configPath;
    Overlay _overlay;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _configPath = [@"~/Library/Preferences/kbd_layout_overlay.cfg" stringByExpandingTildeInPath];
    if (load_config([_configPath fileSystemRepresentation], &_cfg) != 0) {
        strcpy(_cfg.overlay_path, "keymap.png");
        _cfg.opacity = 1.0f;
        _cfg.invert = 0;
        _cfg.autostart = 0;
        save_config([_configPath fileSystemRepresentation], &_cfg);
    }

    [self createOverlay];
    [self setupStatusItem];

    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;
    InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, (__bridge void *)self, &_eventHandler);

    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'kblo';
    hotKeyID.id = 1;
    RegisterEventHotKey(kVK_ANSI_O, cmdKey + shiftKey, hotKeyID, GetApplicationEventTarget(), 0, &_hotKeyRef);

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
    const char *path = _cfg.overlay_path[0] ? _cfg.overlay_path : "keymap.png";
    if (load_overlay_image(path, &_overlay) != 0) {
        NSLog(@"Failed to load %s", path);
        return;
    }
    apply_opacity_inversion(&_overlay, _cfg.opacity, _cfg.invert);

    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    CGFloat width = (CGFloat)_overlay.width / scale;
    CGFloat height = (CGFloat)_overlay.height / scale;
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

- (void)togglePanel {
    if ([_panel isVisible]) {
        [_panel orderOut:nil];
    } else {
        [_panel orderFront:nil];
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
    if (hkCom.id == 1 && GetEventKind(event) == kEventHotKeyPressed) {
        [self togglePanel];
    }
    return noErr;
}
