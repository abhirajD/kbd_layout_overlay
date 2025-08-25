# Keyboard Layout Overlay Development Guide

**ALWAYS follow these instructions first and only fallback to additional search and context gathering if the information here is incomplete or found to be in error.**

## Overview

Keyboard Layout Overlay is a lightweight C application that displays a keyboard overlay when you press a global hotkey. It's cross-platform (macOS and Windows) with a clean architecture using CMake for builds.

**Key Architecture:**
- `shared/` - Core overlay loading, config management, logging (732 lines total)
  - `overlay.c` - Image loading, effects, memory management (415 lines)
  - `config.c` - Configuration persistence and parsing (232 lines)
  - `log.c` - Cross-platform logging (85 lines)
- `windows/` - Win32 implementation (1130 lines in main.c)
- `macos/` - Cocoa implementation (2035 lines total)  
  - `AppDelegate.m` - Main application logic (1964 lines)
  - `hotkey_parse.c` - Hotkey string parsing (58 lines)
  - `OverlayWindow.m` - Window management (12 lines)
- `tests/` - Unit tests for overlay functionality (287 lines total)
- **Total:** ~4,184 lines of application code

## Critical Build & Timing Information

### Build Process (NEVER CANCEL - Set Long Timeouts)
```bash
# 1. Configure with CMake
mkdir build && cd build
cmake ..                # Takes ~1 second

# 2. Build (CRITICAL: NEVER CANCEL - Set timeout to 10+ minutes)
make                    # Takes ~2 minutes on average. NEVER CANCEL.
# or 
cmake --build .         # Windows alternative

# 3. Run all tests (takes <1 second each)
./test_mvp
./test_overlay_copy  
./test_overlay_scale
```

**CRITICAL TIMING EXPECTATIONS:**
- **CMake configure**: ~1 second
- **Build process**: ~2 minutes (NEVER CANCEL - set timeout to 10+ minutes)
- **All tests**: <1 second each
- **Config test compilation**: ~1 second

### Platform-Specific Build Commands

**macOS:**
```bash
cmake -S . -B build -DENABLE_CODESIGN=OFF    # Disable code signing for development
cmake --build build --config Release
```

**Windows:**
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**Linux (for testing shared code only):**
```bash
mkdir build && cd build
cmake ..
make
```

## Working Effectively

### Bootstrap Development Environment
```bash
# Clone and enter repository
cd /path/to/kbd_layout_overlay

# ALWAYS build and test before making changes
mkdir build && cd build
cmake ..
make                    # NEVER CANCEL - Takes ~2 minutes
./test_mvp && ./test_overlay_copy && ./test_overlay_scale
```

### Custom Keymap Development
```bash
# 1. OPTIONAL: Add custom keyboard layout image
cp your_keyboard_layout.png assets/keymap.png

# 2. Rebuild to embed the image
cd build
make                    # NEVER CANCEL - Embedding process adds ~10 seconds

# 3. Verify embedding worked
grep -q "Found keymap.png, will embed in binary" CMakeCache.txt
```

### Configuration Testing
```bash
# Compile and run config tests manually
cd build
gcc -I../shared ../tests/test_config.c ../shared/config.c -lm -o test_config
./test_config           # Should output "All config tests passed"
```

### Running the Applications

**macOS:**
```bash
# From build directory
./KbdLayoutOverlay.app/Contents/MacOS/KbdLayoutOverlay

# Check logs for debugging
tail -f /tmp/kbd_layout_overlay.log
```

**Windows:**
```bash
# From build directory  
./kbd_layout_overlay.exe        # or Release/kbd_layout_overlay.exe
```

**Note:** The applications cannot be fully exercised on Linux as they require platform-specific UI frameworks (Cocoa for macOS, Win32 for Windows). However, all shared library code and tests can be built and validated on Linux for development purposes.

## Validation Scenarios

**ALWAYS test these scenarios after making changes:**

1. **Build System Validation:**
   ```bash
   # Clean build test
   rm -rf build && mkdir build && cd build
   cmake .. && make                # NEVER CANCEL - Set 10+ minute timeout
   ./test_mvp && ./test_overlay_copy && ./test_overlay_scale
   ```

