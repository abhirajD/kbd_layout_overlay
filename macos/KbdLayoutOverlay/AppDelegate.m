#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import <dispatch/dispatch.h>
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
    /* Ensure UI work is performed on the main thread to reliably show the overlay
       even when the app is backgrounded. */
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            [self toggleOverlay];
        }
    });
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
    int max_w = (int)([screen frame].size.width * scale * _config.scale);
    int max_h = (int)([screen frame].size.height * scale * _config.scale);
    
    /* Try multiple locations for keymap.png */
    NSArray *searchPaths = @[
        @"keymap.png",                                   // Current directory
        @"assets/keymap.png",                           // Assets folder
        @"../assets/keymap.png",                        // Assets folder (relative)
        [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"] ?: @"", // Bundle resource
    ];
    
    OverlayError result = OVERLAY_ERROR_FILE_NOT_FOUND;
    for (NSString *pathStr in searchPaths) {
        if ([pathStr length] == 0) continue;
        
        const char *path = [pathStr fileSystemRepresentation];
        result = load_overlay(path, max_w, max_h, &_overlay);
        if (result == OVERLAY_OK) {
            NSLog(@"Loaded overlay from: %@", pathStr);
            break;
        }
    }
    
    if (result != OVERLAY_OK) {
        /* Try embedded fallback */
        int size;
        const unsigned char *data = get_default_keymap(&size);
        if (data && size > 0) {
            result = load_overlay_mem(data, size, max_w, max_h, &_overlay);
            if (result == OVERLAY_OK) {
                NSLog(@"Using embedded keymap (build-time)");
            }
        }
    }
    
    if (result != OVERLAY_OK) {
        NSString *errorTitle = @"Image Loading Failed";
        NSString *errorMsg = @"Unknown error";
        
        switch (result) {
            case OVERLAY_ERROR_FILE_NOT_FOUND:
                errorMsg = @"Could not find keymap.png in any location"; break;
            case OVERLAY_ERROR_DECODE_FAILED:
                errorMsg = @"Could not decode image file"; break;
            case OVERLAY_ERROR_OUT_OF_MEMORY:
                errorMsg = @"Out of memory loading image"; break;
            case OVERLAY_ERROR_RESIZE_FAILED:
                errorMsg = @"Failed to resize image"; break;
            case OVERLAY_ERROR_NULL_PARAM:
                errorMsg = @"Internal error (null parameter)"; break;
            case OVERLAY_OK:
                errorMsg = @"No error"; break;
        }
        
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:errorTitle];
        [alert setInformativeText:[NSString stringWithFormat:@"%@\n\nPlease place keymap.png in one of these locations:\n• assets/ folder (before building)\n• Project root directory\n• App bundle resources", errorMsg]];
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }
    
    [self updateOverlayImage];
}

- (void)updateOverlayImage {
    /* Ensure we run on main thread when updating AppKit objects */
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateOverlayImage];
        });
        return;
    }

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
    /* Allow the overlay panel to appear while the app is in background and on all spaces */
    [_panel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary];
    /* Do not force app activation; allow the panel to float without stealing focus */
    [_panel setBecomesKeyOnlyIfNeeded:YES];
    [_panel setFloatingPanel:YES];
    
    _imageView = [[NSImageView alloc] initWithFrame:rect];
    [_imageView setImage:_overlayImage];
    [_panel setContentView:_imageView];
}

- (void)registerHotkey {
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;
    
    /* Install SYSTEM-WIDE event handler, not just for this app */
    InstallEventHandler(GetEventDispatcherTarget(), &hotKeyHandler, 1, &eventType, (__bridge void *)self, NULL);
    
    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'htk1';
    hotKeyID.id = 1;
    
    /* Command+Option+Shift+/ - system-wide registration */
    OSStatus status = RegisterEventHotKey(kVK_ANSI_Slash,
                                         cmdKey | optionKey | shiftKey,
                                         hotKeyID, GetEventDispatcherTarget(), 0, &_hotKeyRef);
    
    if (status != noErr) {
        NSLog(@"Failed to register global hotkey: %d", (int)status);
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Hotkey Registration Failed"];
        [alert setInformativeText:@"Could not register global hotkey. The app may not respond when other apps are focused."];
        [alert runModal];
    } else {
        NSLog(@"Successfully registered global hotkey");
    }
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
    
    /* Size options */
    NSMenuItem *size50Item = [[NSMenuItem alloc] initWithTitle:@"Size: 50%" action:@selector(setSize50:) keyEquivalent:@""];
    [size50Item setTarget:self];
    [size50Item setState:(_config.scale == 0.5f) ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:size50Item];
    
    NSMenuItem *size75Item = [[NSMenuItem alloc] initWithTitle:@"Size: 75%" action:@selector(setSize75:) keyEquivalent:@""];
    [size75Item setTarget:self];
    [size75Item setState:(_config.scale == 0.75f) ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:size75Item];
    
    NSMenuItem *size100Item = [[NSMenuItem alloc] initWithTitle:@"Size: 100%" action:@selector(setSize100:) keyEquivalent:@""];
    [size100Item setTarget:self];
    [size100Item setState:(_config.scale == 1.0f) ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:size100Item];
    
    NSMenuItem *size150Item = [[NSMenuItem alloc] initWithTitle:@"Size: 150%" action:@selector(setSize150:) keyEquivalent:@""];
    [size150Item setTarget:self];
    [size150Item setState:(_config.scale == 1.5f) ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:size150Item];
    
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
    
    /* Apply configurable positioning */
    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0 + _config.position_x;
    panelFrame.origin.y = NSMinY(screenFrame) + _config.position_y;
    
    [_panel setFrame:panelFrame display:NO];
    /* Order front regardless of app activation so the overlay is visible when app is backgrounded */
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

- (void)setSize50:(id)sender {
    [self setSizeScale:0.5f sender:sender];
}

- (void)setSize75:(id)sender {
    [self setSizeScale:0.75f sender:sender];
}

- (void)setSize100:(id)sender {
    [self setSizeScale:1.0f sender:sender];
}

- (void)setSize150:(id)sender {
    [self setSizeScale:1.5f sender:sender];
}

- (void)setSizeScale:(float)scale sender:(id)sender {
    _config.scale = scale;
    
    /* Reload overlay with new scale */
    free_overlay(&_overlay);
    [self loadOverlay];
    [self createOverlayWindow];
    
    /* Update menu checkmarks */
    _statusItem.menu = [self buildMenu];
    
    NSLog(@"Changed scale to %.0f%%", scale * 100);
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

@end
