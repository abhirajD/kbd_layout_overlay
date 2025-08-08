use std::num::NonZeroU32;
use std::path::Path;
use std::sync::{Arc, Mutex};

use image::{imageops::FilterType, RgbaImage};

use active_win_pos_rs::get_active_window;
use anyhow::Result;
use display_info::DisplayInfo;
use log::{error, warn};
use mouse_position::mouse_position::Mouse;
use winit::{
    dpi::{LogicalSize, PhysicalPosition},
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder},
    window::{Window, WindowBuilder, WindowLevel},
};

const DEFAULT_IMAGE: &[u8] = include_bytes!("../assets/keymap.png");

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

pub fn run(
    image_path: Option<&Path>,
    width: u32,
    height: u32,
    opacity: f32,
    invert: bool,
    persist: bool,
    hotkey: Vec<rdev::Key>,
) -> Result<()> {
    let event_loop = EventLoopBuilder::<OverlayEvent>::with_user_event().build();
    let proxy = event_loop.create_proxy();

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

    window.set_inner_size(LogicalSize::new(width, height));
    bottom_center_on_target(&window, width, height);
    let context = unsafe { softbuffer::Context::new(&window) }.unwrap();
    let mut surface = unsafe { softbuffer::Surface::new(&context, &window) }.unwrap();
    surface
        .resize(
            NonZeroU32::new(width).unwrap(),
            NonZeroU32::new(height).unwrap(),
        )
        .unwrap();
    let buffer = Arc::new(Mutex::new(img));

    // Spawn listener thread for hotkey
    {
        let proxy = proxy.clone();
        let required = hotkey;
        std::thread::spawn(move || {
            use rdev::{listen, EventType, Key};
            use std::collections::HashSet;
            let mut pressed = HashSet::new();
            let mut combo_active = false;
            let _ = listen(move |event| match event.event_type {
                EventType::KeyPress(key) => {
                    pressed.insert(key);
                    if required.iter().all(|k| pressed.contains(k)) {
                        if persist {
                            if !combo_active {
                                combo_active = true;
                                let _ = proxy.send_event(OverlayEvent::Toggle);
                            }
                        } else {
                            let _ = proxy.send_event(OverlayEvent::Show);
                        }
                    }
                }
                EventType::KeyRelease(key) => {
                    pressed.remove(&key);
                    if persist {
                        if !required.iter().all(|k| pressed.contains(k)) {
                            combo_active = false;
                        }
                    } else {
                        let _ = proxy.send_event(OverlayEvent::Hide);
                    }
                }
                _ => {}
            });
        });
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
                }
            }
            Event::UserEvent(OverlayEvent::Hide) => {
                window.set_visible(false);
                visible = false;
            }
            Event::UserEvent(OverlayEvent::Toggle) => {
                if visible {
                    window.set_visible(false);
                    visible = false;
                } else if buffer.lock().unwrap().is_some() {
                    bottom_center_on_target(&window, width, height);
                    window.set_visible(true);
                    window.request_redraw();
                    visible = true;
                }
            }
            Event::RedrawRequested(_) => {
                if let Some(img) = &*buffer.lock().unwrap() {
                    let mut frame = surface.buffer_mut().unwrap();
                    for (i, pixel) in img.pixels().enumerate() {
                        let rgba = pixel.0;
                        let (r, g, b) = if invert {
                            (255 - rgba[0], 255 - rgba[1], 255 - rgba[2])
                        } else {
                            (rgba[0], rgba[1], rgba[2])
                        };
                        let a = (rgba[3] as f32 * opacity) as u32;
                        frame[i] = (a << 24) | ((r as u32) << 16) | ((g as u32) << 8) | (b as u32);
                    }
                    frame.present().unwrap();
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