2. **Embedded Keymap Test:**
   ```bash
   # Test with embedded keymap
   cp assets/keymap.png assets/keymap_test.png
   rm assets/keymap.png
   make                           # Should build with "No keymap.png found" message
   cp assets/keymap_test.png assets/keymap.png  
   make                           # Should build with "Found keymap.png, will embed" message
   ```

3. **Config System Test:**
   ```bash
   cd build
   gcc -I../shared ../tests/test_config.c ../shared/config.c -lm -o test_config
   ./test_config                  # Must pass all tests
   ```

4. **Cross-Platform Code Test:**
   ```bash
   # Ensure shared code compiles on Linux
   cd build
   gcc -I../shared -c ../shared/overlay.c -o overlay.o
   gcc -I../shared -c ../shared/config.c -o config.o
   gcc -I../shared -c ../shared/log.c -o log.o
   ```

### Manual Functionality Testing

**IMPORTANT: These tests require actual target platforms (macOS/Windows) with GUI environments:**

1. **Basic Overlay Function:**
   - Install and run the application
   - Verify tray icon appears (macOS: menu bar, Windows: system tray)
   - Test global hotkey (Cmd+Option+Shift+/ on macOS, Ctrl+Alt+Shift+/ on Windows)
   - Confirm overlay shows and hides correctly
   - **Note:** Cannot be tested on Linux - requires platform-specific GUI frameworks

2. **Configuration Testing:**
   - Test menu options (opacity, invert, sizing)
   - Verify settings persistence across app restarts
   - Test hotkey customization through preferences
   - **Note:** Configuration file locations:
     - macOS: `~/Library/Application Support/kbd_layout_overlay/config.json`
     - Windows: `%APPDATA%\kbd_layout_overlay\config.json`

3. **Error Handling:**
   - Test with missing keymap file (should use embedded fallback)
   - Test with corrupted image file (should show helpful error)
   - Verify error messages are helpful and actionable
   - Check log files for debugging information:
     - macOS: `/tmp/kbd_layout_overlay.log`

**Functional validation can only be performed on target platforms with GUI environments.**

## Common Development Tasks

### Adding New Tests
```bash
# 1. Create test file in tests/ directory
# 2. Add executable to CMakeLists.txt:
add_executable(test_new_feature tests/test_new_feature.c)
target_link_libraries(test_new_feature PRIVATE overlay_lib)
target_include_directories(test_new_feature PRIVATE shared)

# 3. Rebuild and test
cd build && make && ./test_new_feature
```

### Modifying Overlay Effects
```bash
# Key files for overlay effects:
# - shared/overlay.c: apply_effects(), apply_effects_copy()
# - shared/overlay.h: OverlayError enum, function declarations

# Always test changes with:
cd build && make && ./test_mvp && ./test_overlay_copy
```

### Configuration Changes
```bash
# Key files for configuration:
# - shared/config.h: Config struct definition
# - shared/config.c: load_config(), save_config()
# - tests/test_config.c: Configuration tests

# Always test config changes with:
cd build
gcc -I../shared ../tests/test_config.c ../shared/config.c -lm -o test_config
./test_config
```

### Platform-Specific Changes

**macOS (Cocoa/Objective-C):**
```bash
# Key files:
# - macos/KbdLayoutOverlay/AppDelegate.m: Main application logic
# - macos/KbdLayoutOverlay/OverlayWindow.m: Window management
# - macos/KbdLayoutOverlay/hotkey_parse.c: Hotkey string parsing

# Build with:
cmake --build build --config Release
```

**Windows (Win32/C):**
```bash
# Key files:
# - windows/main.c: Main application logic (~40KB file)

# Build with:
cmake --build build --config Release --verbose
```

## CI/CD Integration

### GitHub Actions Workflow
The repository uses `.github/workflows/build.yml` which:
- Builds on Windows and macOS
- Runs `test_mvp` and `test_overlay_copy` 
- Uploads artifacts for both platforms

**Before committing, always run locally:**
```bash
# Test what CI will do:
cd build
make                    # NEVER CANCEL - Build step
./test_mvp && ./test_overlay_copy    # Test step
```

