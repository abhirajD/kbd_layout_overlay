cmake_minimum_required(VERSION 3.16)
project(kbd_layout_overlay LANGUAGES C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Shared overlay library
add_library(overlay_lib STATIC 
    shared/overlay.c
)
target_include_directories(overlay_lib PUBLIC shared)

# Windows executable
if(WIN32)
    add_executable(kbd_layout_overlay WIN32
        windows/main.c
    )
    target_link_libraries(kbd_layout_overlay PRIVATE overlay_lib user32 gdi32 shell32)
    target_include_directories(kbd_layout_overlay PRIVATE shared)
    
    if(MSVC)
        target_compile_options(kbd_layout_overlay PRIVATE /W3 /O2)
        target_compile_options(overlay_lib PRIVATE /W3 /O2)
    endif()
endif()

# macOS application
if(APPLE)
    add_executable(KbdLayoutOverlay MACOSX_BUNDLE
        macos/KbdLayoutOverlay/main.m
        macos/KbdLayoutOverlay/AppDelegate.m
    )
    target_link_libraries(KbdLayoutOverlay PRIVATE overlay_lib 
        "-framework Cocoa" "-framework Carbon")
    target_include_directories(KbdLayoutOverlay PRIVATE shared)
    target_compile_options(KbdLayoutOverlay PRIVATE "-fobjc-arc")
    set_target_properties(KbdLayoutOverlay PROPERTIES 
        MACOSX_BUNDLE_BUNDLE_NAME "Kbd Layout Overlay")
endif()