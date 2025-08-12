#import "OverlayView.h"
#import <CoreGraphics/CoreGraphics.h>

@interface OverlayView ()
@property (nonatomic) CGImageRef image;
@end

@implementation OverlayView

- (void)dealloc {
    if (_image) {
        CGImageRelease(_image);
    }
}

- (void)setImageData:(const unsigned char *)data width:(int)width height:(int)height {
    if (_image) {
        CGImageRelease(_image);
        _image = NULL;
    }
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, data,
        (size_t)width * height * 4, NULL);
    _image = CGImageCreate(width, height, 8, 32, width * 4, colorSpace,
        kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast,
        provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (self.image) {
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        CGRect rect = CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height);
        CGContextDrawImage(ctx, rect, self.image);
    }
}

@end
