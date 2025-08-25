cmake_minimum_required(VERSION 3.16)
project(kbd_layout_overlay LANGUAGES C)

option(ENABLE_CODESIGN "Perform codesign post-build for macOS bundle" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Generate embedded keymap header if keymap.png exists
set(KEYMAP_PNG "${CMAKE_SOURCE_DIR}/assets/keymap.png")
set(KEYMAP_HEADER "${CMAKE_BINARY_DIR}/keymap_embedded.h")

if(EXISTS "${KEYMAP_PNG}")
    message(STATUS "Found keymap.png, will embed in binary")
    add_custom_command(
        OUTPUT "${KEYMAP_HEADER}"
        COMMAND ${CMAKE_COMMAND} 
            -DPNG_FILE="${KEYMAP_PNG}"
            -DHEADER_FILE="${KEYMAP_HEADER}"
            -P "${CMAKE_SOURCE_DIR}/embed_keymap.cmake"
        DEPENDS "${KEYMAP_PNG}" "${CMAKE_SOURCE_DIR}/embed_keymap.cmake"
        COMMENT "Embedding keymap.png into C header"
    )
    set(KEYMAP_SOURCES "${KEYMAP_HEADER}")
    set(EMBED_KEYMAP ON)
else()
    message(STATUS "No keymap.png found, using runtime file loading only")
    set(KEYMAP_SOURCES "")
    set(EMBED_KEYMAP OFF)
endif()

# Shared overlay library
add_library(overlay_lib STATIC 
    shared/overlay.c
    shared/config.c
    shared/log.c
    ${KEYMAP_SOURCES}
)
target_include_directories(overlay_lib PUBLIC shared)
target_include_directories(overlay_lib PRIVATE "${CMAKE_BINARY_DIR}")

if(EMBED_KEYMAP)
    target_compile_definitions(overlay_lib PRIVATE EMBED_KEYMAP=1)
endif()

# Windows executable
if(WIN32)
    add_executable(kbd_layout_overlay WIN32
        windows/main.c
    )
    target_link_libraries(kbd_layout_overlay PRIVATE overlay_lib user32 gdi32 shell32 comctl32)
    target_include_directories(kbd_layout_overlay PRIVATE shared)
    
    if(MSVC)
        target_compile_options(kbd_layout_overlay PRIVATE /W3 /O2)
        target_compile_options(overlay_lib PRIVATE /W3 /O2)
        # Suppress MSVC warnings about "unsafe" CRT functions used intentionally
        target_compile_definitions(kbd_layout_overlay PRIVATE _CRT_SECURE_NO_WARNINGS=1)
        target_compile_definitions(overlay_lib PRIVATE _CRT_SECURE_NO_WARNINGS=1)
    endif()
endif()

# macOS application
if(APPLE)
    add_executable(KbdLayoutOverlay MACOSX_BUNDLE
        macos/KbdLayoutOverlay/main.m
        macos/KbdLayoutOverlay/AppDelegate.m
        macos/KbdLayoutOverlay/OverlayWindow.m
        macos/KbdLayoutOverlay/hotkey_parse.c
    )
    target_link_libraries(KbdLayoutOverlay PRIVATE overlay_lib 
        "-framework Cocoa" "-framework Carbon" "-framework ApplicationServices" "-framework IOKit")
    target_include_directories(KbdLayoutOverlay PRIVATE shared macos/KbdLayoutOverlay)
    target_compile_options(KbdLayoutOverlay PRIVATE "-fobjc-arc")
    set_target_properties(KbdLayoutOverlay PROPERTIES 
        MACOSX_BUNDLE_BUNDLE_NAME "Kbd Layout Overlay"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/macos/Info.plist.in"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/macos/Entitlements.plist"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES)
    
if(APPLE AND ENABLE_CODESIGN)
    # Add post-build step to sign with entitlements
    add_custom_command(TARGET KbdLayoutOverlay POST_BUILD
        COMMAND codesign --force --sign - --entitlements "${CMAKE_SOURCE_DIR}/macos/Entitlements.plist" --options runtime "$<TARGET_FILE:KbdLayoutOverlay>"
        COMMENT "Code signing KbdLayoutOverlay with entitlements"
    )
endif()
endif()

# Test executable (cross-platform)
add_executable(test_mvp tests/test_overlay.c)
target_link_libraries(test_mvp PRIVATE overlay_lib)
target_include_directories(test_mvp PRIVATE shared)

add_executable(test_overlay_copy tests/test_overlay_copy.c)
target_link_libraries(test_overlay_copy PRIVATE overlay_lib)
target_include_directories(test_overlay_copy PRIVATE shared)

if(UNIX AND NOT APPLE)
    target_link_libraries(test_mvp PRIVATE pthread)
    target_link_libraries(test_overlay_copy PRIVATE pthread)
endif()
