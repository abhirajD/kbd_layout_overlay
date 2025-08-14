use std::num::NonZeroU32;
use std::path::Path;
use std::sync::{Arc, Mutex};

use image::{imageops::FilterType, RgbaImage};

use active_win_pos_rs::get_active_window;
use anyhow::{Context, Result};
use display_info::DisplayInfo;
use log::{debug, error, warn};
use mouse_position::mouse_position::Mouse;
use winit::{
    dpi::{LogicalSize, PhysicalPosition},
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder, EventLoopProxy},
    window::{Window, WindowBuilder, WindowLevel},
};

const DEFAULT_IMAGE: &[u8] = include_bytes!("../../shared/assets/keymap.png");

#[cfg(target_os = "windows")]
use windows_sys::Win32::{
    Foundation::HWND,
    UI::WindowsAndMessaging::{
        GetWindowLongW, SetWindowLongW, GWL_EXSTYLE, WS_EX_LAYERED, WS_EX_TRANSPARENT,
    },
};
#[cfg(target_os = "macos")]
use winit::platform::macos::WindowExtMacOS;
#[cfg(target_os = "windows")]
use winit::platform::windows::WindowExtWindows;

#[derive(Debug, Clone, Copy)]
pub enum OverlayEvent {
    Show,
    Hide,
    Toggle,
}

#[cfg(target_os = "windows")]
use std::sync::OnceLock;
#[cfg(target_os = "windows")]
static EVENT_PROXY: OnceLock<EventLoopProxy<OverlayEvent>> = OnceLock::new();

pub fn run(
    image_path: Option<&Path>,
    width: u32,
    height: u32,
    opacity: f32,
    invert: bool,
    _persist: bool,
    hotkey: Vec<String>,
) -> Result<()> {
    let hotkey_str = hotkey.join("+");
    let img_src = image_path
        .map(|p| p.display().to_string())
        .unwrap_or_else(|| "embedded image".into());
    let mut builder = EventLoopBuilder::<OverlayEvent>::with_user_event();
    #[cfg(target_os = "windows")]
    {
        use windows_sys::Win32::UI::WindowsAndMessaging::{MSG, WM_HOTKEY};
        use winit::platform::windows::EventLoopBuilderExtWindows;
        let hk = hotkey_str.clone();
        builder.with_msg_hook(move |msg| unsafe {
            let msg = &*(msg as *const MSG);
            if msg.message == WM_HOTKEY {
                debug!("hotkey triggered: {hk}");
                if let Some(p) = EVENT_PROXY.get() {
                    let _ = p.send_event(OverlayEvent::Toggle);
                }
                true
            } else {
                false
            }
        });
    }
    let event_loop = builder.build();
    let proxy = event_loop.create_proxy();
    #[cfg(target_os = "windows")]
    let _ = EVENT_PROXY.set(proxy.clone());

    let window = WindowBuilder::new()
        .with_decorations(false)
        .with_transparent(true)
        .with_window_level(WindowLevel::AlwaysOnTop)
        .with_visible(false)
        .build(&event_loop)?;

    // make window click-through and shadowless
    #[cfg(target_os = "macos")]
    {
        // winit 0.28 removed `set_ignores_mouse_events` in favour of the
        // cross-platform `set_cursor_hittest`. Disable hit testing to allow
        // the overlay window to be click-through.
        let _ = window.set_cursor_hittest(false);
        window.set_has_shadow(false);
    }
    #[cfg(target_os = "windows")]
    {
        let hwnd = window.hwnd() as HWND;
        unsafe {
            let ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            SetWindowLongW(
                hwnd,
                GWL_EXSTYLE,
                ex | WS_EX_LAYERED as i32 | WS_EX_TRANSPARENT as i32,
            );
        }
    }

    // Load image data
    let mut img = load_image(image_path);
    if let Some(ref i) = img {
        if i.width() != width || i.height() != height {
            img = Some(image::imageops::resize(
                i,
                width,
                height,
                FilterType::Triangle,
            ));
        }
    }
    // Apply opacity and inversion once and cache the transformed buffer
    let img = img.map(|i| {
        i.pixels()
            .map(|pixel| {
                let rgba = pixel.0;
                let (r, g, b) = if invert {
                    (255 - rgba[0], 255 - rgba[1], 255 - rgba[2])
                } else {
                    (rgba[0], rgba[1], rgba[2])
                };
                let a = (rgba[3] as f32 * opacity) as u32;
                (a << 24) | ((r as u32) << 16) | ((g as u32) << 8) | (b as u32)
            })
            .collect::<Vec<u32>>()
    });

    window.set_inner_size(LogicalSize::new(width, height));
    bottom_center_on_target(&window, width, height);
    let context = unsafe { softbuffer::Context::new(&window) }?;
    let mut surface = unsafe { softbuffer::Surface::new(&context, &window) }?;
    let width_nz = NonZeroU32::new(width).context("width must be non-zero")?;
    let height_nz = NonZeroU32::new(height).context("height must be non-zero")?;
    surface.resize(width_nz, height_nz)?;
    let buffer = Arc::new(Mutex::new(img));

    #[cfg(target_os = "windows")]
    {
        use windows_sys::Win32::UI::WindowsAndMessaging::{RegisterHotKey, MOD_NOREPEAT};
        let (mods, key) = parse_hotkey_windows(&hotkey);
        unsafe {
            RegisterHotKey(window.hwnd() as HWND, 1, mods | MOD_NOREPEAT, key);
        }
        debug!("registered hotkey: {hotkey_str}");
    }

    #[cfg(target_os = "macos")]
    {
        start_hotkey_listener(hotkey, hotkey_str.clone(), proxy.clone());
        debug!("registered hotkey: {hotkey_str}");
    }

    let mut visible = false;
    event_loop.run(move |event, _, control_flow| {
        *control_flow = ControlFlow::Wait;
        match event {
            Event::UserEvent(OverlayEvent::Show) => {
                if buffer.lock().unwrap().is_some() {
                    bottom_center_on_target(&window, width, height);
                    window.set_visible(true);
                    window.request_redraw();
                    visible = true;
                    debug!("overlay shown {}x{} using {img_src}", width, height);
                }
            }
            Event::UserEvent(OverlayEvent::Hide) => {
                window.set_visible(false);
                visible = false;
                debug!("overlay hidden");
            }
            Event::UserEvent(OverlayEvent::Toggle) => {
                debug!("overlay toggle requested (visible={visible})");
                if visible {
                    window.set_visible(false);
                    visible = false;
                    debug!("overlay hidden");
                } else if buffer.lock().unwrap().is_some() {
                    bottom_center_on_target(&window, width, height);
                    window.set_visible(true);
                    window.request_redraw();
                    visible = true;
                    debug!("overlay shown {}x{} using {img_src}", width, height);
                }
            }
            Event::RedrawRequested(_) => {
                let mut guard = buffer.lock().unwrap();
                if let Some(img) = &*guard {
                    match surface.buffer_mut() {
                        Ok(mut frame) => {
                            frame.copy_from_slice(img);
                            match frame.present() {
                                Ok(_) => debug!("rendered image buffer {}x{}", width, height),
                                Err(e) => debug!("failed to render image buffer: {e}"),
                            }
                        }
                        Err(e) => debug!("failed to obtain frame buffer: {e}"),
                    }
                } else {
                    debug!("no image buffer to render");
                }
            }
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => {
                *control_flow = ControlFlow::Exit;
            }
            _ => {}
        }
    });
}

