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
    NSImage *_overlayImage;
    BOOL _visible;
    /* Preferences UI state */
    NSWindow *_prefsWindow;
    HotkeyCaptureField *_prefHotkeyField;
    NSSlider *_prefOpacitySlider;
    NSTextField *_prefOpacityLabel;
    NSSlider *_prefScaleSlider;
    NSTextField *_prefScaleLabel;
    NSButton *_prefUseCustomSizeCheckbox;
    NSTextField *_prefWidthField;
    NSTextField *_prefHeightField;
    NSTextField *_prefErrorLabel;
    NSButton *_prefRestoreDefaultsBtn;
    NSPopUpButton *_prefAutoHidePopup;
    NSPopUpButton *_prefPositionPopup;
    NSButton *_prefClickThroughCheckbox;
    NSButton *_prefAlwaysOnTopCheckbox;
    /* Preview buffer (reused) so live preview does not mutate original overlay pixels */
    unsigned char *_previewBuffer;
    size_t _previewBufferSize;
    
    /* Carbon hotkey registration (reliable, no permissions needed) */
    EventHotKeyRef _carbonHotKey;
    BOOL _carbonHotkeyActive;
    EventHandlerRef _carbonEventHandlerRef;
    BOOL _carbonHandlerInstalled;
    NSTimer *_carbonHideTimer;
    NSTimer *_autoHideTimer;

    /* Original PNG data and last-used sizing config */
    unsigned char *_originalImageData;
    int _originalImageSize;
    float _lastScale;
    int _lastCustomWidth;
    int _lastCustomHeight;
    int _lastUseCustom;
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

/* Handle Carbon hotkey press - called from main queue */
- (void)cancelAutoHideTimers {
    if (_autoHideTimer) {
        [_autoHideTimer invalidate];
        _autoHideTimer = nil;
    }
    if (_carbonHideTimer) {
        [_carbonHideTimer invalidate];
        _carbonHideTimer = nil;
    }
}

/* Schedule a single-shot auto-hide timer (seconds > 0). Cancels any existing timers first. */
- (void)scheduleAutoHideForSeconds:(float)secs {
    [self cancelAutoHideTimers];
    if (secs <= 0.0f) return;
    __weak typeof(self) weakSelf = self;
    _autoHideTimer = [NSTimer scheduledTimerWithTimeInterval:secs repeats:NO block:^(NSTimer * _Nonnull t) {
        __strong typeof(self) s = weakSelf;
        if (!s) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            [s hideOverlay];
            s->_autoHideTimer = nil;
        });
    }];
}

