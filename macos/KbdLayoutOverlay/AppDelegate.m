#import "AppDelegate.h"
#import "OverlayWindow.h"
#import <Carbon/Carbon.h>
#import <ApplicationServices/ApplicationServices.h>
#import "hotkey_parse.h"
#import <dispatch/dispatch.h>
#include <math.h>
#import "../shared/config.h"
#import "../shared/overlay.h"
#import "../shared/log.h"
#import <CommonCrypto/CommonDigest.h>
#import <IOKit/hid/IOHIDManager.h>

//
// Lightweight NSWindow subclass used only for instrumentation to log lifecycle events.
// This helps identify who releases the preferences window and when it is deallocated.
//
@interface LoggingWindow : NSWindow
@end

@implementation LoggingWindow
- (void)dealloc {
    NSLog(@"LoggingWindow dealloc: %p", self);
}
@end

@interface HotkeyCaptureField : NSTextField
@end

@implementation HotkeyCaptureField {
    NSEventModifierFlags _latestModifiers;
    BOOL _isCapturing;
    NSString *_lockedHotkey;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)mouseDown:(NSEvent *)event {
    // Start a new capture session. Unlock previous capture and clear the field so user can press a new combo.
    _isCapturing = YES;
    _lockedHotkey = nil;
    [self setStringValue:@""];
    [self.window makeFirstResponder:self];
}

- (void)flagsChanged:(NSEvent *)event {
    // If a hotkey has already been locked (a non-modifier key was pressed), ignore further modifier changes
    if (_lockedHotkey) return;

    // Only show live modifier preview while actively capturing
    if (!_isCapturing) return;

    _latestModifiers = [event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift | NSEventModifierFlagControl);
    NSMutableArray *parts = [NSMutableArray array];
    if (_latestModifiers & NSEventModifierFlagCommand) [parts addObject:@"Command"];
    if (_latestModifiers & NSEventModifierFlagOption) [parts addObject:@"Option"];
    if (_latestModifiers & NSEventModifierFlagShift) [parts addObject:@"Shift"];
    if (_latestModifiers & NSEventModifierFlagControl) [parts addObject:@"Control"];
    if ([parts count] > 0) {
        [self setStringValue:[parts componentsJoinedByString:@"+"]];
    } else if ([[self stringValue] length] == 0) {
        [self setPlaceholderString:@"Click and press a hotkey"];
    }
}

- (void)keyDown:(NSEvent *)event {
    // Capture stops when a non-modifier key is pressed; lock the captured hotkey to prevent modifier-release from changing it.
    NSString *chars = [event charactersIgnoringModifiers];
    unsigned short keyCode = [event keyCode];
    NSMutableArray *parts = [NSMutableArray array];
    NSEventModifierFlags flags = [event modifierFlags];
    if (flags & NSEventModifierFlagCommand) [parts addObject:@"Command"];
    if (flags & NSEventModifierFlagOption) [parts addObject:@"Option"];
    if (flags & NSEventModifierFlagShift) [parts addObject:@"Shift"];
    if (flags & NSEventModifierFlagControl) [parts addObject:@"Control"];

    NSString *keyName = nil;
    if ([chars length] > 0) {
        // Use printable character when available
        keyName = [[chars uppercaseString] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        // Normalize common printable keys to the same token used by registration logic
        if ([keyName isEqualToString:@"/"] || keyCode == kVK_ANSI_Slash) {
            keyName = @"Slash";
        }
    }

    if (!keyName || [keyName length] == 0) {
        switch (keyCode) {
            case kVK_Space: keyName = @"Space"; break;
            case kVK_Return: keyName = @"Return"; break;
            case kVK_ANSI_Slash: keyName = @"Slash"; break;
            case kVK_Escape: keyName = @"Escape"; break;
            case kVK_F1: keyName = @"F1"; break;
            case kVK_F2: keyName = @"F2"; break;
            case kVK_F3: keyName = @"F3"; break;
            case kVK_F4: keyName = @"F4"; break;
            case kVK_F5: keyName = @"F5"; break;
            case kVK_F6: keyName = @"F6"; break;
            case kVK_F7: keyName = @"F7"; break;
            case kVK_F8: keyName = @"F8"; break;
            case kVK_F9: keyName = @"F9"; break;
            case kVK_F10: keyName = @"F10"; break;
            case kVK_F11: keyName = @"F11"; break;
            case kVK_F12: keyName = @"F12"; break;
            default: keyName = nil; break;
        }
    }

    // Prevent capturing only modifiers
    if ((!keyName || [keyName length] == 0) || ([parts count] == 0 && (!keyName || [keyName length] == 0))) {
        NSBeep();
        return;
    }

    [parts addObject:keyName];
    NSString *hotkey = [parts componentsJoinedByString:@"+"];

    // Lock the captured hotkey so subsequent modifier release events won't overwrite it.
    _lockedHotkey = hotkey;
    _isCapturing = NO;

    [self setStringValue:hotkey];

    // Notify target/action if configured
    if ([self action] && [self target]) {
        [[NSApplication sharedApplication] sendAction:[self action] to:[self target] from:self];
    }

    // Finish capture by resigning first responder
    [self.window makeFirstResponder:nil];
}

- (void)clearHotkey {
    _lockedHotkey = nil;
    [self setStringValue:@""];
}

// Return the currently-captured hotkey. If a non-modifier key was pressed the capture is locked
// into _lockedHotkey; otherwise fall back to the current field text.
- (NSString *)currentHotkey {
    if (_lockedHotkey && [_lockedHotkey length] > 0) return _lockedHotkey;
    NSString *s = [self stringValue];
    return s ? s : @"";
}

@end

@interface AppDelegate () <NSWindowDelegate> {
    OverlayWindow *_panel;
    NSImageView *_imageView;
    NSStatusItem *_statusItem;
    Config _config;
    Overlay _overlay;
    CFMachPortRef _eventTap;
    CFRunLoopSourceRef _runLoopSource;
    NSUInteger _targetModifierFlags;
    NSUInteger _targetKeyCode;
    NSImage *_overlayImage;
    BOOL _visible;
    /* Preferences UI state */
    NSWindow *_prefsWindow;
    HotkeyCaptureField *_prefHotkeyField;
    NSSlider *_prefOpacitySlider;
    NSButton *_prefUseCustomSizeCheckbox;
    NSTextField *_prefWidthField;
    NSTextField *_prefHeightField;
    NSTextField *_prefErrorLabel;
    NSButton *_prefRestoreDefaultsBtn;
    /* Preview buffer (reused) so live preview does not mutate original overlay pixels */
    unsigned char *_previewBuffer;
    size_t _previewBufferSize;
    
    /* State tracking for press-and-hold behavior */
    BOOL _hotkeyPressed;
    BOOL _overlayShownByHotkey;
    
    /* Carbon hotkey registration (reliable, no permissions needed) */
    EventHotKeyRef _carbonHotKey;
    BOOL _carbonHotkeyActive;
    NSTimer *_carbonHideTimer;
    /* HID fallback manager for environments where CGEventTap cannot be created */
    IOHIDManagerRef _hidManager;
    BOOL _hidFallbackActive;
    /* HID-derived modifier mask (synthesized from left/right modifier usages) */
    CGEventFlags _hidModifierFlags;
    /* Last non-modifier HID usage seen (for basic debouncing) */
    uint32_t _hidLastKeyUsage;
}
@end

CGEventRef eventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
    AppDelegate *appDelegate = (__bridge AppDelegate *)refcon;
    
    if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
        CGKeyCode keyCode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        CGEventFlags flags = CGEventGetFlags(event);
        
        // Filter to only the modifier flags we care about
        CGEventFlags relevantFlags = flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift | kCGEventFlagMaskControl);
        
        // Call back to the app delegate to handle the event
        BOOL isKeyDown = (type == kCGEventKeyDown);
        [appDelegate handleCGEventWithKeyCode:keyCode flags:flags filteredFlags:relevantFlags isKeyDown:isKeyDown];
    }
    
    // Return the event unmodified
    return event;
}

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

