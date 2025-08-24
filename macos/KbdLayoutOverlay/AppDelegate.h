#import <Cocoa/Cocoa.h>
#import "OverlayWindow.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)toggleOverlay;
- (void)showOverlay;
- (void)hideOverlay;
- (BOOL)isPersistent;

/* Hotkey control helpers */
- (void)registerHotkey;
- (void)registerCarbonHotkey;
- (void)unregisterCarbonHotkey;
- (void)handleCarbonHotkeyPressed;
- (void)unregisterCurrentHotkey;
- (BOOL)isValidHotkeyString:(NSString *)hk;
- (void)checkAccessibilityPermissions;
- (void)handleCGEventWithKeyCode:(CGKeyCode)keyCode flags:(CGEventFlags)flags filteredFlags:(CGEventFlags)filteredFlags isKeyDown:(BOOL)isKeyDown;

@end