/* Handle Carbon hotkey press - unified with auto-hide semantics.
   - If auto_hide == 0.0 => persistent toggle behavior (legacy)
   - Else => show overlay and schedule auto-hide after configured seconds
*/
- (void)handleCarbonHotkeyPressed {
    // Cancel any existing timers
    [self cancelAutoHideTimers];

    float ah = _config.auto_hide;
    if (ah <= 0.0f) {
        // Persistent toggle
        if ([_panel isVisible]) {
            [self hideOverlay];
        } else {
            [self showOverlay];
        }
    } else {
        // Timed show
        if ([_panel isVisible]) {
            [self hideOverlay];
        } else {
            [self showOverlay];
            [self scheduleAutoHideForSeconds:ah];
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

    _originalImageData = NULL;
    _originalImageSize = 0;
    _lastScale = -1.0f;
    _lastCustomWidth = -1;
    _lastCustomHeight = -1;
    _lastUseCustom = -1;

    [self loadOverlay];
    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;
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
    if (_originalImageData) {
        free(_originalImageData);
        _originalImageData = NULL;
        _originalImageSize = 0;
    }

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
    
    if (!_originalImageData) {
        /* Try multiple locations for keymap.png */
        NSArray *searchPaths = @[
            @"keymap.png",
            @"assets/keymap.png",
            @"../assets/keymap.png",
            [[NSBundle mainBundle] pathForResource:@"keymap" ofType:@"png"] ?: @"",
        ];

        for (NSString *pathStr in searchPaths) {
            if ([pathStr length] == 0) continue;
            NSData *d = [NSData dataWithContentsOfFile:pathStr];
            if (d) {
                _originalImageSize = (int)[d length];
                _originalImageData = malloc(_originalImageSize);
                if (_originalImageData) {
                    memcpy(_originalImageData, [d bytes], _originalImageSize);
                    NSLog(@"Loaded overlay from: %@", pathStr);
                    break;
                }
            }
        }

        if (!_originalImageData) {
            int size;
            const unsigned char *data = get_default_keymap(&size);
            if (data && size > 0) {
                _originalImageData = malloc(size);
                if (_originalImageData) {
                    memcpy(_originalImageData, data, size);
                    _originalImageSize = size;
                    NSLog(@"Using embedded keymap (build-time)");
                }
            }
        }
    }

    if (!_originalImageData || _originalImageSize <= 0) {
        NSString *errorTitle = @"Image Loading Failed";
        NSString *errorMsg = @"Could not find keymap.png in any location";

        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:errorTitle];
        [alert setInformativeText:[NSString stringWithFormat:@"%@\n\nPlease place keymap.png in one of these locations:\n• assets/ folder (before building)\n• Project root directory\n• App bundle resources", errorMsg]];
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }

    OverlayError result = load_overlay_mem(_originalImageData, _originalImageSize,
                                           max_w, max_h, &_overlay);

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

- (void)reloadOverlayIfNeeded {
    if (fabsf(_config.scale - _lastScale) < 0.001f &&
        _config.custom_width_px == _lastCustomWidth &&
        _config.custom_height_px == _lastCustomHeight &&
        _config.use_custom_size == _lastUseCustom) {
        return; /* No changes */
    }

    _lastScale = _config.scale;
    _lastCustomWidth = _config.custom_width_px;
    _lastCustomHeight = _config.custom_height_px;
    _lastUseCustom = _config.use_custom_size;

    free_overlay(&_overlay);
    [self loadOverlay];
    [self createOverlayWindow];
    [self updateOverlayImage];
}

- (void)createOverlayWindow {
    CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
    NSSize size = NSMakeSize(_overlay.width / scale, _overlay.height / scale);
    
    /* Compute initial position based on persisted config */
    NSScreen *screen = [NSScreen mainScreen];
    NSRect screenFrame = [screen visibleFrame];
    NSRect rect = NSMakeRect(0, 0, size.width, size.height);
    switch (_config.position_mode) {
        case 0: /* Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMidY(screenFrame) - size.height / 2.0;
            break;
        case 1: /* Top-Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMaxY(screenFrame) - size.height - (_config.position_y);
            break;
        case 2: /* Bottom-Center */
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0;
            rect.origin.y = NSMinY(screenFrame) + (_config.position_y);
            break;
        case 3: /* Custom */
        default:
            rect.origin.x = NSMidX(screenFrame) - size.width / 2.0 + _config.position_x;
            rect.origin.y = NSMinY(screenFrame) + _config.position_y;
            break;
    }
    
    NSLog(@"createOverlayWindow: scale=%.1f size=(%.0fx%.0f) rect=(%.0f,%.0f,%.0fx%.0f)", 
          scale, size.width, size.height, rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    
    /* Use OverlayWindow for proper non-activating behavior */
    _panel = [[OverlayWindow alloc] initWithContentRect:rect
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [_panel setOpaque:NO];
    [_panel setBackgroundColor:[NSColor clearColor]]; /* Transparent background */
    /* Window level follows persisted always_on_top flag */
    if (_config.always_on_top) {
        [_panel setLevel:NSScreenSaverWindowLevel];
    } else {
        [_panel setLevel:NSNormalWindowLevel];
    }
    /* Click-through */
    if (_config.click_through) {
        [_panel setIgnoresMouseEvents:YES];
    } else {
        [_panel setIgnoresMouseEvents:NO];
    }
    /* Opacity */
    [_panel setAlphaValue:_config.opacity];
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

/* Convert HID event into the existing CGEvent-style handler so we can reuse hotkey logic.
   For non-modifier keys we synthesize filteredFlags from _hidModifierFlags and call
   handleCGEventWithKeyCode:flags:filteredFlags:isKeyDown:
*/

/* IOHID callback now converts HID usages into synthesized modifier state and key events */



/* Existing event handler (unchanged) */

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

    /* Use Carbon as the primary, reliable hotkey mechanism */
    [self registerCarbonHotkey];
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
    if (!_carbonHandlerInstalled) {
        InstallEventHandler(GetApplicationEventTarget(), CarbonHotkeyHandler, 1, &eventType,
                           (__bridge void *)self, &_carbonEventHandlerRef);
        _carbonHandlerInstalled = YES;
        logger_log("Carbon event handler installed");
    }
    
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
    if (_carbonHandlerInstalled && _carbonEventHandlerRef) {
        RemoveEventHandler(_carbonEventHandlerRef);
        _carbonEventHandlerRef = NULL;
        _carbonHandlerInstalled = NO;
        logger_log("Carbon event handler removed");
    }
}

/* Unregister the current hotkey if present */

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
    
    /* Show Keymap toggle with state indicator */
    NSMenuItem *showKeymapItem = [[NSMenuItem alloc] initWithTitle:@"Show Keymap" 
                                                            action:@selector(toggleShowKeymap:) 
                                                     keyEquivalent:@""];
    [showKeymapItem setTarget:self];
    [showKeymapItem setState:_visible ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:showKeymapItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    /* Scale submenu */
    NSMenuItem *scaleParent = [[NSMenuItem alloc] initWithTitle:@"Scale" action:nil keyEquivalent:@""];
    NSMenu *scaleMenu = [[NSMenu alloc] initWithTitle:@"Scale"];
    
    NSMenuItem *scale75 = [[NSMenuItem alloc] initWithTitle:@"75%" action:@selector(setSize75:) keyEquivalent:@""];
    [scale75 setTarget:self];
    [scale75 setState:(fabsf(_config.scale - 0.75f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [scaleMenu addItem:scale75];
    
    NSMenuItem *scale100 = [[NSMenuItem alloc] initWithTitle:@"100%" action:@selector(setSize100:) keyEquivalent:@""];
    [scale100 setTarget:self];
    [scale100 setState:(fabsf(_config.scale - 1.0f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [scaleMenu addItem:scale100];
    
    NSMenuItem *scale125 = [[NSMenuItem alloc] initWithTitle:@"125%" action:@selector(setSize125:) keyEquivalent:@""];
    [scale125 setTarget:self];
    [scale125 setState:(fabsf(_config.scale - 1.25f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [scaleMenu addItem:scale125];
    
    NSMenuItem *scale150 = [[NSMenuItem alloc] initWithTitle:@"150%" action:@selector(setSize150:) keyEquivalent:@""];
    [scale150 setTarget:self];
    [scale150 setState:(fabsf(_config.scale - 1.5f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [scaleMenu addItem:scale150];
    
    NSMenuItem *scaleFitScreen = [[NSMenuItem alloc] initWithTitle:@"Fit Screen" action:@selector(setSizeFitScreen:) keyEquivalent:@""];
    [scaleFitScreen setTarget:self];
    [scaleFitScreen setState:(_config.use_custom_size && _config.scale > 1.9f) ? NSControlStateValueOn : NSControlStateValueOff];
    [scaleMenu addItem:scaleFitScreen];
    
    [scaleParent setSubmenu:scaleMenu];
    [menu addItem:scaleParent];
    
    /* Opacity submenu */
    NSMenuItem *opacityParent = [[NSMenuItem alloc] initWithTitle:@"Opacity" action:nil keyEquivalent:@""];
    NSMenu *opacityMenu = [[NSMenu alloc] initWithTitle:@"Opacity"];
    
    NSMenuItem *opacity50 = [[NSMenuItem alloc] initWithTitle:@"50%" action:@selector(setOpacity50:) keyEquivalent:@""];
    [opacity50 setTarget:self];
    [opacity50 setState:(fabsf(_config.opacity - 0.5f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [opacityMenu addItem:opacity50];
    
    NSMenuItem *opacity70 = [[NSMenuItem alloc] initWithTitle:@"70%" action:@selector(setOpacity70:) keyEquivalent:@""];
    [opacity70 setTarget:self];
    [opacity70 setState:(fabsf(_config.opacity - 0.7f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [opacityMenu addItem:opacity70];
    
    NSMenuItem *opacity85 = [[NSMenuItem alloc] initWithTitle:@"85%" action:@selector(setOpacity85:) keyEquivalent:@""];
    [opacity85 setTarget:self];
    [opacity85 setState:(fabsf(_config.opacity - 0.85f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [opacityMenu addItem:opacity85];
    
    NSMenuItem *opacity100 = [[NSMenuItem alloc] initWithTitle:@"100%" action:@selector(setOpacity100:) keyEquivalent:@""];
    [opacity100 setTarget:self];
    [opacity100 setState:(fabsf(_config.opacity - 1.0f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [opacityMenu addItem:opacity100];
    
    [opacityParent setSubmenu:opacityMenu];
    [menu addItem:opacityParent];
    
    /* Auto-hide submenu */
    NSMenuItem *autoHideParent = [[NSMenuItem alloc] initWithTitle:@"Auto-hide" action:nil keyEquivalent:@""];
    NSMenu *autoHideMenu = [[NSMenu alloc] initWithTitle:@"Auto-hide"];
    
    NSMenuItem *ahOff = [[NSMenuItem alloc] initWithTitle:@"Off" action:@selector(setAutoHideOff:) keyEquivalent:@""];
    [ahOff setTarget:self];
    [ahOff setState:(_config.auto_hide == 0.0f) ? NSControlStateValueOn : NSControlStateValueOff];
    [autoHideMenu addItem:ahOff];
    
    NSMenuItem *ah08 = [[NSMenuItem alloc] initWithTitle:@"0.8s" action:@selector(setAutoHide08:) keyEquivalent:@""];
    [ah08 setTarget:self];
    [ah08 setState:(fabsf(_config.auto_hide - 0.8f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [autoHideMenu addItem:ah08];
    
    NSMenuItem *ah2 = [[NSMenuItem alloc] initWithTitle:@"2.0s" action:@selector(setAutoHide2s:) keyEquivalent:@""];
    [ah2 setTarget:self];
    [ah2 setState:(fabsf(_config.auto_hide - 2.0f) < 0.001f) ? NSControlStateValueOn : NSControlStateValueOff];
    [autoHideMenu addItem:ah2];
    
    NSMenuItem *ahCustom = [[NSMenuItem alloc] initWithTitle:@"Custom..." action:@selector(openPreferences:) keyEquivalent:@""];
    [ahCustom setTarget:self];
    [autoHideMenu addItem:ahCustom];
    
    [autoHideParent setSubmenu:autoHideMenu];
    [menu addItem:autoHideParent];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    /* Preview Keymap */
    NSMenuItem *previewItem = [[NSMenuItem alloc] initWithTitle:@"Preview Keymap" 
                                                         action:@selector(previewKeymap:) 
                                                  keyEquivalent:@""];
    [previewItem setTarget:self];
    [menu addItem:previewItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    /* Preferences */
    NSMenuItem *prefsItem = [[NSMenuItem alloc] initWithTitle:@"Preferences..." action:@selector(openPreferences:) keyEquivalent:@","];
    [prefsItem setTarget:self];
    [menu addItem:prefsItem];
    
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

- (void)setAutoHideOff:(id)sender {
    _config.auto_hide = 0.0f;
    save_config(&_config, NULL);
    _statusItem.menu = [self buildMenu];
}

- (void)setAutoHide08:(id)sender {
    _config.auto_hide = 0.8f;
    save_config(&_config, NULL);
    _statusItem.menu = [self buildMenu];
}

- (void)setAutoHide2s:(id)sender {
    _config.auto_hide = 2.0f;
    save_config(&_config, NULL);
    _statusItem.menu = [self buildMenu];
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

- (void)setSize125:(id)sender {
    [self setSizeScale:1.25f sender:sender];
}

- (void)setSize150:(id)sender {
    [self setSizeScale:1.5f sender:sender];
}

- (void)setSizeFitScreen:(id)sender {
    /* Calculate scale to fit screen width */
    NSScreen *screen = [NSScreen mainScreen];
    CGFloat screenWidth = screen.frame.size.width;
    CGFloat targetWidth = screenWidth * 0.8f; // 80% of screen width
    
    if (_overlay.width > 0) {
        float fitScale = targetWidth / _overlay.width;
        _config.scale = fitScale;
        _config.use_custom_size = 0; // Use scale mode

        save_config(&_config, NULL);
        [self reloadOverlayIfNeeded];

        /* Update menu checkmarks */
        _statusItem.menu = [self buildMenu];

        NSLog(@"Fit screen: scale %.1f%% (%.0fx%.0f)", fitScale * 100, _overlay.width * fitScale, _overlay.height * fitScale);
    }
}

- (void)setSizeScale:(float)scale sender:(id)sender {
    _config.scale = scale;

    /* Persist change */
    save_config(&_config, NULL);

    [self reloadOverlayIfNeeded];

    /* Update menu checkmarks */
    _statusItem.menu = [self buildMenu];

    NSLog(@"Changed scale to %.0f%%", scale * 100);
}

/* Opacity action methods */
- (void)setOpacity50:(id)sender {
    [self setOpacityValue:0.5f];
}

- (void)setOpacity70:(id)sender {
    [self setOpacityValue:0.7f];
}

- (void)setOpacity85:(id)sender {
    [self setOpacityValue:0.85f];
}

- (void)setOpacity100:(id)sender {
    [self setOpacityValue:1.0f];
}

- (void)setOpacityValue:(float)opacity {
    _config.opacity = opacity;
    save_config(&_config, NULL);
    [self updateOverlayImage];
    _statusItem.menu = [self buildMenu];
    NSLog(@"Changed opacity to %.0f%%", opacity * 100);
}

/* Toggle Show Keymap with optional auto-hide */
- (void)toggleShowKeymap:(id)sender {
    if (_visible) {
        [self hideOverlay];
    } else {
        [self showOverlay];
        /* If auto-hide is enabled, schedule hide timer */
        if (_config.auto_hide > 0.0f && _carbonHideTimer == nil) {
            _carbonHideTimer = [NSTimer scheduledTimerWithTimeInterval:_config.auto_hide repeats:NO block:^(NSTimer * _Nonnull t) {
                [self hideOverlay];
                _carbonHideTimer = nil;
            }];
        }
    }
    _statusItem.menu = [self buildMenu]; // Update checkmark
}

/* Preview Keymap - temporary show for testing */
- (void)previewKeymap:(id)sender {
    [self showOverlay];
    /* Always auto-hide preview after 3 seconds regardless of settings */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (_visible) {
            [self hideOverlay];
        }
    });
}

/* Preferences UI - minimal and local (opacity + hotkey text). Hotkey changes require restart to activate in this MVP. */

- (void)openPreferences:(id)sender {
    if (_prefsWindow) {
        NSLog(@"openPreferences: bringing existing prefs window %p to front", _prefsWindow);
        [_prefsWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        return;
    }

    const CGFloat winW = 480.0f;
    const CGFloat winH = 520.0f;
    NSRect rect = NSMakeRect(0, 0, winW, winH);
    _prefsWindow = [[LoggingWindow alloc] initWithContentRect:rect
                                                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    NSLog(@"openPreferences: created prefs window %p", _prefsWindow);
    [_prefsWindow setTitle:@"Preferences"];
    [_prefsWindow setDelegate:self];

    NSView *content = [_prefsWindow contentView];
    content.wantsLayer = YES;

    /* Helper functions for consistent UI elements */
    NSTextField *(^makeLabel)(NSString *) = ^NSTextField*(NSString *txt) {
        NSTextField *l = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [l setStringValue:txt];
        [l setBezeled:NO];
        [l setDrawsBackground:NO];
        [l setEditable:NO];
        [l setSelectable:NO];
        l.translatesAutoresizingMaskIntoConstraints = NO;
        return l;
    };
    
    NSBox *(^makeGroupBox)(NSString *) = ^NSBox*(NSString *title) {
        NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
        box.boxType = NSBoxPrimary;
        box.title = title;
        box.titlePosition = NSAtTop;
        box.translatesAutoresizingMaskIntoConstraints = NO;
        return box;
    };

    /* === OVERLAY SECTION === */
    NSBox *overlayBox = makeGroupBox(@"Overlay");
    [content addSubview:overlayBox];
    NSView *overlayContent = [[NSView alloc] init];
    [overlayBox setContentView:overlayContent];
    
    /* Scale slider with percentage display */
    NSTextField *scaleLabel = makeLabel(@"Scale:");
    _prefScaleSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    _prefScaleSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefScaleSlider setMinValue:0.5];
    [_prefScaleSlider setMaxValue:1.5];
    [_prefScaleSlider setDoubleValue:_config.scale];
    [_prefScaleSlider setTarget:self];
    [_prefScaleSlider setAction:@selector(prefScaleChanged:)];
    
    _prefScaleLabel = makeLabel([NSString stringWithFormat:@"%.0f%%", _config.scale * 100]);
    _prefScaleLabel.textColor = [NSColor secondaryLabelColor];
    
    [overlayContent addSubview:scaleLabel];
    [overlayContent addSubview:_prefScaleSlider];
    [overlayContent addSubview:_prefScaleLabel];
    
    /* Opacity slider with percentage display */
    NSTextField *opacityLabel = makeLabel(@"Opacity:");
    _prefOpacitySlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    _prefOpacitySlider.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefOpacitySlider setMinValue:0.3];
    [_prefOpacitySlider setMaxValue:1.0];
    [_prefOpacitySlider setDoubleValue:_config.opacity];
    [_prefOpacitySlider setTarget:self];
    [_prefOpacitySlider setAction:@selector(prefOpacityChanged:)];
    
    _prefOpacityLabel = makeLabel([NSString stringWithFormat:@"%.0f%%", _config.opacity * 100]);
    _prefOpacityLabel.textColor = [NSColor secondaryLabelColor];
    
    [overlayContent addSubview:opacityLabel];
    [overlayContent addSubview:_prefOpacitySlider];
    [overlayContent addSubview:_prefOpacityLabel];

    NSTextField *autoHideLabel = makeLabel(@"Auto-hide:");
    _prefAutoHidePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _prefAutoHidePopup.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefAutoHidePopup addItemsWithTitles:@[@"Off (Persistent)", @"0.8s", @"2s"]];
    if (_config.auto_hide <= 0.0f) {
        [_prefAutoHidePopup selectItemAtIndex:0];
    } else if (fabsf(_config.auto_hide - 0.8f) < 0.001f) {
        [_prefAutoHidePopup selectItemAtIndex:1];
    } else if (fabsf(_config.auto_hide - 2.0f) < 0.001f) {
        [_prefAutoHidePopup selectItemAtIndex:2];
    } else {
        [_prefAutoHidePopup selectItemAtIndex:1];
    }
    [content addSubview:autoHideLabel];
    [content addSubview:_prefAutoHidePopup];

    NSTextField *positionLabel = makeLabel(@"Position:");
    _prefPositionPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _prefPositionPopup.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefPositionPopup addItemsWithTitles:@[@"Center", @"Top-Center", @"Bottom-Center", @"Custom"]];
    NSInteger posIndex = _config.position_mode;
    if (posIndex < 0 || posIndex > 3) posIndex = 2;
    [_prefPositionPopup selectItemAtIndex:posIndex];
    [content addSubview:positionLabel];
    [content addSubview:_prefPositionPopup];

    _prefClickThroughCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
    _prefClickThroughCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefClickThroughCheckbox setButtonType:NSButtonTypeSwitch];
    [_prefClickThroughCheckbox setTitle:@"Click-through (ignore mouse)"];
    [_prefClickThroughCheckbox setState:_config.click_through ? NSControlStateValueOn : NSControlStateValueOff];
    [content addSubview:_prefClickThroughCheckbox];

    _prefAlwaysOnTopCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
    _prefAlwaysOnTopCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefAlwaysOnTopCheckbox setButtonType:NSButtonTypeSwitch];
    [_prefAlwaysOnTopCheckbox setTitle:@"Always on top"];
    [_prefAlwaysOnTopCheckbox setState:_config.always_on_top ? NSControlStateValueOn : NSControlStateValueOff];
    [content addSubview:_prefAlwaysOnTopCheckbox];

    /* Separator */
    NSBox *sep = [[NSBox alloc] initWithFrame:NSZeroRect];
    sep.boxType = NSBoxSeparator;
    sep.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:sep];

    NSTextField *hotkeyLabel = makeLabel(@"Hotkey:");
    _prefHotkeyField = [[HotkeyCaptureField alloc] initWithFrame:NSZeroRect];
    _prefHotkeyField.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefHotkeyField setTarget:self];
    [_prefHotkeyField setAction:@selector(prefHotkeyCaptured:)];
    NSString *hk = [NSString stringWithUTF8String:_config.hotkey];
    if (!hk) hk = @"";
    [_prefHotkeyField setStringValue:hk];
    [_prefHotkeyField setPlaceholderString:@"Click and press a hotkey"];
    [content addSubview:hotkeyLabel];
    [content addSubview:_prefHotkeyField];

    _prefUseCustomSizeCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
    _prefUseCustomSizeCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefUseCustomSizeCheckbox setButtonType:NSButtonTypeSwitch];
    [_prefUseCustomSizeCheckbox setTitle:@"Use custom size"];
    [_prefUseCustomSizeCheckbox setTarget:self];
    [_prefUseCustomSizeCheckbox setAction:@selector(prefUseCustomSizeToggled:)];
    [_prefUseCustomSizeCheckbox setState:_config.use_custom_size ? NSControlStateValueOn : NSControlStateValueOff];
    [content addSubview:_prefUseCustomSizeCheckbox];

    NSTextField *widthLabel = makeLabel(@"W:");
    widthLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:widthLabel];

    _prefWidthField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _prefWidthField.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefWidthField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_width_px]];
    [_prefWidthField setEnabled:_config.use_custom_size ? YES : NO];
    [content addSubview:_prefWidthField];

    NSTextField *heightLabel = makeLabel(@"H:");
    heightLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:heightLabel];

    _prefHeightField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _prefHeightField.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefHeightField setStringValue:[NSString stringWithFormat:@"%d", _config.custom_height_px]];
    [_prefHeightField setEnabled:_config.use_custom_size ? YES : NO];
    [content addSubview:_prefHeightField];

    _prefErrorLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _prefErrorLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_prefErrorLabel setBezeled:NO];
    [_prefErrorLabel setDrawsBackground:NO];
    [_prefErrorLabel setEditable:NO];
    [_prefErrorLabel setSelectable:NO];
    [_prefErrorLabel setTextColor:[NSColor systemRedColor]];
    [_prefErrorLabel setStringValue:@""];
    [content addSubview:_prefErrorLabel];

    /* Buttons */
    NSButton *restoreBtn = [[NSButton alloc] initWithFrame:NSZeroRect];
    restoreBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [restoreBtn setTitle:@"Restore Defaults"];
    [restoreBtn setBezelStyle:NSBezelStyleRounded];
    [restoreBtn setTarget:self];
    [restoreBtn setAction:@selector(prefRestoreDefaults:)];
    _prefRestoreDefaultsBtn = restoreBtn;
    [content addSubview:restoreBtn];

    NSButton *closeBtn = [[NSButton alloc] initWithFrame:NSZeroRect];
    closeBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [closeBtn setTitle:@"Close"];
    [closeBtn setBezelStyle:NSBezelStyleRounded];
    [closeBtn setTarget:self];
    [closeBtn setAction:@selector(prefClose:)];
    [content addSubview:closeBtn];

    NSButton *clearBtn = [[NSButton alloc] initWithFrame:NSZeroRect];
    clearBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [clearBtn setTitle:@"Clear"];
    [clearBtn setBezelStyle:NSBezelStyleRounded];
    [clearBtn setTarget:self];
    [clearBtn setAction:@selector(prefClearHotkey:)];
    [content addSubview:clearBtn];

    NSButton *applyBtn = [[NSButton alloc] initWithFrame:NSZeroRect];
    applyBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [applyBtn setTitle:@"Apply"];
    [applyBtn setBezelStyle:NSBezelStyleRounded];
    [applyBtn setTarget:self];
    [applyBtn setAction:@selector(prefApply:)];
    [content addSubview:applyBtn];

    /* Disable restore when current config already equals defaults */
    {
        Config defaults = get_default_config();
        BOOL hkEqual = YES;
        NSString *curHot = [NSString stringWithUTF8String:_config.hotkey ?: ""];
        NSString *defHot = [NSString stringWithUTF8String:defaults.hotkey ?: ""];
        hkEqual = (curHot && defHot && [curHot isEqualToString:defHot]);

        BOOL is_default = (_config.use_custom_size == defaults.use_custom_size &&
                           _config.custom_width_px == defaults.custom_width_px &&
                           _config.custom_height_px == defaults.custom_height_px &&
                           fabsf(_config.opacity - defaults.opacity) < 0.001f &&
                           _config.invert == defaults.invert &&
                           fabsf(_config.scale - defaults.scale) < 0.001f &&
                           hkEqual &&
                           _config.position_mode == defaults.position_mode &&
                           _config.start_at_login == defaults.start_at_login &&
                           _config.click_through == defaults.click_through &&
                           _config.always_on_top == defaults.always_on_top &&
                           fabsf(_config.auto_hide - defaults.auto_hide) < 0.001f);
        [_prefRestoreDefaultsBtn setEnabled: !is_default];
    }

    /* Layout constraints */
    NSDictionary *views = @{
        @"opacityLabel": opacityLabel,
        @"opacitySlider": _prefOpacitySlider,
        @"autoHideLabel": autoHideLabel,
        @"autoHidePopup": _prefAutoHidePopup,
        @"positionLabel": positionLabel,
        @"positionPopup": _prefPositionPopup,
        @"clickThrough": _prefClickThroughCheckbox,
        @"alwaysOnTop": _prefAlwaysOnTopCheckbox,
        @"sep": sep,
        @"hotkeyLabel": hotkeyLabel,
        @"hotkeyField": _prefHotkeyField,
        @"useCustom": _prefUseCustomSizeCheckbox,
        @"widthLabel": widthLabel,
        @"widthField": _prefWidthField,
        @"heightLabel": heightLabel,
        @"heightField": _prefHeightField,
        @"error": _prefErrorLabel,
        @"restore": restoreBtn,
        @"close": closeBtn,
        @"clear": clearBtn,
        @"apply": applyBtn
    };

    CGFloat margin = 14.0f;
    NSMutableArray *constraints = [NSMutableArray array];

    /* Horizontal: label column fixed width, controls stretch */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:opacityLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:opacityLabel attribute:NSLayoutAttributeWidth relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1.0 constant:120],
        [NSLayoutConstraint constraintWithItem:_prefOpacitySlider attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:opacityLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:8],
        [NSLayoutConstraint constraintWithItem:_prefOpacitySlider attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:autoHideLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:autoHideLabel attribute:NSLayoutAttributeWidth relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1.0 constant:120],
        [NSLayoutConstraint constraintWithItem:_prefAutoHidePopup attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:autoHideLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:8],
        [NSLayoutConstraint constraintWithItem:_prefAutoHidePopup attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationLessThanOrEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:positionLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:positionLabel attribute:NSLayoutAttributeWidth relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1.0 constant:120],
        [NSLayoutConstraint constraintWithItem:_prefPositionPopup attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:positionLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:8],
        [NSLayoutConstraint constraintWithItem:_prefPositionPopup attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationLessThanOrEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    /* Checkboxes side-by-side */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:_prefClickThroughCheckbox attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:_prefAlwaysOnTopCheckbox attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:_prefClickThroughCheckbox attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:12],
        [NSLayoutConstraint constraintWithItem:_prefAlwaysOnTopCheckbox attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationLessThanOrEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    /* Separator full width */
    [constraints addObject:[NSLayoutConstraint constraintWithItem:sep attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin]];
    [constraints addObject:[NSLayoutConstraint constraintWithItem:sep attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]];

    /* Hotkey row */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:hotkeyLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:hotkeyLabel attribute:NSLayoutAttributeWidth relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1.0 constant:120],
        [NSLayoutConstraint constraintWithItem:_prefHotkeyField attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:hotkeyLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:8],
        [NSLayoutConstraint constraintWithItem:_prefHotkeyField attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    /* Custom size controls inline */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:widthLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:12],
        [NSLayoutConstraint constraintWithItem:_prefWidthField attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:widthLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:6],
        [NSLayoutConstraint constraintWithItem:heightLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:_prefWidthField attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:12],
        [NSLayoutConstraint constraintWithItem:_prefHeightField attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:heightLabel attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:6],
        [NSLayoutConstraint constraintWithItem:_prefHeightField attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationLessThanOrEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    /* Error label spans full width */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:_prefErrorLabel attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin],
        [NSLayoutConstraint constraintWithItem:_prefErrorLabel attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin]
    ]];

    /* Buttons row (right aligned) */
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:applyBtn attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTrailing multiplier:1.0 constant:-margin],
        [NSLayoutConstraint constraintWithItem:closeBtn attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:applyBtn attribute:NSLayoutAttributeLeading multiplier:1.0 constant:-12],
        [NSLayoutConstraint constraintWithItem:clearBtn attribute:NSLayoutAttributeTrailing relatedBy:NSLayoutRelationEqual toItem:closeBtn attribute:NSLayoutAttributeLeading multiplier:1.0 constant:-12],
        [NSLayoutConstraint constraintWithItem:restoreBtn attribute:NSLayoutAttributeLeading relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeLeading multiplier:1.0 constant:margin]
    ]];

    /* Vertical stacking */
    CGFloat vgap = 12.0f;
    [constraints addObjectsFromArray:@[
        [NSLayoutConstraint constraintWithItem:opacityLabel attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeTop multiplier:1.0 constant:20],
        [NSLayoutConstraint constraintWithItem:_prefOpacitySlider attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:opacityLabel attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:autoHideLabel attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:opacityLabel attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        [NSLayoutConstraint constraintWithItem:_prefAutoHidePopup attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:autoHideLabel attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:positionLabel attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:autoHideLabel attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        [NSLayoutConstraint constraintWithItem:_prefPositionPopup attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:positionLabel attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:_prefClickThroughCheckbox attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:positionLabel attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        [NSLayoutConstraint constraintWithItem:_prefAlwaysOnTopCheckbox attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:_prefClickThroughCheckbox attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:sep attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:_prefClickThroughCheckbox attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        
        [NSLayoutConstraint constraintWithItem:hotkeyLabel attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:sep attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        [NSLayoutConstraint constraintWithItem:_prefHotkeyField attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:hotkeyLabel attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:_prefHotkeyField attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],
        [NSLayoutConstraint constraintWithItem:widthLabel attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],
        [NSLayoutConstraint constraintWithItem:_prefWidthField attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],
        [NSLayoutConstraint constraintWithItem:heightLabel attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],
        [NSLayoutConstraint constraintWithItem:_prefHeightField attribute:NSLayoutAttributeCenterY relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeCenterY multiplier:1.0 constant:0],

        [NSLayoutConstraint constraintWithItem:_prefErrorLabel attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:_prefUseCustomSizeCheckbox attribute:NSLayoutAttributeBottom multiplier:1.0 constant:vgap],

        [NSLayoutConstraint constraintWithItem:applyBtn attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeBottom multiplier:1.0 constant:-12],
        [NSLayoutConstraint constraintWithItem:restoreBtn attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeBottom multiplier:1.0 constant:-12],
        [NSLayoutConstraint constraintWithItem:closeBtn attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeBottom multiplier:1.0 constant:-12],
        [NSLayoutConstraint constraintWithItem:clearBtn attribute:NSLayoutAttributeBottom relatedBy:NSLayoutRelationEqual toItem:content attribute:NSLayoutAttributeBottom multiplier:1.0 constant:-12]
    ]];

    [NSLayoutConstraint activateConstraints:constraints];

    /* Center and show */
    [_prefsWindow center];
    [NSApp activateIgnoringOtherApps:YES];
    [_prefsWindow makeKeyAndOrderFront:nil];
}

- (void)prefOpacityChanged:(NSSlider *)sender {
    _config.opacity = (float)[sender doubleValue];
    /* Update percentage label */
    if (_prefOpacityLabel) {
        [_prefOpacityLabel setStringValue:[NSString stringWithFormat:@"%.0f%%", _config.opacity * 100]];
    }
    /* Clear inline error (live feedback) */
    if (_prefErrorLabel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefErrorLabel setStringValue:@""];
        });
    }
    /* Live preview */
    [self updateOverlayImage];
}

- (void)prefScaleChanged:(NSSlider *)sender {
    _config.scale = (float)[sender doubleValue];
    _config.use_custom_size = NO; // Switch to scale mode
    
    /* Update percentage label */
    if (_prefScaleLabel) {
        [_prefScaleLabel setStringValue:[NSString stringWithFormat:@"%.0f%%", _config.scale * 100]];
    }
    /* Clear inline error (live feedback) */
    if (_prefErrorLabel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefErrorLabel setStringValue:@""];
        });
    }
    /* Live preview with new scale */
    [self reloadOverlayIfNeeded];
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
    /* Capture hotkey string from field */
    NSString *hkStr = nil;
    if ([_prefHotkeyField respondsToSelector:@selector(currentHotkey)]) {
        hkStr = (NSString *)[_prefHotkeyField performSelector:@selector(currentHotkey)];
    } else {
        hkStr = [_prefHotkeyField stringValue];
    }
    if (!hkStr) hkStr = @"";

    /* Validate hotkey includes a modifier */
    BOOL hasModifier = ([hkStr localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hkStr localizedCaseInsensitiveContainsString:@"Control"]);
    if ([hkStr length] == 0 || !hasModifier) {
        if (_prefErrorLabel) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [_prefErrorLabel setStringValue:@"Please capture a valid hotkey (include a modifier)."];
            });
        } else {
            NSAlert *a = [[NSAlert alloc] init];
            [a setMessageText:@"Invalid Hotkey"];
            [a setInformativeText:@"Please capture a hotkey including at least one modifier (Command/Option/Shift/Control)."];
            [a runModal];
        }
        return;
    }

    NSLog(@"prefApply: applying hotkey='%@' opacity=%.2f", hkStr, (float)[_prefOpacitySlider doubleValue]);

    /* Write hotkey into config */
    const char *hk = [hkStr UTF8String];
    if (hk) {
        size_t len = strlen(hk);
        if (len >= sizeof(_config.hotkey)) len = sizeof(_config.hotkey) - 1;
        memcpy(_config.hotkey, hk, len);
        _config.hotkey[len] = '\0';
    }

    /* Opacity */
    _config.opacity = (float)[_prefOpacitySlider doubleValue];

    /* Auto-hide popup -> config.auto_hide */
    if (_prefAutoHidePopup) {
        NSInteger idx = [_prefAutoHidePopup indexOfSelectedItem];
        switch (idx) {
            case 0: /* Off / persistent */
                _config.auto_hide = 0.0f;
                break;
            case 1:
                _config.auto_hide = 0.8f;
                break;
            case 2:
                _config.auto_hide = 2.0f;
                break;
            default:
                _config.auto_hide = 0.8f;
                break;
        }
    }

    /* Position mode */
    if (_prefPositionPopup) {
        NSInteger pm = [_prefPositionPopup indexOfSelectedItem];
        if (pm < 0) pm = 2;
        _config.position_mode = (int)pm;
    }

    /* Click-through and Always-on-top */
    if (_prefClickThroughCheckbox) {
        _config.click_through = ([_prefClickThroughCheckbox state] == NSControlStateValueOn) ? 1 : 0;
    }
    if (_prefAlwaysOnTopCheckbox) {
        _config.always_on_top = ([_prefAlwaysOnTopCheckbox state] == NSControlStateValueOn) ? 1 : 0;
    }

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
                    NSAlert *alert = [[NSAlert alloc] init];
                    [alert setMessageText:@"Invalid size"];
                    [alert setInformativeText:@"Please enter positive integer values for width and height."];
                    [alert runModal];
                }
                return;
            }
            _config.custom_width_px = w;
            _config.custom_height_px = h;
        }
    }

    /* Persist config */
    save_config(&_config, NULL);

    /* Apply click-through and always-on-top immediately to existing panel if present */
    if (_panel) {
        dispatch_async(dispatch_get_main_queue(), ^{
            /* Click-through */
            if (_config.click_through) {
                [_panel setIgnoresMouseEvents:YES];
            } else {
                [_panel setIgnoresMouseEvents:NO];
            }
            /* Window level */
            if (_config.always_on_top) {
                [_panel setLevel:NSScreenSaverWindowLevel];
            } else {
                [_panel setLevel:NSNormalWindowLevel];
            }

            /* Alpha (opacity) */
            [_panel setAlphaValue:_config.opacity];

            /* Reposition according to position_mode */
            NSScreen *screen = [NSScreen mainScreen];
            NSRect screenFrame = [screen visibleFrame];
            NSRect panelFrame = [_panel frame];
            switch (_config.position_mode) {
                case 0: /* Center */
                    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
                    panelFrame.origin.y = NSMidY(screenFrame) - NSHeight(panelFrame) / 2.0;
                    break;
                case 1: /* Top-Center */
                    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
                    panelFrame.origin.y = NSMaxY(screenFrame) - NSHeight(panelFrame) - (_config.position_y);
                    break;
                case 2: /* Bottom-Center */
                    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0;
                    panelFrame.origin.y = NSMinY(screenFrame) + (_config.position_y);
                    break;
                case 3: /* Custom: use position_x/position_y as offsets from center/bottom */
                default:
                    panelFrame.origin.x = NSMidX(screenFrame) - NSWidth(panelFrame) / 2.0 + _config.position_x;
                    panelFrame.origin.y = NSMinY(screenFrame) + _config.position_y;
                    break;
            }
            [_panel setFrame:panelFrame display:YES];
        });
    }

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
                           fabsf(_config.scale - defaults.scale) < 0.001f &&
                           hkEqual &&
                           _config.position_mode == defaults.position_mode &&
                           _config.start_at_login == defaults.start_at_login &&
                           _config.click_through == defaults.click_through &&
                           _config.always_on_top == defaults.always_on_top &&
                           fabsf(_config.auto_hide - defaults.auto_hide) < 0.001f);
        dispatch_async(dispatch_get_main_queue(), ^{
            [_prefRestoreDefaultsBtn setEnabled: !is_default];
        });
    }

    [self reloadOverlayIfNeeded];

    /* Re-register Carbon hotkey and update menu/preview */
    [self registerCarbonHotkey];
    _statusItem.menu = [self buildMenu];
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

    /* New UI fields: Auto-hide, Position, Click-through, Always-on-top */
    if (_prefAutoHidePopup) {
        if (_config.auto_hide <= 0.0f) {
            [_prefAutoHidePopup selectItemAtIndex:0];
        } else if (fabsf(_config.auto_hide - 0.8f) < 0.001f) {
            [_prefAutoHidePopup selectItemAtIndex:1];
        } else if (fabsf(_config.auto_hide - 2.0f) < 0.001f) {
            [_prefAutoHidePopup selectItemAtIndex:2];
        } else {
            [_prefAutoHidePopup selectItemAtIndex:1];
        }
    }
    if (_prefPositionPopup) {
        NSInteger posIndex = _config.position_mode;
        if (posIndex < 0 || posIndex > 3) posIndex = 2;
        [_prefPositionPopup selectItemAtIndex:posIndex];
    }
    if (_prefClickThroughCheckbox) {
        [_prefClickThroughCheckbox setState:_config.click_through ? NSControlStateValueOn : NSControlStateValueOff];
    }
    if (_prefAlwaysOnTopCheckbox) {
        [_prefAlwaysOnTopCheckbox setState:_config.always_on_top ? NSControlStateValueOn : NSControlStateValueOff];
    }

    /* Reload overlay and UI */
    [self reloadOverlayIfNeeded];
    _statusItem.menu = [self buildMenu];

    /* Re-register Carbon hotkey (defaults) */
    [self registerCarbonHotkey];

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
    return fabsf(_config.auto_hide - 0.0f) < 0.0001f;
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
        _prefAutoHidePopup = nil;
        _prefPositionPopup = nil;
        _prefClickThroughCheckbox = nil;
        _prefAlwaysOnTopCheckbox = nil;
    }
}

/* Backwards-compatible stubs for legacy header declarations.
   The app now uses Carbon as the single hotkey mechanism; these methods
   forward to the Carbon-based implementation or act as no-ops to keep the
   public API stable and silence build warnings.
*/
- (void)unregisterCurrentHotkey {
    /* Forward to Carbon unregister helper */
    [self unregisterCarbonHotkey];
}

- (void)handleCGEventWithKeyCode:(CGKeyCode)keyCode flags:(CGEventFlags)flags filteredFlags:(CGEventFlags)filteredFlags isKeyDown:(BOOL)isKeyDown {
    /* No-op fallback: CGEventTap / IOHID fallbacks were removed to reduce bulk.
       Keep this stub so code paths that reference the selector remain valid.
     */
    (void)keyCode;
    (void)flags;
    (void)filteredFlags;
    (void)isKeyDown;
}

@end