/* Handle Carbon hotkey press - called from main queue */
- (void)handleCarbonHotkeyPressed {
    // Invalidate any existing hide timer
    if (_carbonHideTimer) {
        [_carbonHideTimer invalidate];
        _carbonHideTimer = nil;
    }

    // Persistent mode: simply toggle without scheduling a timer
    if ([self isPersistent]) {
        if ([_panel isVisible]) {
            [self hideOverlay];
        } else {
            [self showOverlay];
        }
    } else {
        // Non-persistent: toggle and schedule auto-hide after 0.8s when shown
        if ([_panel isVisible]) {
            [self hideOverlay];
        } else {
            [self showOverlay];
            _carbonHideTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 repeats:NO block:^(NSTimer * _Nonnull t) {
                [self hideOverlay];
                _carbonHideTimer = nil;
            }];
        }
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    _config = get_default_config();
    /* Load persisted config if present (overrides defaults) */
    load_config(&_config, NULL);
    _visible = NO;

    /* Initialize logger early for parity with Windows */
    logger_init();
    logger_log("KbdLayoutOverlay (macOS) starting up");
    
    [self loadOverlay];
    [self createOverlayWindow];
    /* Carbon hotkeys don't need accessibility permissions */
    [self registerCarbonHotkey];
    [self setupStatusItem];
    
    /* Use Regular activation policy to allow global hotkey monitoring.
       LSUIElement in Info.plist prevents dock icon. */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    /* Don't terminate when windows are closed - keep running as menu bar app */
    return NO;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    logger_log("KbdLayoutOverlay (macOS) terminating");
    
    /* Clean up Carbon hotkey */
    [self unregisterCarbonHotkey];
    if (_carbonHideTimer) {
        [_carbonHideTimer invalidate];
        _carbonHideTimer = nil;
    }
    
    
    free_overlay(&_overlay);
    if (_previewBuffer) {
        free(_previewBuffer);
        _previewBuffer = NULL;
        _previewBufferSize = 0;
    }
    /* Tear down HID fallback if active */
    [self teardownHIDFallback];
    logger_close();
}

- (void)loadOverlay {
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = [screen backingScaleFactor];
    int max_w = 0;
    int max_h = 0;
    if (_config.use_custom_size) {
        /* Use explicit pixel size from config when requested */
        max_w = _config.custom_width_px > 0 ? _config.custom_width_px : (int)([screen frame].size.width * scale * _config.scale);
        max_h = _config.custom_height_px > 0 ? _config.custom_height_px : (int)([screen frame].size.height * scale * _config.scale);
    } else {
        max_w = (int)([screen frame].size.width * scale * _config.scale);
        max_h = (int)([screen frame].size.height * scale * _config.scale);
    }
    
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

    /* Allocate or resize preview buffer as needed */
    size_t pixelCount = (size_t)_overlay.width * (size_t)_overlay.height * 4;
    if (_previewBufferSize < pixelCount || !_previewBuffer) {
        _previewBuffer = realloc(_previewBuffer, pixelCount);
        _previewBufferSize = pixelCount;
    }
    if (!_previewBuffer) {
        NSLog(@"updateOverlayImage: failed to allocate preview buffer");
        return;
    }

    /* Copy original pixels into preview buffer and apply effects to the copy */
    memcpy(_previewBuffer, _overlay.data, pixelCount);
    Overlay tmp;
    tmp.data = _previewBuffer;
    tmp.width = _overlay.width;
    tmp.height = _overlay.height;
    tmp.channels = _overlay.channels;
    tmp.cached_effects = 0;
    tmp.cached_opacity = 0.0f;
    tmp.cached_invert = 0;

    apply_effects(&tmp, _config.opacity, _config.invert);

    /* Create NSImage from preview buffer (use planes array) */
    unsigned char *planes[1] = { _previewBuffer };
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:planes
                      pixelsWide:tmp.width
                      pixelsHigh:tmp.height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:tmp.width * 4
                    bitsPerPixel:32];

    _overlayImage = [[NSImage alloc] init];
    [_overlayImage addRepresentation:bitmap];

    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    [_overlayImage setSize:NSMakeSize(tmp.width / scale, tmp.height / scale)];

    if (_imageView) {
        [_imageView setImage:_overlayImage];
    }
}

- (void)createOverlayWindow {
    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    NSSize size = NSMakeSize(_overlay.width / scale, _overlay.height / scale);
    NSRect rect = NSMakeRect(100, 100, size.width, size.height); /* Fixed position for testing */
    
    NSLog(@"createOverlayWindow: scale=%.1f size=(%.0fx%.0f) rect=(%.0f,%.0f,%.0fx%.0f)", 
          scale, size.width, size.height, rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    
    /* Use OverlayWindow for proper non-activating behavior */
    _panel = [[OverlayWindow alloc] initWithContentRect:rect
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]]; /* Transparent background */
    [_panel setLevel:NSScreenSaverWindowLevel]; /* Float above everything */
    [_panel setIgnoresMouseEvents:YES];
    [_panel setHasShadow:NO];
    
    /* Set proper collection behavior for multi-space support */
    [_panel setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces | 
                                   NSWindowCollectionBehaviorFullScreenAuxiliary | 
                                   NSWindowCollectionBehaviorIgnoresCycle)];
    
    /* Simple image view */
    _imageView = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    [_imageView setImage:_overlayImage];
    [_panel setContentView:_imageView];
    
    NSLog(@"createOverlayWindow: created window=%p imageView=%p overlayImage=%p", _panel, _imageView, _overlayImage);
}

- (void)checkAccessibilityPermissions {
    /* First check without prompting to see current status */
    BOOL accessibilityEnabled = AXIsProcessTrusted();
    NSLog(@"checkAccessibilityPermissions: Initial check - accessibility enabled: %d", accessibilityEnabled);
    
    if (!accessibilityEnabled) {
        /* Only prompt once per app session by checking if we've already shown the prompt */
        static BOOL hasPrompted = NO;
        if (!hasPrompted) {
            NSLog(@"checkAccessibilityPermissions: Accessibility access not granted - requesting permission");
            NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
            accessibilityEnabled = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
            hasPrompted = YES;
            NSLog(@"checkAccessibilityPermissions: After prompt - accessibility enabled: %d", accessibilityEnabled);
        }
        
        if (!accessibilityEnabled) {
            NSLog(@"checkAccessibilityPermissions: Accessibility permission required - please grant in System Preferences > Privacy & Security > Accessibility");
        }
    } else {
        NSLog(@"checkAccessibilityPermissions: Accessibility access already granted");
    }
}

