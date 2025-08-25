#import <Cocoa/Cocoa.h>
#import "OverlayWindow.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)toggleOverlay;
- (void)showOverlay;
- (void)hideOverlay;
- (BOOL)isPersistent;

/* New UX action methods */
- (void)toggleShowKeymap:(id)sender;
- (void)previewKeymap:(id)sender;
- (void)setSize125:(id)sender;
- (void)setSizeFitScreen:(id)sender;
- (void)setOpacity50:(id)sender;
- (void)setOpacity70:(id)sender;
- (void)setOpacity85:(id)sender;
- (void)setOpacity100:(id)sender;

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
