#import "system_mac.h"
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <ApplicationServices/ApplicationServices.h>
#import "hotkey_parse.h"
#import "../shared/config.h"
#import "../shared/log.h"

struct SystemTray {
    NSStatusItem *statusItem;
};

struct HotkeyHandler {
    EventHotKeyRef carbonHotKey;
    BOOL carbonHotkeyActive;
    EventHandlerRef carbonEventHandlerRef;
    BOOL carbonHandlerInstalled;
    void (*callback)(void);
};

static void hotkey_callback(void) {
    // This will be set by the hotkey handler
}

/* Carbon hotkey event handler - called when global hotkey is pressed */
static OSStatus CarbonHotkeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID hotKeyID;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hotKeyID), NULL, &hotKeyID);

    if (hotKeyID.signature == 'KLOH' && hotKeyID.id == 1) {
        HotkeyHandler *handler = (__bridge HotkeyHandler *)userData;
        if (handler && handler->callback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                handler->callback();
            });
        }
    }

    return noErr;
}

SystemTray* create_system_tray(void) {
    SystemTray *tray = calloc(1, sizeof(SystemTray));
    if (!tray) return NULL;

    tray->statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    tray->statusItem.button.title = @"KLO";

    return tray;
}

void destroy_system_tray(SystemTray* tray) {
    if (!tray) return;

    if (tray->statusItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:tray->statusItem];
    }

    free(tray);
}

void update_tray_menu(SystemTray* tray) {
    if (!tray || !tray->statusItem) return;

    // This would be implemented to update the menu
    // For now, just keep the existing menu
}

HotkeyHandler* register_hotkey(const char* hotkey_str, void(*callback)(void)) {
    if (!hotkey_str || !callback) return NULL;

    HotkeyHandler *handler = calloc(1, sizeof(HotkeyHandler));
    if (!handler) return NULL;

    handler->callback = callback;

    NSString *hk = [NSString stringWithUTF8String:hotkey_str];
    if (!hk) {
        free(handler);
        return NULL;
    }

    /* Basic validation: require at least one modifier to avoid accidental single-key triggers */
    BOOL hasModifier = ([hk localizedCaseInsensitiveContainsString:@"Command"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Option"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Shift"] ||
                        [hk localizedCaseInsensitiveContainsString:@"Control"]);
    if (![hk length] || !hasModifier) {
        logger_log("Invalid or modifier-less hotkey '%s' - skipping Carbon registration", [hk UTF8String]);
        free(handler);
        return NULL;
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
        free(handler);
        return NULL;
    }

    /* Parse modifiers - use Carbon constants directly */
    if ([hk localizedCaseInsensitiveContainsString:@"Command"]) modifiers |= cmdKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Option"]) modifiers |= optionKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Shift"]) modifiers |= shiftKey;
    if ([hk localizedCaseInsensitiveContainsString:@"Control"]) modifiers |= controlKey;

    if (modifiers == 0) {
        logger_log("No modifiers in hotkey: %s", [hk UTF8String]);
        free(handler);
        return NULL;
    }

    /* Install Carbon event handler */
    EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    if (!handler->carbonHandlerInstalled) {
        InstallEventHandler(GetApplicationEventTarget(), CarbonHotkeyHandler, 1, &eventType,
                           (__bridge void *)handler, &handler->carbonEventHandlerRef);
        handler->carbonHandlerInstalled = YES;
        logger_log("Carbon event handler installed");
    }

    /* Register the hotkey with system event target */
    EventHotKeyID hotKeyID = { 'KLOH', 1 };
    OSStatus status = RegisterEventHotKey(keyCode, modifiers, hotKeyID,
                                         GetApplicationEventTarget(), 0, &handler->carbonHotKey);

    if (status == noErr) {
        handler->carbonHotkeyActive = YES;
        logger_log("Carbon hotkey registered successfully: %s (keyCode=%u modifiers=0x%x)",
                  [hk UTF8String], keyCode, modifiers);
    } else {
        logger_log("Carbon hotkey registration failed: %s (OSStatus=%d)", [hk UTF8String], (int)status);
        free(handler);
        return NULL;
    }

    return handler;
}

void unregister_hotkey(HotkeyHandler* handler) {
    if (!handler) return;

    if (handler->carbonHotkeyActive && handler->carbonHotKey) {
        UnregisterEventHotKey(handler->carbonHotKey);
        handler->carbonHotKey = NULL;
        handler->carbonHotkeyActive = NO;
        logger_log("Carbon hotkey unregistered");
    }
    if (handler->carbonHandlerInstalled && handler->carbonEventHandlerRef) {
        RemoveEventHandler(handler->carbonEventHandlerRef);
        handler->carbonEventHandlerRef = NULL;
        handler->carbonHandlerInstalled = NO;
        logger_log("Carbon event handler removed");
    }

    free(handler);
}

void show_notification(const char* title, const char* message) {
    if (!title || !message) return;

    NSString *nsTitle = [NSString stringWithUTF8String:title];
    NSString *nsMessage = [NSString stringWithUTF8String:message];

    NSUserNotification *notification = [[NSUserNotification alloc] init];
    notification.title = nsTitle;
    notification.informativeText = nsMessage;

    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];

    [notification release];
}