/* Diagnostic helpers: log codesign output and SHA256 of the running executable, and prompt to open privacy panes. */
- (void)logCodesignAndExecutableInfo {
    NSString *bundlePath = [[NSBundle mainBundle] bundlePath] ?: @"(unknown)";
    NSString *executablePath = [[NSBundle mainBundle] executablePath] ?: @"(unknown)";

    /* Run codesign --display --verbose=4 on the bundle path and log output */
    @try {
        NSTask *task = [[NSTask alloc] init];
        [task setLaunchPath:@"/usr/bin/codesign"];
        [task setArguments:@[@"--display", @"--verbose=4", bundlePath]];
        NSPipe *pipe = [NSPipe pipe];
        [task setStandardOutput:pipe];
        [task setStandardError:pipe];
        [task launch];
        [task waitUntilExit];
        NSData *outData = [[pipe fileHandleForReading] readDataToEndOfFile];
        NSString *outStr = [[NSString alloc] initWithData:outData encoding:NSUTF8StringEncoding] ?: @"(no output)";
        logger_log("codesign --display --verbose=4 output:\\n%s", [outStr UTF8String]);
        NSLog(@"codesign output: %@", outStr);
    } @catch (NSException *ex) {
        logger_log("codesign task exception: %s", [[[ex reason] description] UTF8String]);
        NSLog(@"codesign task exception: %@", ex);
    }

    /* Compute SHA256 of the executable file */
    if ([[NSFileManager defaultManager] fileExistsAtPath:executablePath]) {
        NSData *fileData = [NSData dataWithContentsOfFile:executablePath];
        if (fileData) {
            unsigned char digest[CC_SHA256_DIGEST_LENGTH];
            CC_SHA256([fileData bytes], (CC_LONG)[fileData length], digest);
            NSMutableString *hex = [NSMutableString stringWithCapacity:CC_SHA256_DIGEST_LENGTH * 2];
            for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
                [hex appendFormat:@"%02x", digest[i]];
            }
            logger_log("executable_sha256=%s path=%s", [hex UTF8String], [executablePath UTF8String]);
            NSLog(@"Executable SHA256: %@", hex);
        } else {
            logger_log("Failed to read executable for SHA256 at path=%s", [executablePath UTF8String]);
        }
    } else {
        logger_log("Executable path does not exist: %s", [executablePath UTF8String]);
    }
}

/* Prompt user with option to open Input Monitoring or Accessibility privacy panes */
- (void)promptOpenPrivacyPanes {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Input Monitoring / Accessibility permissions required"];
        [alert setInformativeText:@"KbdLayoutOverlay failed to register a global hotkey monitor. This commonly requires Input Monitoring (and sometimes Accessibility) to be enabled in System Settings → Privacy & Security. Choose a pane to open:"];
        [alert addButtonWithTitle:@"Open Input Monitoring"];
        [alert addButtonWithTitle:@"Open Accessibility"];
        [alert addButtonWithTitle:@"Cancel"];
        NSModalResponse resp = [alert runModal];
        if (resp == NSAlertFirstButtonReturn) {
            NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_InputMonitoring"];
            [[NSWorkspace sharedWorkspace] openURL:url];
        } else if (resp == NSAlertSecondButtonReturn) {
            NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
            [[NSWorkspace sharedWorkspace] openURL:url];
        }
    });
}

/* Minimal IOHIDManager fallback: register for keyboard usages and log events.
   This is a diagnostic prototype — mapping HID usages to macOS virtual keycodes
   is non-trivial and platform-specific; this function helps determine whether
   HID events are visible to the process when CGEventTap cannot be created.
*/

/* Helper: map common HID keyboard usages (USB HID usage IDs) to mac CGKeyCode values.
   This mapping is intentionally minimal — extend as needed. USB HID usage for 'a'==4.
*/
static CGKeyCode hidUsageToCGKeyCode(uint32_t usage) {
    // Letters: usage 4 -> 'A', 5 -> 'B', ...
    if (usage >= 4 && usage <= 29) { // a..z (4..29)
        // Map letters to mac keycodes (QWERTY)
        static const CGKeyCode letterMap[26] = {
            0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46, 45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6
        };
        return letterMap[usage - 4];
    }

    // Numbers (top row): usage 30..39 -> 1..0
    if (usage >= 30 && usage <= 39) {
        static const CGKeyCode numMap[10] = {
            18,19,20,21,23,22,26,28,25,29
        };
        return numMap[usage - 30];
    }

    // Special keys
    switch (usage) {
        case 44: return 44; /* Slash -> kVK_ANSI_Slash (already 44 in our mapping) */
        case 40: return 41; /* Semicolon usage (HID 51?) best-effort fallback */
        case 57: return 49; /* Space usage */
        case 40 + 8: /* Return/Enter common HID id can vary */ return 36;
        default:
            return (CGKeyCode)NSNotFound;
    }
}

/* Update synthesized modifier mask from HID modifier usages (0xE0 - 0xE7).
   Left/Right mapping:
     0xE0 = LeftControl  -> kCGEventFlagMaskControl
     0xE1 = LeftShift    -> kCGEventFlagMaskShift
     0xE2 = LeftAlt      -> kCGEventFlagMaskAlternate
     0xE3 = LeftGUI      -> kCGEventFlagMaskCommand
     0xE4 = RightControl -> kCGEventFlagMaskControl
     0xE5 = RightShift   -> kCGEventFlagMaskShift
     0xE6 = RightAlt     -> kCGEventFlagMaskAlternate
     0xE7 = RightGUI     -> kCGEventFlagMaskCommand
*/
static void handleHIDModifier(AppDelegate *self, uint32_t usage, CFIndex pressed) {
    if (!self) return;
    CGEventFlags mask = 0;
    if (usage >= 0xE0 && usage <= 0xE7) {
        switch (usage) {
            case 0xE0: case 0xE4: mask = kCGEventFlagMaskControl; break;
            case 0xE1: case 0xE5: mask = kCGEventFlagMaskShift; break;
            case 0xE2: case 0xE6: mask = kCGEventFlagMaskAlternate; break;
            case 0xE3: case 0xE7: mask = kCGEventFlagMaskCommand; break;
            default: mask = 0; break;
        }
        if (pressed) {
            self->_hidModifierFlags |= mask;
        } else {
            self->_hidModifierFlags &= ~mask;
        }
        logger_log("IOHID: modifier usage=0x%02x pressed=%d mask=0x%llx", usage, (int)pressed, (unsigned long long)self->_hidModifierFlags);
    }
}

/* Convert HID event into the existing CGEvent-style handler so we can reuse hotkey logic.
   For non-modifier keys we synthesize filteredFlags from _hidModifierFlags and call
   handleCGEventWithKeyCode:flags:filteredFlags:isKeyDown:
*/
static void handleHIDKeyEvent(AppDelegate *self, uint32_t usage, CFIndex value) {
    if (!self) return;
    // Detect modifier usages first
    if (usage >= 0xE0 && usage <= 0xE7) {
        handleHIDModifier(self, usage, value > 0);
        return;
    }

    // Map HID usage to mac CGKeyCode
    CGKeyCode keyCode = hidUsageToCGKeyCode(usage);
    if (keyCode == (CGKeyCode)NSNotFound) {
        logger_log("IOHID: unmapped HID usage %u (ignoring)", usage);
        return;
    }

    // Use the synthesized modifier mask as filteredFlags (subset matching)
    CGEventFlags filteredFlags = self->_hidModifierFlags;
    BOOL isKeyDown = (value > 0);

    // Call into the same handler used by CGEventTap
    [self handleCGEventWithKeyCode:keyCode flags:filteredFlags filteredFlags:filteredFlags isKeyDown:isKeyDown];

    // Store last usage for basic debouncing / diagnostics
    self->_hidLastKeyUsage = usage;
}

/* IOHID callback now converts HID usages into synthesized modifier state and key events */
static void HIDInputCallback(void *context, IOReturn result, void *sender, IOHIDValueRef value) {
    AppDelegate *self = (__bridge AppDelegate *)context;
    if (!value || !self) return;
    IOHIDElementRef elem = IOHIDValueGetElement(value);
    if (!elem) return;
    uint32_t usagePage = IOHIDElementGetUsagePage(elem);
    uint32_t usage = IOHIDElementGetUsage(elem);
    CFIndex intValue = IOHIDValueGetIntegerValue(value);

    // Only handle keyboard usage page
    if (usagePage != 0x07) {
        return;
    }

    // Log for diagnostics
    logger_log("IOHID: keyboard usage=%u value=%ld", (unsigned)usage, (long)intValue);

    // Handle modifier and key events
    handleHIDKeyEvent(self, usage, intValue);
}

