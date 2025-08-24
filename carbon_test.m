// Test Carbon hotkey approach - clang -fobjc-arc -framework Cocoa -framework Carbon carbon_test.m -o carbon_test
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

static EventHotKeyRef gHotKeyRef = NULL;

static OSStatus HotKeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID hotKeyID;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hotKeyID), NULL, &hotKeyID);
    
    if (hotKeyID.signature == 'TEST' && hotKeyID.id == 1) {
        NSLog(@"Carbon hotkey pressed successfully!");
        
        // Show a simple notification
        NSUserNotification *notification = [[NSUserNotification alloc] init];
        notification.title = @"Carbon Hotkey Test";
        notification.informativeText = @"Cmd+Option+Shift+Slash detected!";
        [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
    }
    
    return noErr;
}

static OSStatus RegisterTestHotkey(void) {
    EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    InstallEventHandler(GetApplicationEventTarget(), HotKeyHandler, 1, &eventType, NULL, NULL);

    EventHotKeyID hotKeyID = { 'TEST', 1 };
    // Cmd+Option+Shift+Slash
    UInt32 keycode = kVK_ANSI_Slash; // 44
    UInt32 mods = cmdKey | optionKey | shiftKey;

    return RegisterEventHotKey(keycode, mods, hotKeyID, GetApplicationEventTarget(), 0, &gHotKeyRef);
}

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        
        OSStatus err = RegisterTestHotkey();
        if (err != noErr) {
            NSLog(@"RegisterEventHotKey failed with OSStatus=%d", (int)err);
        } else {
            NSLog(@"Carbon hotkey registered: Cmd+Option+Shift+Slash");
            NSLog(@"Press the hotkey to test, Ctrl+C to exit");
        }
        
        [NSApp run];
        
        if (gHotKeyRef) UnregisterEventHotKey(gHotKeyRef);
    }
    return 0;
}