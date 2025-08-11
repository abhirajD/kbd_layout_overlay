#import "OverlayView.h"
#import <CoreGraphics/CoreGraphics.h>

@interface OverlayView ()
@property (nonatomic) CGImageRef buffer;
@end

@implementation OverlayView

- (void)dealloc {
    if (_buffer) {
        CGImageRelease(_buffer);
    }
}

- (void)cacheSampleBuffer {
    size_t width = 300;
    size_t height = 100;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, width, height, 8, width * 4,
                                            colorSpace, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colorSpace);

    CGContextSetRGBFillColor(ctx, 0, 0, 0, 0.5);
    CGContextFillRect(ctx, CGRectMake(0, 0, width, height));
    CGContextSetRGBStrokeColor(ctx, 1, 0, 0, 1);
    CGContextSetLineWidth(ctx, 4);
    CGContextStrokeRect(ctx, CGRectMake(2, 2, width - 4, height - 4));

    if (_buffer) {
        CGImageRelease(_buffer);
    }
    _buffer = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (self.buffer) {
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        size_t width = CGImageGetWidth(self.buffer);
        size_t height = CGImageGetHeight(self.buffer);
        CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), self.buffer);
    }
}

@end