fn load_image(image_path: Option<&Path>) -> Option<RgbaImage> {
    if let Some(path) = image_path {
        match image::open(path) {
            Ok(i) => return Some(i.to_rgba8()),
            Err(e) => error!("failed to load image {}: {e}", path.display()),
        }
    }

    if let Ok(mut exe) = std::env::current_exe() {
        exe.set_file_name("keymap.png");
        match image::open(&exe) {
            Ok(i) => return Some(i.to_rgba8()),
            Err(e) => {
                if exe.exists() {
                    error!("failed to load {}: {e}", exe.display());
                }
            }
        }
    }

    match image::load_from_memory(DEFAULT_IMAGE) {
        Ok(i) => {
            warn!("falling back to built-in image");
            Some(i.to_rgba8())
        }
        Err(e) => {
            error!("failed to load built-in image: {e}");
            None
        }
    }
}

fn target_point() -> Option<(i32, i32)> {
    if let Ok(active) = get_active_window() {
        let p = active.position;
        let cx = (p.x + p.width / 2.0) as i32;
        let cy = (p.y + p.height / 2.0) as i32;
        return Some((cx, cy));
    }
    match Mouse::get_mouse_position() {
        Mouse::Position { x, y } => Some((x, y)),
        Mouse::Error => None,
    }
}

fn bottom_center_on_target(window: &Window, width: u32, height: u32) {
    if let Some((cx, cy)) = target_point() {
        if let Ok(info) = DisplayInfo::from_point(cx, cy) {
            let pos_x = info.x + (info.width as i32 - width as i32) / 2;
            let pos_y = info.y + (info.height as i32 - height as i32);
            window.set_outer_position(PhysicalPosition::new(pos_x, pos_y));
            window.set_inner_size(LogicalSize::new(width, height));
        }
    }
}

#[cfg(target_os = "windows")]
fn parse_hotkey_windows(keys: &[String]) -> (u32, u32) {
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{
        MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN, VK_OEM_2,
    };
    let mut mods = 0u32;
    let mut vk = 0u32;
    for k in keys {
        match k.to_ascii_lowercase().as_str() {
            "alt" | "option" | "opt" => mods |= MOD_ALT,
            "ctrl" | "control" | "controlleft" | "controlright" => mods |= MOD_CONTROL,
            "shift" | "shiftleft" | "shiftright" => mods |= MOD_SHIFT,
            "meta" | "metaleft" | "metaright" | "win" | "command" | "cmd" => mods |= MOD_WIN,
            "slash" => vk = VK_OEM_2,
            n if n.len() == 1 && n.chars().all(|c| c.is_ascii_alphabetic()) => {
                vk = n.chars().next().unwrap().to_ascii_uppercase() as u32;
            }
            n if n.len() == 1 && n.chars().all(|c| c.is_ascii_digit()) => {
                vk = n.chars().next().unwrap() as u32;
            }
            _ => {}
        }
    }
    (mods, vk)
}

