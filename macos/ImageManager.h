#import <Foundation/Foundation.h>
#import "../shared/config.h"

@interface ImageManager : NSObject

- (instancetype)initWithConfig:(Config)config;
- (BOOL)loadOverlay;
- (BOOL)reloadOverlayIfNeeded;
- (const unsigned char *)getOverlayData;
- (int)getOverlayWidth;
- (int)getOverlayHeight;
- (void)updateConfig:(Config)newConfig;

@end