### Debugging CI Failures
```bash
# Check if your changes compile cleanly:
cd build
make 2>&1 | tee build.log          # NEVER CANCEL - Check for warnings
grep -i warning build.log          # Should be minimal warnings

# Verify tests pass:
./test_mvp && ./test_overlay_copy && ./test_overlay_scale
echo "Exit code: $?"               # Should be 0
```

## Important File Locations

### Core Application Logic
- `shared/overlay.c` - Image loading, effects, memory management (415 lines)
- `shared/config.c` - Configuration persistence and parsing (232 lines)
- `shared/log.c` - Cross-platform logging (85 lines)
- `shared/overlay.h` - API definitions and error codes

### Platform Implementations  
- `macos/KbdLayoutOverlay/AppDelegate.m` - macOS main logic (1964 lines, 87KB)
- `windows/main.c` - Windows main logic (1130 lines, 41KB)

### Build System
- `CMakeLists.txt` - Main build configuration
- `embed_keymap.cmake` - Script to embed PNG files in C headers
- `.github/workflows/build.yml` - CI/CD pipeline

### Tests and Validation
- `tests/test_overlay.c` - Basic overlay effects testing (42 lines)
- `tests/test_overlay_copy.c` - Non-destructive effects testing (83 lines)
- `tests/test_overlay_scale.c` - Image scaling testing (26 lines)
- `tests/test_config.c` - Configuration system testing (80 lines)

### Documentation
- `README.md` - User documentation and basic build instructions
- `MVP_CHANGELOG.md` - Recent changes and validation checklist
- `implementation_plan.md` - Development progress tracking

## Troubleshooting Common Issues

### Build Issues
```bash
# Clean build if strange errors occur:
rm -rf build && mkdir build && cd build
cmake .. && make                # NEVER CANCEL

# Check for missing dependencies on macOS:
xcode-select --install          # If Xcode tools missing

# Windows: Ensure Visual Studio 2022 is available
cmake --version                 # Should be 3.16+
```

### Test Failures
```bash
# Run individual tests for debugging:
cd build
./test_mvp              # Basic effects test
./test_overlay_copy     # Copy-based effects test  
./test_overlay_scale    # Scaling test

# Manual config test if CMake doesn't include it:
gcc -I../shared ../tests/test_config.c ../shared/config.c -lm -o test_config
./test_config
```

### Memory Issues
```bash
# Run with address sanitizer on supported platforms:
export CFLAGS="-fsanitize=address -g"
cd build && make clean && make          # NEVER CANCEL
./test_mvp                              # Check for memory errors
```

## Performance Considerations

- **Build time**: ~2 minutes (with keymap embedding)
- **Image embedding**: Adds ~10 seconds to build time
- **Test execution**: All tests complete in <5 seconds total
- **Binary size**: ~400-500KB per platform

The application is designed for minimal resource usage with effect caching to prevent redundant image processing.

## Key Dependencies

**Required:**
- CMake 3.16+
- C11 compiler (GCC, Clang, MSVC)
- Platform SDKs (Windows SDK, macOS SDK)

**Optional:**
- Python 3 with Pillow (for create_default.py script)
  ```bash
  pip install Pillow    # Required if using create_default.py
  ```

**Embedded Libraries:**
- STB Image (stb_image.h, stb_image_resize.h) - Already included in shared/

## Security Notes

- macOS requires Input Monitoring permissions for global hotkeys
- Windows executable requires Administrator privileges for global hotkey registration  
- Code signing is optional for development (ENABLE_CODESIGN=OFF)
- All file operations use safe buffer handling

## Final Validation Checklist

Before considering any changes complete, ALWAYS verify:

- [ ] `cd build && make` completes without errors (NEVER CANCEL)
- [ ] All tests pass: `./test_mvp && ./test_overlay_copy && ./test_overlay_scale`  
- [ ] Config test passes: `./test_config` (if modified config system)
- [ ] No new compiler warnings in build output
- [ ] Changes work on both macOS and Windows (if platform-specific)
- [ ] Documentation updated if user-facing changes made

**Remember: NEVER CANCEL long-running build commands. Set timeouts to 10+ minutes and wait for completion.**