#[cfg(target_os = "macos")]
fn start_hotkey_listener(
    hotkey: Vec<String>,
    hotkey_display: String,
    proxy: EventLoopProxy<OverlayEvent>,
) {
    use core_foundation::base::kCFAllocatorDefault;
    use core_foundation::mach_port::CFMachPortCreateRunLoopSource;
    use core_foundation::runloop::{
        kCFRunLoopCommonModes, CFRunLoopAddSource, CFRunLoopGetCurrent,
    };
    use core_graphics::event::{
        kCGKeyboardEventKeycode, CGEventFlags, CGEventGetFlags, CGEventGetIntegerValueField,
        CGEventMaskBit, CGEventTapCreate, CGEventTapLocation, CGEventTapOptions,
        CGEventTapPlacement, CGEventTapProxy, CGEventType,
    };
    use std::os::raw::c_void;

    let (mods, key) = parse_hotkey_macos(&hotkey);

    #[repr(C)]
    struct HotkeyData {
        mods: CGEventFlags,
        key: u32,
        proxy: EventLoopProxy<OverlayEvent>,
        hotkey: String,
    }

    extern "C" fn handler(
        _proxy: CGEventTapProxy,
        ty: CGEventType,
        event: core_graphics::sys::CGEventRef,
        user: *mut c_void,
    ) -> core_graphics::sys::CGEventRef {
        unsafe {
            if ty == CGEventType::KeyDown {
                let data = &*(user as *const HotkeyData);
                let flags = CGEventGetFlags(event);
                let keycode =
                    CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode as u64) as u32;
                if flags.contains(data.mods) && keycode == data.key {
                    let _ = data.proxy.send_event(OverlayEvent::Toggle);
                    log::debug!("hotkey triggered: {}", data.hotkey);
                }
            }
            event
        }
    }

    let data = Box::new(HotkeyData {
        mods,
        key,
        proxy,
        hotkey: hotkey_display,
    });
    unsafe {
        let tap = CGEventTapCreate(
            CGEventTapLocation::HID,
            CGEventTapPlacement::HeadInsertEventTap,
            CGEventTapOptions::Default,
            CGEventMaskBit(CGEventType::KeyDown),
            Some(handler),
            Box::into_raw(data) as *mut c_void,
        );
        if tap.is_null() {
            return;
        }
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        core_graphics::event::CGEventTapEnable(tap, true);
    }
}

#[cfg(target_os = "macos")]
fn parse_hotkey_macos(keys: &[String]) -> (CGEventFlags, u32) {
    use core_graphics::event::CGEventFlags;
    let mut mods = CGEventFlags::empty();
    let mut keycode = 0u32;
    for k in keys {
        match k.to_ascii_lowercase().as_str() {
            "alt" | "option" | "opt" => {
                mods.insert(CGEventFlags::CGEventFlagAlternate);
            }
            "ctrl" | "control" | "controlleft" | "controlright" => {
                mods.insert(CGEventFlags::CGEventFlagControl);
            }
            "shift" | "shiftleft" | "shiftright" => {
                mods.insert(CGEventFlags::CGEventFlagShift);
            }
            "meta" | "metaleft" | "metaright" | "command" | "cmd" => {
                mods.insert(CGEventFlags::CGEventFlagCommand);
            }
            "slash" => keycode = 0x2C, // kVK_ANSI_Slash
            n if n.len() == 1 && n.chars().all(|c| c.is_ascii_alphabetic()) => {
                keycode = match n.chars().next().unwrap().to_ascii_uppercase() {
                    'A' => 0x00,
                    'B' => 0x0B,
                    'C' => 0x08,
                    'D' => 0x02,
                    'E' => 0x0E,
                    'F' => 0x03,
                    'G' => 0x05,
                    'H' => 0x04,
                    'I' => 0x22,
                    'J' => 0x26,
                    'K' => 0x28,
                    'L' => 0x25,
                    'M' => 0x2E,
                    'N' => 0x2D,
                    'O' => 0x1F,
                    'P' => 0x23,
                    'Q' => 0x0C,
                    'R' => 0x0F,
                    'S' => 0x01,
                    'T' => 0x11,
                    'U' => 0x20,
                    'V' => 0x09,
                    'W' => 0x0D,
                    'X' => 0x07,
                    'Y' => 0x10,
                    'Z' => 0x06,
                    _ => keycode,
                };
            }
            n if n.len() == 1 && n.chars().all(|c| c.is_ascii_digit()) => {
                keycode = match n {
                    "0" => 0x1D,
                    "1" => 0x12,
                    "2" => 0x13,
                    "3" => 0x14,
                    "4" => 0x15,
                    "5" => 0x17,
                    "6" => 0x16,
                    "7" => 0x1A,
                    "8" => 0x1C,
                    "9" => 0x19,
                    _ => keycode,
                };
            }
            _ => {}
        }
    }
    (mods, keycode)
}
