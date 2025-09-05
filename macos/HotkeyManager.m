#import "HotkeyManager.h"
#import <Carbon/Carbon.h>
#import <ApplicationServices/ApplicationServices.h>
#import "hotkey_parse.h"
#import "../shared/config.h"
#import "../shared/log.h"

@interface HotkeyManager () {
    Config _config;
    EventHotKeyRef _carbonHotKey;
    BOOL _carbonHotkeyActive;
    EventHandlerRef _carbonEventHandlerRef;
    BOOL _carbonHandlerInstalled;
    void (^_hotkeyCallback)(void);
}
@end

/* Carbon hotkey event handler - called when global hotkey is pressed */
static OSStatus CarbonHotkeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID hotKeyID;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hotKeyID), NULL, &hotKeyID);

    if (hotKeyID.signature == 'KLOH' && hotKeyID.id == 1) {
        HotkeyManager *manager = (__bridge HotkeyManager *)userData;
        if (manager) {
            [manager triggerHotkeyCallback];
        }
    }

    return noErr;
}

@implementation HotkeyManager

- (instancetype)initWithConfig:(Config)config callback:(void (^)(void))callback {
    self = [super init];
    if (self) {
        _config = config;
        _hotkeyCallback = [callback copy];
        _carbonHotkeyActive = NO;
        _carbonHandlerInstalled = NO;
        _carbonHotKey = NULL;
        _carbonEventHandlerRef = NULL;
    }
    return self;
}

- (void)dealloc {
    [self unregisterHotkey];
}

- (BOOL)registerHotkey {
    /* Ensure registration happens on the main thread */
    if (![NSThread isMainThread]) {
        __block BOOL result = NO;
        dispatch_sync(dispatch_get_main_queue(), ^{
            result = [self registerHotkey];
        });
        return result;
    }

    /* Use Carbon as the primary, reliable hotkey mechanism */
    return [self registerCarbonHotkey];
}

- (BOOL)registerCarbonHotkey {
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
        return NO;
    }

    /* Parse hotkey string to Carbon key code and modifiers */
    UInt32 keyCode = 0;
    UInt32 modifiers = 0;

    /* Enhanced key mapping based on existing parseKeyCode logic */
    if ([hk containsString:@"Slash"]) {
        keyCode = kVK_ANSI_Slash;
    } else if ([hk containsString:@"Space"]) {
        keyCode = kVK_Space;
    } else if ([hk containsString:@"Return"] || [hk containsString:@"Enter"]) {
        keyCode = kVK_Return;
    } else if ([hk containsString:@"Escape"]) {
        keyCode = kVK_Escape;
    } else if ([hk containsString:@"F1"]) {
        keyCode = kVK_F1;
    } else if ([hk containsString:@"F2"]) {
        keyCode = kVK_F2;
    } else if ([hk containsString:@"F3"]) {
        keyCode = kVK_F3;
    } else if ([hk containsString:@"F4"]) {
        keyCode = kVK_F4;
    } else {
        logger_log("Unsupported key in hotkey: %s", [hk UTF8String]);
        return NO;
    }

    /* Parse modifiers - use Carbon constants directly */
    if ([hk localizedCaseInsensitiveContainsString:@"Command"]) modifiers |= cmdKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Option"]) modifiers |= optionKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Shift"]) modifiers |= shiftKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Control"]) modifiers |= controlKey;

    if (modifiers == 0) {
        logger_log("No modifiers in hotkey: %s", [hk UTF8String]);
        return NO;
    }

    /* Install Carbon event handler */
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
        return YES;
    } else {
        logger_log("Carbon hotkey registration failed: %s (OSStatus=%d)", [hk UTF8String], (int)status);
        return NO;
    }
}

- (void)unregisterHotkey {
    [self unregisterCarbonHotkey];
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

- (void)updateConfig:(Config)newConfig {
    _config = newConfig;
}

- (void)triggerHotkeyCallback {
    if (_hotkeyCallback) {
        dispatch_async(dispatch_get_main_queue(), ^{
            _hotkeyCallback();
        });
    }
}

@end
