#import <Cocoa/Cocoa.h>

@interface OverlayView : NSView
- (void)setImageData:(const unsigned char *)data width:(int)width height:(int)height;
@end