- (void)setupHIDFallback {
    if (_hidManager) return;
    _hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!_hidManager) {
        logger_log("setupHIDFallback: IOHIDManagerCreate failed");
        return;
    }

    // Match keyboard usage page (0x07). Usage 0 means "any".
    CFNumberRef page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, (int[]){0x07});
    CFNumberRef usageAny = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, (int[]){0});
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                            &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), page);
    CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), usageAny);

    IOHIDManagerSetDeviceMatching(_hidManager, dict);
    IOHIDManagerRegisterInputValueCallback(_hidManager, HIDInputCallback, (__bridge void *)self);
    IOHIDManagerScheduleWithRunLoop(_hidManager, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    IOReturn r = IOHIDManagerOpen(_hidManager, kIOHIDOptionsTypeNone);
    if (r == kIOReturnSuccess) {
        _hidFallbackActive = YES;
        logger_log("setupHIDFallback: IOHIDManager opened successfully");
    } else {
        logger_log("setupHIDFallback: IOHIDManagerOpen failed: 0x%x", r);
    }

    if (page) CFRelease(page);
    if (usageAny) CFRelease(usageAny);
    if (dict) CFRelease(dict);
}

- (void)teardownHIDFallback {
    if (!_hidManager) return;
    IOHIDManagerUnscheduleFromRunLoop(_hidManager, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    IOHIDManagerRegisterInputValueCallback(_hidManager, NULL, NULL);
    IOHIDManagerClose(_hidManager, kIOHIDOptionsTypeNone);
    CFRelease(_hidManager);
    _hidManager = NULL;
    _hidFallbackActive = NO;
}

/* Existing event handler (unchanged) */
- (void)handleCGEventWithKeyCode:(CGKeyCode)keyCode flags:(CGEventFlags)flags filteredFlags:(CGEventFlags)filteredFlags isKeyDown:(BOOL)isKeyDown {
    // Check if this matches our target hotkey with subset matching
    BOOL keyMatches = (keyCode == _targetKeyCode);
    BOOL modifiersMatch = (filteredFlags & _targetModifierFlags) == _targetModifierFlags;
    
    if (keyMatches && modifiersMatch) {
        if (isKeyDown && !_hotkeyPressed) {
            // Key pressed: show overlay and mark state
            logger_log("CGEventTap: Hotkey pressed - showing overlay");
            _hotkeyPressed = YES;
            _overlayShownByHotkey = YES;
            
            dispatch_async(dispatch_get_main_queue(), ^{
                [self showOverlay];
            });
        }
    } else if (_hotkeyPressed && (!modifiersMatch || (keyMatches && !isKeyDown))) {
        // Key released or modifiers changed: hide overlay if shown by hotkey
        logger_log("CGEventTap: Hotkey released - hiding overlay");
        _hotkeyPressed = NO;
        
        if (_overlayShownByHotkey && ![self isPersistent]) {
            _overlayShownByHotkey = NO;
            dispatch_async(dispatch_get_main_queue(), ^{
                [self hideOverlay];
            });
        }
    }
}

- (NSEventModifierFlags)parseModifierFlags:(NSString *)hotkeyString {
    NSEventModifierFlags flags = 0;
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Command"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Cmd"]) {
        flags |= NSEventModifierFlagCommand;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Option"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Alt"]) {
        flags |= NSEventModifierFlagOption;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Shift"]) {
        flags |= NSEventModifierFlagShift;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Control"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Ctrl"]) {
        flags |= NSEventModifierFlagControl;
    }
    return flags;
}

- (CGEventFlags)parseCGModifierFlags:(NSString *)hotkeyString {
    CGEventFlags flags = 0;
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Command"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Cmd"]) {
        flags |= kCGEventFlagMaskCommand;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Option"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Alt"]) {
        flags |= kCGEventFlagMaskAlternate;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Shift"]) {
        flags |= kCGEventFlagMaskShift;
    }
    if ([hotkeyString localizedCaseInsensitiveContainsString:@"Control"] || 
        [hotkeyString localizedCaseInsensitiveContainsString:@"Ctrl"]) {
        flags |= kCGEventFlagMaskControl;
    }
    return flags;
}

- (NSUInteger)parseKeyCode:(NSString *)hotkeyString {
    /* Extract the key name (last component after '+') */
    NSArray *components = [hotkeyString componentsSeparatedByString:@"+"];
    NSString *keyName = [[components lastObject] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    
    /* Use a mapping dictionary for cleaner, more maintainable key mapping */
    static NSDictionary *keyMap = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        keyMap = @{
            // Function keys
            @"F1": @122, @"F2": @120, @"F3": @99, @"F4": @118, @"F5": @96, @"F6": @97,
            @"F7": @98, @"F8": @100, @"F9": @101, @"F10": @109, @"F11": @103, @"F12": @111,
            
            // Special keys
            @"SLASH": @44, @"SPACE": @49, @"RETURN": @36, @"ENTER": @36, @"ESCAPE": @53, @"ESC": @53,
            @"TAB": @48, @"DELETE": @51, @"BACKSPACE": @51,
            
            // Arrow keys
            @"UP": @126, @"DOWN": @125, @"LEFT": @123, @"RIGHT": @124,
            
            // Number row
            @"0": @29, @"1": @18, @"2": @19, @"3": @20, @"4": @21, @"5": @23,
            @"6": @22, @"7": @26, @"8": @28, @"9": @25,
            
            // Letters (QWERTY layout)
            @"A": @0, @"B": @11, @"C": @8, @"D": @2, @"E": @14, @"F": @3,
            @"G": @5, @"H": @4, @"I": @34, @"J": @38, @"K": @40, @"L": @37,
            @"M": @46, @"N": @45, @"O": @31, @"P": @35, @"Q": @12, @"R": @15,
            @"S": @1, @"T": @17, @"U": @32, @"V": @9, @"W": @13, @"X": @7,
            @"Y": @16, @"Z": @6,
            
            // Punctuation
            @"SEMICOLON": @41, @"QUOTE": @39, @"COMMA": @43, @"PERIOD": @47,
            @"MINUS": @27, @"EQUAL": @24, @"LEFTBRACKET": @33, @"RIGHTBRACKET": @30,
            @"BACKSLASH": @42, @"GRAVE": @50
        };
    });
    
    NSString *upperKeyName = [keyName uppercaseString];
    NSNumber *keyCode = keyMap[upperKeyName];
    
    if (keyCode) {
        return [keyCode unsignedIntegerValue];
    }
    
    NSLog(@"parseKeyCode: Unknown key name '%@' - supported keys: %@", keyName, [[keyMap allKeys] sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)]);
    return NSNotFound;
}

