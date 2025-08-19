#!/usr/bin/env python3
"""Create a simple default keyboard layout image as a C array."""

from PIL import Image, ImageDraw, ImageFont
import io

# Create a simple keyboard layout image
width, height = 800, 300
img = Image.new('RGBA', (width, height), (0, 0, 0, 200))  # Semi-transparent black
draw = ImageDraw.Draw(img)

# Draw a simple keyboard representation
draw.rectangle([50, 50, width-50, height-50], outline=(255, 255, 255, 255), width=2)
draw.text((width//2-100, height//2-20), "KEYBOARD OVERLAY", fill=(255, 255, 255, 255))
draw.text((width//2-80, height//2+10), "Press hotkey to hide", fill=(200, 200, 200, 255))

# Save as PNG to memory
png_buffer = io.BytesIO()
img.save(png_buffer, format='PNG')
png_data = png_buffer.getvalue()

# Convert to C array
print(f"/* Generated default keyboard overlay - {width}x{height} PNG */")
print("static const unsigned char default_keymap[] = {")

for i, byte in enumerate(png_data):
    if i % 12 == 0:
        print("    ", end="")
    print(f"0x{byte:02X}", end="")
    if i < len(png_data) - 1:
        print(", ", end="")
    if (i + 1) % 12 == 0:
        print()

if len(png_data) % 12 != 0:
    print()
print("};")
print(f"\n/* Size: {len(png_data)} bytes */")