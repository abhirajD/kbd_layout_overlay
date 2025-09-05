#import <Foundation/Foundation.h>
#import "../shared/config.h"

@interface MenuController : NSObject

- (instancetype)initWithConfig:(Config)config;
- (void)setupStatusItem;
- (void)updateMenu;
- (void)updateConfig:(Config)newConfig;

@end