- (void)registerHotkey {
    /* Ensure registration happens on the main thread */
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            [self registerHotkey];
        });
        return;
    }

    NSString *hk = [NSString stringWithUTF8String:_config.hotkey];
    if (!hk) hk = @"";
    NSLog(@"registerHotkey: attempting to register hotkey string: '%@'", hk);

    /* Basic validation: require at least one modifier to avoid accidental single-key triggers */
    BOOL hasModifier = ([hk localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Control"]);
    if (![hk length] || !hasModifier) {
        NSLog(@"registerHotkey: invalid or modifier-less hotkey '%@' - skipping registration", hk);
        return;
    }

    /* Parse the hotkey string into CGEvent modifier flags and key code */
    CGEventFlags modifierFlags = [self parseCGModifierFlags:hk];
    CGKeyCode keyCode = [self parseKeyCode:hk];
    
    if (modifierFlags == 0 || keyCode == NSNotFound) {
        NSLog(@"registerHotkey: failed to parse hotkey '%@' - skipping registration", hk);
        return;
    }

    /* Remove existing event tap if present */
    if (_eventTap) {
        CGEventTapEnable(_eventTap, false);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), _runLoopSource, kCFRunLoopCommonModes);
        CFRelease(_runLoopSource);
        CFRelease(_eventTap);
        _eventTap = NULL;
        _runLoopSource = NULL;
    }

    /* Store the target combination for comparison */
    _targetModifierFlags = modifierFlags;
    _targetKeyCode = keyCode;

    /* Create CGEventTap to monitor key events */
    _eventTap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp),
        eventTapCallback,
        (__bridge void *)self
    );

    if (_eventTap) {
        _runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, _eventTap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), _runLoopSource, kCFRunLoopCommonModes);
        CGEventTapEnable(_eventTap, true);
        
        NSLog(@"Successfully registered CGEventTap hotkey monitor: %@ (modifierFlags=0x%llx keyCode=%d)", 
              hk, (unsigned long long)modifierFlags, keyCode);
        logger_log("Successfully registered CGEventTap hotkey monitor: %s modifierFlags=0x%llx keyCode=%d", [hk UTF8String], (unsigned long long)modifierFlags, keyCode);
    } else {
        NSLog(@"Failed to create CGEventTap for hotkey: %@", hk);
        logger_log("Failed to create CGEventTap for hotkey: %s", [hk UTF8String]);

        /* Diagnostic info to help troubleshoot TCC/permission issues */
        NSString *bundleID = [[NSBundle mainBundle] bundleIdentifier] ?: @"(unknown)";
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath] ?: @"(unknown)";
        NSString *version = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleShortVersionString"] ?: @"(unknown)";
        logger_log("CGEventTap diagnostic: bundlePath=%s bundleID=%s version=%s",
                   [bundlePath UTF8String], [bundleID UTF8String], [version UTF8String]);
        NSLog(@"CGEventTap diagnostic: bundlePath=%@ bundleID=%@ version=%@", bundlePath, bundleID, version);

        /* Additional runtime diagnostics to aid debugging */
        BOOL accessibilityTrusted = AXIsProcessTrusted();
        NSDictionary *axOptions = @{(__bridge id)kAXTrustedCheckOptionPrompt: @NO};
        BOOL accessibilityTrustedNoPrompt = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)axOptions);
        logger_log("AXIsProcessTrusted=%d AXIsProcessTrustedWithOptionsNoPrompt=%d", accessibilityTrusted, accessibilityTrustedNoPrompt);
        NSLog(@"AXIsProcessTrusted=%d AXIsProcessTrustedWithOptionsNoPrompt=%d", accessibilityTrusted, accessibilityTrustedNoPrompt);

        /* Log codesign and executable info (codesign output + sha256) */
        [self logCodesignAndExecutableInfo];

        /* Try a listen-only event tap as a fallback (useful to distinguish TCC denials from other failures) */
        CFMachPortRef listenTap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly,
            CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp),
            eventTapCallback,
            (__bridge void *)self
        );

        if (listenTap) {
            CFRunLoopSourceRef listenSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, listenTap, 0);
            CFRunLoopAddSource(CFRunLoopGetCurrent(), listenSource, kCFRunLoopCommonModes);
            CGEventTapEnable(listenTap, true);
            logger_log("Fallback: successfully created listen-only CGEventTap (may indicate permission restrictions on non-listen taps)");
            NSLog(@"Fallback: created listen-only tap=%p (note: listen-only cannot modify events)", listenTap);
            /* Keep listenTap around briefly to observe behavior then release */
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                if (listenTap) {
                    CGEventTapEnable(listenTap, false);
                    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), listenSource, kCFRunLoopCommonModes);
                    CFRelease(listenSource);
                    CFRelease(listenTap);
                }
            });
        } else {
            logger_log("Fallback listen-only CGEventTap creation also failed");
            NSLog(@"Fallback listen-only CGEventTap creation also failed");
        }

        /* Offer the user direct links to the relevant privacy panes (Input Monitoring and Accessibility) */
        [self setupHIDFallback];
        [self promptOpenPrivacyPanes];
    }
}

/* Register Carbon hotkey - proven approach based on working example */
- (void)registerCarbonHotkey {
    /* Unregister existing if present */
    [self unregisterCarbonHotkey];
    
    NSString *hk = [NSString stringWithUTF8String:_config.hotkey];
    if (!hk) hk = @"";
    
    /* Basic validation: require at least one modifier to avoid accidental single-key triggers */
    BOOL hasModifier = ([hk localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Control"]);
    if (![hk length] || !hasModifier) {
        logger_log("Invalid or modifier-less hotkey '%s' - skipping Carbon registration", [hk UTF8String]);
        return;
    }
    
    /* Parse hotkey string to Carbon key code and modifiers */
    UInt32 keyCode = 0;
    UInt32 modifiers = 0;
    
    /* Enhanced key mapping based on existing parseKeyCode logic */
    if ([hk containsString:@"Slash"]) {
        keyCode = kVK_ANSI_Slash; // 44
    } else if ([hk containsString:@"Space"]) {
        keyCode = kVK_Space; // 49  
    } else if ([hk containsString:@"Return"] || [hk containsString:@"Enter"]) {
        keyCode = kVK_Return; // 36
    } else if ([hk containsString:@"Escape"]) {
        keyCode = kVK_Escape; // 53
    } else if ([hk containsString:@"F1"]) {
        keyCode = kVK_F1; // 122
    } else if ([hk containsString:@"F2"]) {
        keyCode = kVK_F2; // 120
    } else if ([hk containsString:@"F3"]) {
        keyCode = kVK_F3; // 99
    } else if ([hk containsString:@"F4"]) {
        keyCode = kVK_F4; // 118
    } else {
        logger_log("Unsupported key in hotkey: %s", [hk UTF8String]);
        return;
    }
    
    /* Parse modifiers - use Carbon constants directly */
    if ([hk localizedCaseInsensitiveContainsString:@"Command"]) modifiers |= cmdKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Option"]) modifiers |= optionKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Shift"]) modifiers |= shiftKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Control"]) modifiers |= controlKey;
    
    if (modifiers == 0) {
        logger_log("No modifiers in hotkey: %s", [hk UTF8String]);
        return;
    }
    
    /* Install Carbon event handler - following working example pattern */
    EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    InstallEventHandler(GetApplicationEventTarget(), CarbonHotkeyHandler, 1, &eventType, 
                       (__bridge void *)self, NULL);
    
    /* Register the hotkey with system event target */
    EventHotKeyID hotKeyID = { 'KLOH', 1 };
    OSStatus status = RegisterEventHotKey(keyCode, modifiers, hotKeyID, 
                                         GetApplicationEventTarget(), 0, &_carbonHotKey);
    
    if (status == noErr) {
        _carbonHotkeyActive = YES;
        logger_log("Carbon hotkey registered successfully: %s (keyCode=%u modifiers=0x%x)", 
                  [hk UTF8String], keyCode, modifiers);
        NSLog(@"Carbon hotkey registered: %@", hk);
    } else {
        logger_log("Carbon hotkey registration failed: %s (OSStatus=%d)", [hk UTF8String], (int)status);
        NSLog(@"Failed to register Carbon hotkey: %@ (OSStatus=%d)", hk, (int)status);
    }
}

- (void)unregisterCarbonHotkey {
    if (_carbonHotkeyActive && _carbonHotKey) {
        UnregisterEventHotKey(_carbonHotKey);
        _carbonHotKey = NULL;
        _carbonHotkeyActive = NO;
        logger_log("Carbon hotkey unregistered");
    }
}

