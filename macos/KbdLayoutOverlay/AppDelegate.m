#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import "../shared/config.h"
#import "../shared/overlay.h"

@interface AppDelegate () {
    NSPanel *_panel;
    NSImageView *_imageView;
    NSStatusItem *_statusItem;
    Config _config;
    Overlay _overlay;
    EventHotKeyRef _hotKeyRef;
    NSImage *_overlayImage;
    BOOL _visible;
}
@end

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    AppDelegate *self = (__bridge AppDelegate *)userData;
    [self toggleOverlay];
    return noErr;
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _config = get_default_config();
    _visible = NO;
    
    [self loadOverlay];
    [self createOverlayWindow];
    [self registerHotkey];
    [self setupStatusItem];
    
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    if (_hotKeyRef) {
        UnregisterEventHotKey(_hotKeyRef);
    }
    free_overlay(&_overlay);
}

- (void)loadOverlay {
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    int max_w = (int)([screen frame].size.width * scale);
    int max_h = (int)([screen frame].size.height * scale);
    
    /* Try keymap.png first */
    if (load_overlay("keymap.png", max_w, max_h, &_overlay) != 0) {
        /* Try bundle resource */
        NSString *path = [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"];
        if (path && load_overlay([path fileSystemRepresentation], max_w, max_h, &_overlay) != 0) {
            /* Fallback to embedded default */
            int size;
            const unsigned char *data = get_default_keymap(&size);
            if (load_overlay_mem(data, size, max_w, max_h, &_overlay) != 0) {
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:@"Failed to load overlay image"];
                [alert runModal];
                [NSApp terminate:nil];
                return;
            }
        }
    }
    
    [self updateOverlayImage];
}

- (void)updateOverlayImage {
    /* Apply current effects */
    apply_effects(&_overlay, _config.opacity, _config.invert);
    
    /* Create NSImage from overlay data */
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:&_overlay.data
                      pixelsWide:_overlay.width
                      pixelsHigh:_overlay.height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:_overlay.width * 4
                    bitsPerPixel:32];
    
    _overlayImage = [[NSImage alloc] init];
    [_overlayImage addRepresentation:bitmap];
    
    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    [_overlayImage setSize:NSMakeSize(_overlay.width / scale, _overlay.height / scale)];
    
    if (_imageView) {
        [_imageView setImage:_overlayImage];
    }
}

- (void)createOverlayWindow {
    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    NSSize size = NSMakeSize(_overlay.width / scale, _overlay.height / scale);
    NSRect rect = NSMakeRect(0, 0, size.width, size.height);
    
    _panel = [[NSPanel alloc] initWithContentRect:rect
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]];
    [_panel setLevel:NSStatusWindowLevel];
    [_panel setIgnoresMouseEvents:YES];
    
    _imageView = [[NSImageView alloc] initWithFrame:rect];
    [_imageView setImage:_overlayImage];
    [_panel setContentView:_imageView];
}

- (void)registerHotkey {
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;
    
    InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, (__bridge void *)self, NULL);
    
    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'htk1';
    hotKeyID.id = 1;
    
    /* Command+Option+Shift+/ */
    RegisterEventHotKey(kVK_ANSI_Slash,
                       cmdKey | optionKey | shiftKey,
                       hotKeyID, GetApplicationEventTarget(), 0, &_hotKeyRef);
}

- (void)setupStatusItem {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    _statusItem.button.title = @"KLO";
    _statusItem.menu = [self buildMenu];
}

- (NSMenu *)buildMenu {
    NSMenu *menu = [[NSMenu alloc] init];
    
    NSMenuItem *persistentItem = [[NSMenuItem alloc] initWithTitle:@"Persistent mode" 
                                                            action:@selector(togglePersistent:) 
                                                     keyEquivalent:@""];
    [persistentItem setTarget:self];
    [persistentItem setState:_config.persistent ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:persistentItem];
    
    NSMenuItem *invertItem = [[NSMenuItem alloc] initWithTitle:@"Invert colors" 
                                                        action:@selector(toggleInvert:) 
                                                 keyEquivalent:@""];
    [invertItem setTarget:self];
    [invertItem setState:_config.invert ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:invertItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" 
                                                      action:@selector(quit:) 
                                               keyEquivalent:@""];
    [quitItem setTarget:self];
    [menu addItem:quitItem];
    
    return menu;
}

- (void)showOverlay {
    if (_visible || !_panel) return;
    
    NSScreen *screen = [NSScreen mainScreen];
    NSRect screenFrame = [screen visibleFrame];
    NSRect panelFrame = [_panel frame];
    
    /* Center horizontally, bottom of screen */
    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
    panelFrame.origin.y = NSMinY(screenFrame) + 100; /* 100px from bottom */
    
    [_panel setFrame:panelFrame display:NO];
    [_panel orderFront:nil];
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

- (void)togglePersistent:(id)sender {
    _config.persistent = !_config.persistent;
    [sender setState:_config.persistent ? NSControlStateValueOn : NSControlStateValueOff];
    
    if (_config.persistent && _visible) {
        [self hideOverlay];
    }
}

- (void)toggleInvert:(id)sender {
    _config.invert = !_config.invert;
    [sender setState:_config.invert ? NSControlStateValueOn : NSControlStateValueOff];
    
    [self updateOverlayImage];
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

@end