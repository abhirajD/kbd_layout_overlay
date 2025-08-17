cmake_minimum_required(VERSION 3.16)
project(kbd_layout_overlay LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Shared/core library
set(KLO_SHARED_SRCS
    shared/config.c
    shared/error.c
    shared/hotkey.c
    shared/monitor.c
    shared/overlay.c
    shared/app_context.c
)

add_library(klo_core STATIC ${KLO_SHARED_SRCS})
target_include_directories(klo_core PUBLIC ${CMAKE_SOURCE_DIR}/shared)

# Windows executable target
if(WIN32)
    # Use WIN32 to create a GUI app (no console)
    add_executable(kbd_layout_overlay WIN32
        windows/main.c
        windows/hotkey_win.c
        windows/platform_win.c
        windows/resource.rc
    )
    target_include_directories(kbd_layout_overlay PRIVATE ${CMAKE_SOURCE_DIR}/windows ${CMAKE_SOURCE_DIR}/shared)
    target_link_libraries(kbd_layout_overlay PRIVATE klo_core user32 gdi32 advapi32 shell32)
    # Use MSVC specific flags conservatively
    if(MSVC)
        target_compile_options(kbd_layout_overlay PRIVATE /W3 /O2)
        target_compile_options(klo_core PRIVATE /W3 /O2)
    endif()
endif()

# macOS application target
if(APPLE)
    enable_language(OBJC)
    add_executable(KbdLayoutOverlay MACOSX_BUNDLE
        macos/KbdLayoutOverlay/main.m
        macos/KbdLayoutOverlay/AppDelegate.m
        macos/KbdLayoutOverlay/OverlayView.m
    )
    target_include_directories(KbdLayoutOverlay PRIVATE ${CMAKE_SOURCE_DIR}/macos/KbdLayoutOverlay ${CMAKE_SOURCE_DIR}/shared)
    target_link_libraries(KbdLayoutOverlay PRIVATE klo_core "-framework Cocoa" "-framework ApplicationServices" "-framework Carbon")
    set_target_properties(KbdLayoutOverlay PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "Kbd Layout Overlay")
endif()
