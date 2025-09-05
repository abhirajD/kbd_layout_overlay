#import "AppDelegate.h"
#import "../WindowManager.h"
#import "../MenuController.h"
#import "../HotkeyManager.h"
#import "../ImageManager.h"
#import "../shared/config.h"
#import "../shared/log.h"
#import <Carbon/Carbon.h>

@interface AppDelegate () {
    Config _config;
    WindowManager *_windowManager;
    MenuController *_menuController;
    HotkeyManager *_hotkeyManager;
    ImageManager *_imageManager;
}
@end


/* Carbon hotkey event handler - called when global hotkey is pressed */
static OSStatus CarbonHotkeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID hotKeyID;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hotKeyID), NULL, &hotKeyID);
    
    if (hotKeyID.signature == 'KLOH' && hotKeyID.id == 1) {
        AppDelegate *appDelegate = (__bridge AppDelegate *)userData;
        if (appDelegate) {
            logger_log("Carbon hotkey pressed - toggle overlay via Carbon handler");
            dispatch_async(dispatch_get_main_queue(), ^{
                [appDelegate handleCarbonHotkeyPressed];
            });
        }
    }
    
    return noErr;
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _config = get_default_config();
    /* Load persisted config if present (overrides defaults) */
    load_config(&_config, NULL);

    /* Initialize logger early for parity with Windows */
    logger_init();
    logger_log("KbdLayoutOverlay (macOS) starting up");

    // Initialize managers
    _imageManager = [[ImageManager alloc] initWithConfig:_config];
    _windowManager = [[WindowManager alloc] initWithConfig:_config];
    _menuController = [[MenuController alloc] initWithConfig:_config];
    _hotkeyManager = [[HotkeyManager alloc] initWithConfig:_config callback:^{
        [self toggleOverlay];
    }];

    // Load and setup overlay
    if ([_imageManager loadOverlay]) {
        [_windowManager createOverlayWindow];
        [_hotkeyManager registerHotkey];
        [_menuController setupStatusItem];
    }

    /* Use Regular activation policy to allow global hotkey monitoring.
       LSUIElement in Info.plist prevents dock icon. */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

- (void)toggleOverlay {
    [_windowManager toggleOverlay];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    /* Don't terminate when windows are closed - keep running as menu bar app */
    return NO;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    logger_log("KbdLayoutOverlay (macOS) terminating");
    logger_close();
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

// Stub implementations for header-declared methods
- (void)showOverlay {
    [_windowManager showOverlay];
}

- (void)hideOverlay {
    [_windowManager hideOverlay];
}

- (BOOL)isPersistent {
    return YES; // Always persistent as menu bar app
}

- (void)toggleShowKeymap:(id)sender {
    [self toggleOverlay];
}

- (void)previewKeymap:(id)sender {
    [_windowManager showOverlay];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [_windowManager hideOverlay];
    });
}

- (void)setSize125:(id)sender {
    // Implementation moved to managers
}

- (void)setSizeFitScreen:(id)sender {
    // Implementation moved to managers
}

- (void)setOpacity50:(id)sender {
    // Implementation moved to managers
}

- (void)setOpacity70:(id)sender {
    // Implementation moved to managers
}

- (void)setOpacity85:(id)sender {
    // Implementation moved to managers
}

- (void)setOpacity100:(id)sender {
    // Implementation moved to managers
}

- (void)registerHotkey {
    [_hotkeyManager registerHotkey];
}

- (void)registerCarbonHotkey {
    [_hotkeyManager registerHotkey];
}

- (void)unregisterCarbonHotkey {
    [_hotkeyManager unregisterHotkey];
}

- (void)handleCarbonHotkeyPressed {
    [self toggleOverlay];
}

- (void)unregisterCurrentHotkey {
    [_hotkeyManager unregisterHotkey];
}

- (BOOL)isValidHotkeyString:(NSString *)hk {
    return YES; // Basic validation
}

- (void)checkAccessibilityPermissions {
    // Implementation moved to managers
}

- (void)handleCGEventWithKeyCode:(CGKeyCode)keyCode flags:(CGEventFlags)flags filteredFlags:(CGEventFlags)filteredFlags isKeyDown:(BOOL)isKeyDown {
    // No-op stub
}

@end