/* Unregister the current hotkey if present */
- (void)unregisterCurrentHotkey {
    if (_eventTap) {
        NSLog(@"unregisterCurrentHotkey: disabling CGEventTap");
        CGEventTapEnable(_eventTap, false);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), _runLoopSource, kCFRunLoopCommonModes);
        CFRelease(_runLoopSource);
        CFRelease(_eventTap);
        _eventTap = NULL;
        _runLoopSource = NULL;
    }
}

/* Basic hotkey string validation used by prefs before attempting registration */
- (BOOL)isValidHotkeyString:(NSString *)hk {
    if (!hk || [hk length] == 0) return NO;
    BOOL hasModifier = ([hk localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Control"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Ctrl"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Cmd"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Alt"]);
    if (!hasModifier) return NO;
    // Require at least one non-modifier token
    NSArray *parts = [hk componentsSeparatedByString:@"+"];
    if ([parts count] < 2) return NO;
    return YES;
}

- (void)setupStatusItem {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    _statusItem.button.title = @"KLO";
    _statusItem.menu = [self buildMenu];
}

- (NSMenu *)buildMenu {
    NSMenu *menu = [[NSMenu alloc] init];
    
    /* Test overlay display */
    NSMenuItem *showOverlayItem = [[NSMenuItem alloc] initWithTitle:@"Show Overlay (Test)" 
                                                             action:@selector(testShowOverlay:) 
                                                      keyEquivalent:@""];
    [showOverlayItem setTarget:self];
    [menu addItem:showOverlayItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
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
    
    /* Custom pixel size indicator (opens Preferences) */
    NSString *customTitle = [NSString stringWithFormat:@"Size: custom %dx%d", _config.custom_width_px, _config.custom_height_px];
    NSMenuItem *customItem = [[NSMenuItem alloc] initWithTitle:customTitle action:@selector(openPreferences:) keyEquivalent:@""];
    [customItem setTarget:self];
    [customItem setState:_config.use_custom_size ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:customItem];
    
    /* Preferences */
    NSMenuItem *prefsItem = [[NSMenuItem alloc] initWithTitle:@"Preferences..." action:@selector(openPreferences:) keyEquivalent:@","];
    [prefsItem setTarget:self];
    [menu addItem:prefsItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" 
                                                      action:@selector(quit:) 
                                               keyEquivalent:@""];
    [quitItem setTarget:self];
    [menu addItem:quitItem];
    
    return menu;
}

- (void)showOverlay {
    // NSLog(@"showOverlay: called, _visible=%d _panel=%p", _visible, _panel);
    if (_visible || !_panel) {
        // NSLog(@"showOverlay: early return - already visible or no panel");
        return;
    }
    
    /* Use fixed center position for testing */
    NSScreen *screen = [NSScreen mainScreen];
    NSRect screenFrame = [screen visibleFrame];
    NSRect panelFrame = [_panel frame];
    
    /* Center the panel on screen */
    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
    panelFrame.origin.y = NSMidY(screenFrame) - NSHeight(panelFrame) / 2.0;
    
    [_panel setFrame:panelFrame display:YES];
    [_panel orderFrontRegardless]; /* Show without activating */
    _visible = YES;
    
    NSLog(@"showOverlay: overlay shown at (%.0f, %.0f) size=(%.0fx%.0f) level=%ld alpha=%.2f", 
          panelFrame.origin.x, panelFrame.origin.y, panelFrame.size.width, panelFrame.size.height, 
          (long)[_panel level], [_panel alphaValue]);
    
    /* Write debug to file */
    NSString *posDebug = [NSString stringWithFormat:@"SIMPLE OVERLAY: x=%.0f y=%.0f w=%.0fx%.0f level=%ld alpha=%.2f visible=%d\nScreen: x=%.0f y=%.0f w=%.0fx%.0f\n", 
                         panelFrame.origin.x, panelFrame.origin.y, panelFrame.size.width, panelFrame.size.height,
                         (long)[_panel level], [_panel alphaValue], [_panel isVisible],
                         screenFrame.origin.x, screenFrame.origin.y, screenFrame.size.width, screenFrame.size.height];
    [posDebug writeToFile:@"/tmp/kbd_overlay_position.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)hideOverlay {
    // NSLog(@"hideOverlay: called, _visible=%d _panel=%p", _visible, _panel);
    if (!_visible || !_panel) {
        // NSLog(@"hideOverlay: early return - not visible or no panel");
        return;
    }
    [_panel orderOut:nil];
    _visible = NO;
    // NSLog(@"hideOverlay: overlay hidden");
}

- (void)toggleOverlay {
    if (_visible) {
        [self hideOverlay];
    } else {
        [self showOverlay];
    }
}

- (void)testShowOverlay:(id)sender {
    NSLog(@"testShowOverlay: manually showing overlay for testing");
    NSLog(@"testShowOverlay: _panel=%p _overlayImage=%p _visible=%d", _panel, _overlayImage, _visible);
    NSLog(@"testShowOverlay: overlay dimensions: %dx%d", _overlay.width, _overlay.height);
    
    /* Write debug info to a file we can easily read */
    NSString *debugInfo = [NSString stringWithFormat:@"DEBUG: panel=%p image=%p visible=%d dimensions=%dx%d\n", 
                          _panel, _overlayImage, _visible, _overlay.width, _overlay.height];
    [debugInfo writeToFile:@"/tmp/kbd_overlay_debug.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
    
    [self showOverlay];
    
    /* Auto-hide after 5 seconds for testing */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        NSLog(@"testShowOverlay: auto-hiding overlay after 5 seconds");
        [self hideOverlay];
    });
}

- (void)togglePersistent:(id)sender {
    _config.persistent = !_config.persistent;
    [sender setState:_config.persistent ? NSControlStateValueOn : NSControlStateValueOff];
    
    /* Persist change */
    save_config(&_config, NULL);
    
    if (_config.persistent && _visible) {
        [self hideOverlay];
    }
}

- (void)toggleInvert:(id)sender {
    _config.invert = !_config.invert;
    [sender setState:_config.invert ? NSControlStateValueOn : NSControlStateValueOff];
    
    /* Persist change */
    save_config(&_config, NULL);
    
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
    
    /* Persist change */
    save_config(&_config, NULL);
    
    /* Reload overlay with new scale */
    free_overlay(&_overlay);
    [self loadOverlay];
    [self createOverlayWindow];
    
    /* Update menu checkmarks */
    _statusItem.menu = [self buildMenu];
    
    NSLog(@"Changed scale to %.0f%%", scale * 100);
}

/* Preferences UI - minimal and local (opacity + hotkey text). Hotkey changes require restart to activate in this MVP. */

- (void)openPreferences:(id)sender {
    if (_prefsWindow) {
        NSLog(@"openPreferences: bringing existing prefs window %p to front", _prefsWindow);
        [_prefsWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        return;
    }

    NSRect rect = NSMakeRect(0, 0, 360, 160);
    _prefsWindow = [[LoggingWindow alloc] initWithContentRect:rect
                                               styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    NSLog(@"openPreferences: created prefs window %p", _prefsWindow);
    [_prefsWindow setTitle:@"Preferences"];
    /* Ensure we receive windowWillClose: notifications so we can clear references safely */
    [_prefsWindow setDelegate:self];
    
    NSView *content = [_prefsWindow contentView];
    
    /* Opacity label */
    NSTextField *opacityLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 110, 80, 24)];
    [opacityLabel setStringValue:@"Opacity:"];
    [opacityLabel setBezeled:NO];
    [opacityLabel setDrawsBackground:NO];
    [opacityLabel setEditable:NO];
    [opacityLabel setSelectable:NO];
    [content addSubview:opacityLabel];
    
    /* Opacity slider */
    _prefOpacitySlider = [[NSSlider alloc] initWithFrame:NSMakeRect(100, 110, 240, 24)];
    [_prefOpacitySlider setMinValue:0.0];
    [_prefOpacitySlider setMaxValue:1.0];
    [_prefOpacitySlider setDoubleValue:_config.opacity];
    [_prefOpacitySlider setTarget:self];
    [_prefOpacitySlider setAction:@selector(prefOpacityChanged:)];
    [content addSubview:_prefOpacitySlider];
    
    /* Hotkey label */
    NSTextField *hotkeyLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 70, 80, 24)];
    [hotkeyLabel setStringValue:@"Hotkey:"];
    [hotkeyLabel setBezeled:NO];
    [hotkeyLabel setDrawsBackground:NO];
    [hotkeyLabel setEditable:NO];
    [hotkeyLabel setSelectable:NO];
    [content addSubview:hotkeyLabel];
    
    /* Hotkey capture field */
    _prefHotkeyField = [[HotkeyCaptureField alloc] initWithFrame:NSMakeRect(100, 70, 240, 24)];
    [_prefHotkeyField setTarget:self];
    [_prefHotkeyField setAction:@selector(prefHotkeyCaptured:)];
    NSString *hk = [NSString stringWithUTF8String:_config.hotkey];
    if (!hk) hk = @"";
    [_prefHotkeyField setStringValue:hk];
    [_prefHotkeyField setPlaceholderString:@"Click and press a hotkey"];
    [content addSubview:_prefHotkeyField];
    
    /* Use custom pixel size checkbox */
    NSButton *useCustomBtn = [[NSButton alloc] initWithFrame:NSMakeRect(12, 40, 140, 24)];
    [useCustomBtn setButtonType:NSButtonTypeSwitch];
    [useCustomBtn setTitle:@"Use custom size"];
    [useCustomBtn setTarget:self];
    [useCustomBtn setAction:@selector(prefUseCustomSizeToggled:)];
    [useCustomBtn setState:_config.use_custom_size ? NSControlStateValueOn : NSControlStateValueOff];
    _prefUseCustomSizeCheckbox = useCustomBtn;
    [content addSubview:useCustomBtn];
    
    /* Width label and field */
    NSTextField *widthLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(160, 42, 20, 24)];
    [widthLabel setStringValue:@"W:"];
    [widthLabel setBezeled:NO];
    [widthLabel setDrawsBackground:NO];
    [widthLabel setEditable:NO];
    [widthLabel setSelectable:NO];
    [content addSubview:widthLabel];
    
    _prefWidthField = [[NSTextField alloc] initWithFrame:NSMakeRect(185, 40, 70, 24)];
    [_prefWidthField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_width_px]];
    [_prefWidthField setEnabled:_config.use_custom_size ? YES : NO];
    [content addSubview:_prefWidthField];
    
    /* Height label and field */
    NSTextField *heightLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(260, 42, 20, 24)];
    [heightLabel setStringValue:@"H:"];
    [heightLabel setBezeled:NO];
    [heightLabel setDrawsBackground:NO];
    [heightLabel setEditable:NO];
    [heightLabel setSelectable:NO];
    [content addSubview:heightLabel];
    
    _prefHeightField = [[NSTextField alloc] initWithFrame:NSMakeRect(285, 40, 55, 24)];
    [_prefHeightField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_height_px]];
    [_prefHeightField setEnabled:_config.use_custom_size ? YES : NO];
    [content addSubview:_prefHeightField];
    
    /* Inline error label (shows validation/registration messages while prefs are open) */
    _prefErrorLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 44, 328, 18)];
    [_prefErrorLabel setBezeled:NO];
    [_prefErrorLabel setDrawsBackground:NO];
    [_prefErrorLabel setEditable:NO];
    [_prefErrorLabel setSelectable:NO];
    /* Set red color for visibility */
    [_prefErrorLabel setTextColor:[NSColor systemRedColor]];
    [_prefErrorLabel setStringValue:@""];
    [content addSubview:_prefErrorLabel];

    /* Restore Defaults button */
    NSButton *restoreBtn = [[NSButton alloc] initWithFrame:NSMakeRect(12, 12, 110, 28)];
    [restoreBtn setTitle:@"Restore Defaults"];
    [restoreBtn setBezelStyle:NSBezelStyleRounded];
    [restoreBtn setTarget:self];
    [restoreBtn setAction:@selector(prefRestoreDefaults:)];
    _prefRestoreDefaultsBtn = restoreBtn;
    /* Disable restore when current config already equals defaults */
    {
        Config defaults = get_default_config();
        NSString *curHot = [NSString stringWithUTF8String:_config.hotkey ?: ""];
        NSString *defHot = [NSString stringWithUTF8String:defaults.hotkey ?: ""];
        BOOL hkEqual = (curHot && defHot && [curHot isEqualToString:defHot]);
        BOOL is_default = (_config.use_custom_size == defaults.use_custom_size &&
                           _config.custom_width_px == defaults.custom_width_px &&
                           _config.custom_height_px == defaults.custom_height_px &&
                           fabsf(_config.opacity - defaults.opacity) < 0.001f &&
                           _config.invert == defaults.invert &&
                           _config.persistent == defaults.persistent &&
                           fabsf(_config.scale - defaults.scale) < 0.001f &&
                           hkEqual);
        [_prefRestoreDefaultsBtn setEnabled: !is_default];
    }
    [content addSubview:restoreBtn];

    /* Apply button */
    NSButton *applyBtn = [[NSButton alloc] initWithFrame:NSMakeRect(260, 12, 80, 28)];
    [applyBtn setTitle:@"Apply"];
    [applyBtn setBezelStyle:NSBezelStyleRounded];
    [applyBtn setTarget:self];
    [applyBtn setAction:@selector(prefApply:)];
    [content addSubview:applyBtn];
    
    /* Clear hotkey button */
    NSButton *clearBtn = [[NSButton alloc] initWithFrame:NSMakeRect(170, 12, 80, 28)];
    [clearBtn setTitle:@"Clear"];
    [clearBtn setBezelStyle:NSBezelStyleRounded];
    [clearBtn setTarget:self];
    [clearBtn setAction:@selector(prefClearHotkey:)];
    [content addSubview:clearBtn];
    
    /* Close button */
    NSButton *closeBtn = [[NSButton alloc] initWithFrame:NSMakeRect(95, 12, 80, 28)];
    [closeBtn setTitle:@"Close"];
    [closeBtn setBezelStyle:NSBezelStyleRounded];
    [closeBtn setTarget:self];
    [closeBtn setAction:@selector(prefClose:)];
    [content addSubview:closeBtn];
    
    [_prefsWindow center];
    [NSApp activateIgnoringOtherApps:YES];
    [_prefsWindow makeKeyAndOrderFront:nil];
}

