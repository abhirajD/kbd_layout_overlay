#import "AppDelegate.h"
#import <Carbon/Carbon.h>
#import "OverlayView.h"

static OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);

@implementation AppDelegate {
    EventHotKeyRef _hotKeyRef;
    EventHandlerRef _eventHandler;
    NSPanel *_panel;
    OverlayView *_overlayView;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self createOverlay];

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
}

- (void)createOverlay {
    NSRect rect = NSMakeRect(100, 100, 300, 100);
    _panel = [[NSPanel alloc] initWithContentRect:rect
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]];
    [_panel setLevel:NSStatusWindowLevel];
    [_panel setIgnoresMouseEvents:YES];

    _overlayView = [[OverlayView alloc] initWithFrame:rect];
    [_panel setContentView:_overlayView];
    [_overlayView cacheSampleBuffer];
}

- (void)togglePanel {
    if ([_panel isVisible]) {
        [_panel orderOut:nil];
    } else {
        [_panel orderFront:nil];
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
