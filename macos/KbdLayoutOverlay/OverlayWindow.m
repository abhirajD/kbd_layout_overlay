#import "OverlayWindow.h"

@implementation OverlayWindow

- (BOOL)canBecomeKeyWindow {
    return NO;
}

- (BOOL)canBecomeMainWindow {
    return NO;
}

@end