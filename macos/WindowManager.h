#import <Foundation/Foundation.h>
#import "../shared/config.h"

@interface WindowManager : NSObject

- (instancetype)initWithConfig:(Config)config;
- (void)createOverlayWindow;
- (void)showOverlay;
- (void)hideOverlay;
- (void)toggleOverlay;
- (void)updateOverlayImage;
- (void)reloadOverlayIfNeeded;
- (BOOL)isVisible;
- (void)updateConfig:(Config)newConfig;
- (void)cleanup;

@end
