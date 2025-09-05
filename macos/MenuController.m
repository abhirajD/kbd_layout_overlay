#import "MenuController.h"
#import <Cocoa/Cocoa.h>
#import "../shared/config.h"

@interface MenuController () {
    NSStatusItem *_statusItem;
    Config _config;
}
@end

@implementation MenuController

- (instancetype)initWithConfig:(Config)config {
    self = [super init];
    if (self) {
        _config = config;
    }
    return self;
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
    [showKeymapItem setState:NO]; // Will be updated by caller
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

- (void)updateMenu {
    _statusItem.menu = [self buildMenu];
}

- (void)updateConfig:(Config)newConfig {
    _config = newConfig;
    [self updateMenu];
}

@end
