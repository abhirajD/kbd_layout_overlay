#import <Foundation/Foundation.h>
#import "../shared/config.h"

@interface HotkeyManager : NSObject

- (instancetype)initWithConfig:(Config)config callback:(void (^)(void))callback;
- (BOOL)registerHotkey;
- (void)unregisterHotkey;
- (BOOL)isValidHotkeyString:(NSString *)hotkeyString;
- (void)updateConfig:(Config)newConfig;
- (void)triggerHotkeyCallback;

@end