- (void)prefOpacityChanged:(NSSlider *)sender {
    _config.opacity = (float)[sender doubleValue];
    /* Clear inline error (live feedback) */
    if (_prefErrorLabel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefErrorLabel setStringValue:@""];
        });
    }
    /* Live preview */
    [self updateOverlayImage];
}

- (void)prefHotkeyCaptured:(id)sender {
    /* Received a captured hotkey from the HotkeyCaptureField.
       Clear any inline error so the user can see fresh validation results on Apply. */
    if (_prefErrorLabel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefErrorLabel setStringValue:@""];
        });
    }
    // No immediate action required; user must press Apply to persist.
}

/* Clear the hotkey field */
- (void)prefClearHotkey:(id)sender {
    if (_prefHotkeyField) {
        [_prefHotkeyField clearHotkey];
    }
}

/* Toggle handler for the "Use custom size" checkbox */
- (void)prefUseCustomSizeToggled:(id)sender {
    BOOL on = ([_prefUseCustomSizeCheckbox state] == NSControlStateValueOn);
    if (_prefWidthField) [_prefWidthField setEnabled:on];
    if (_prefHeightField) [_prefHeightField setEnabled:on];
}

- (void)prefApply:(id)sender {
    /* Save hotkey text and apply immediately by re-registering */
    NSString *hkStr = nil;
    if ([_prefHotkeyField respondsToSelector:@selector(currentHotkey)]) {
        hkStr = (NSString *)[_prefHotkeyField performSelector:@selector(currentHotkey)];
    } else {
        hkStr = [_prefHotkeyField stringValue];
    }
    if (!hkStr || [hkStr length] == 0) {
        if (_prefErrorLabel) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [_prefErrorLabel setStringValue:@"Please capture a hotkey before applying."];
            });
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:@"Invalid Hotkey"];
                [alert setInformativeText:@"Please capture a hotkey before applying."];
                [alert runModal];
            });
        }
        return;
    }
    BOOL hasModifier = ([hkStr localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Control"]);
    if (!hasModifier) {
        if (_prefErrorLabel) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [_prefErrorLabel setStringValue:@"Hotkey must include a modifier (Command/Option/Shift/Control)."];
            });
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:@"Hotkey must include a modifier"];
                [alert setInformativeText:@"Use at least one modifier key (Command, Option, Shift, or Control) to avoid accidental triggers."];
                [alert runModal];
            });
        }
        return;
    }

    NSLog(@"prefApply: applying hotkey='%@' opacity=%.2f", hkStr, (float)[_prefOpacitySlider doubleValue]);

    const char *hk = [hkStr UTF8String];
    if (hk) {
        size_t len = strlen(hk);
        if (len >= sizeof(_config.hotkey)) len = sizeof(_config.hotkey) - 1;
        memcpy(_config.hotkey, hk, len);
        _config.hotkey[len] = '\0';
    }
    /* Save opacity from slider */
    _config.opacity = (float)[_prefOpacitySlider doubleValue];

    /* Read custom size settings if present */
    int prev_use_custom = _config.use_custom_size;
    int prev_w = _config.custom_width_px;
    int prev_h = _config.custom_height_px;

    if (_prefUseCustomSizeCheckbox) {
        BOOL useCustom = ([_prefUseCustomSizeCheckbox state] == NSControlStateValueOn);
        _config.use_custom_size = useCustom ? 1 : 0;
        if (useCustom) {
            int w = (int)[_prefWidthField intValue];
            int h = (int)[_prefHeightField intValue];
            if (w <= 0 || h <= 0) {
                if (_prefErrorLabel) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [_prefErrorLabel setStringValue:@"Width and height must be positive integers."];
                    });
                } else {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        NSAlert *alert = [[NSAlert alloc] init];
                        [alert setMessageText:@"Invalid size"];
                        [alert setInformativeText:@"Please enter positive integer values for width and height."];
                        [alert runModal];
                    });
                }
                return;
            }
            _config.custom_width_px = w;
            _config.custom_height_px = h;
        }
    }

    save_config(&_config, NULL);

    /* Clear inline error after successful save */
    if (_prefErrorLabel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefErrorLabel setStringValue:@""];
        });
    }
    /* Update Restore Defaults button enabled state */
    if (_prefRestoreDefaultsBtn) {
        Config defaults = get_default_config();
        NSString *curHot = [NSString stringWithUTF8String:_config.hotkey ?: ""];
        NSString *defHot = [NSString stringWithUTF8String:defaults.hotkey ?: ""];
        BOOL hkEqual = (curHot && defHot && [curHot isEqualToString:defHot]);
        BOOL is_default = (_config.use_custom_size == defaults.use_custom_size &&
                           _config.custom_width_px == defaults.custom_width_px &&
                           _config.custom_height_px == defaults.custom_height_px &&
                           fabsf(_config.opacity - defaults.opacity) < 0.001f &&
                           _config.invert == defaults.invert &&
                           _config.persistent == defaults.persistent &&
                           fabsf(_config.scale - defaults.scale) < 0.001f &&
                           hkEqual);
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefRestoreDefaultsBtn setEnabled: !is_default];
        });
    }

    /* If sizing changed, reload overlay and recreate panel to reflect new pixel dimensions */
    if (prev_use_custom != _config.use_custom_size || prev_w != _config.custom_width_px || prev_h != _config.custom_height_px) {
        free_overlay(&_overlay);
        [self loadOverlay];
        [self createOverlayWindow];
        /* Update menu so checked size items remain consistent */
        _statusItem.menu = [self buildMenu];
    }

    /* Re-register Carbon hotkey immediately so changes take effect without restart */
    [self registerCarbonHotkey];
    [self updateOverlayImage];
}

- (void)prefRestoreDefaults:(id)sender {
    /* Reset config to defaults, persist, refresh UI and overlay */
    _config = get_default_config();
    save_config(&_config, NULL);

    /* Update prefs UI fields if visible */
    if (_prefHotkeyField) {
        NSString *hk = [NSString stringWithUTF8String:_config.hotkey];
        if (!hk) hk = @"";
        [_prefHotkeyField setStringValue:hk];
    }
    if (_prefOpacitySlider) [_prefOpacitySlider setDoubleValue:_config.opacity];
    if (_prefUseCustomSizeCheckbox) [_prefUseCustomSizeCheckbox setState:_config.use_custom_size ? NSControlStateValueOn : NSControlStateValueOff];
    if (_prefWidthField) [_prefWidthField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_width_px]];
    if (_prefHeightField) [_prefHeightField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_height_px]];
    if (_prefErrorLabel) [_prefErrorLabel setStringValue:@""];
    if (_prefRestoreDefaultsBtn) [_prefRestoreDefaultsBtn setEnabled:NO];

    /* Reload overlay and UI */
    free_overlay(&_overlay);
    [self loadOverlay];
    [self createOverlayWindow];
    _statusItem.menu = [self buildMenu];

    /* Re-register Carbon hotkey (defaults) and update preview */
    [self registerCarbonHotkey];
    [self updateOverlayImage];

    /* Re-enable restore button */
    if (_prefRestoreDefaultsBtn) [_prefRestoreDefaultsBtn setEnabled:YES];
}

- (void)prefClose:(id)sender {
    if (_prefsWindow) {
        NSLog(@"prefClose: closing prefs window %p", _prefsWindow);
        /* Close the window on the main thread and let windowWillClose: clear references.
           Do not nil out UI ivars here to avoid touching objects while Cocoa is in the
           middle of its teardown which can lead to use-after-free. */
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefsWindow close];
        });
    }
}

- (void)quit:(id)sender {
    [NSApp terminate:nil];
}

- (BOOL)isPersistent {
    return _config.persistent;
}

- (void)windowWillClose:(NSNotification *)notification {
    /* Clear retained references when the preferences window actually closes.
       This avoids touching the window pointer while Cocoa is still handling the
       close event (which can lead to use-after-free when the window is released
       during event dispatch). */
    NSLog(@"windowWillClose: prefs window %p will close", notification.object);
    if (notification.object == _prefsWindow) {
        _prefsWindow = nil;
        _prefHotkeyField = nil;
        _prefOpacitySlider = nil;
        _prefUseCustomSizeCheckbox = nil;
        _prefWidthField = nil;
        _prefHeightField = nil;
        _prefErrorLabel = nil;
        _prefRestoreDefaultsBtn = nil;
    }
}

@end
