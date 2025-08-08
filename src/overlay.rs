use std::path::Path;
use std::sync::{Arc, Mutex};
use std::num::NonZeroU32;

use anyhow::Result;
use display_info::DisplayInfo;
use active_win_pos_rs::get_active_window;
use mouse_position::mouse_position::Mouse;
use winit::{
    dpi::{LogicalSize, PhysicalPosition},
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder},
    window::{WindowBuilder, WindowLevel, Window},
};

#[cfg(target_os = "macos")]
use winit::platform::macos::WindowExtMacOS;
#[cfg(target_os = "windows")]
use winit::platform::windows::WindowExtWindows;
#[cfg(target_os = "windows")]
use windows_sys::Win32::{
    Foundation::HWND,
    UI::WindowsAndMessaging::{GetWindowLongW, SetWindowLongW, GWL_EXSTYLE, WS_EX_LAYERED, WS_EX_TRANSPARENT},
};

#[derive(Debug, Clone, Copy)]
pub enum OverlayEvent {
    Show,
    Hide,
}

pub fn run(image_path: &Path, width: u32, height: u32, opacity: f32) -> Result<()> {
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
        window.set_ignores_mouse_events(true);
        window.set_has_shadow(false);
    }
    #[cfg(target_os = "windows")]
    {
        let hwnd = window.hwnd() as HWND;
        unsafe {
            let ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED as i32 | WS_EX_TRANSPARENT as i32);
        }
    }

    window.set_inner_size(LogicalSize::new(width, height));
    center_on_target(&window, width, height);

    // Load image data
    let img = match image::open(image_path) {
        Ok(i) => Some(i.to_rgba8()),
        Err(e) => {
            eprintln!("failed to load image {}: {e}", image_path.display());
            None
        }
    };
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
    std::thread::spawn(move || {
        use rdev::{listen, EventType, Key};
        use std::collections::HashSet;
        let required: [Key; 4] = [Key::ControlLeft, Key::Alt, Key::ShiftLeft, Key::Slash];
        let mut pressed = HashSet::new();
        let _ = listen(move |event| {
            match event.event_type {
                EventType::KeyPress(key) => {
                    pressed.insert(key);
                    if required.iter().all(|k| pressed.contains(k)) {
                        let _ = proxy.send_event(OverlayEvent::Show);
                    }
                }
                EventType::KeyRelease(key) => {
                    pressed.remove(&key);
                    let _ = proxy.send_event(OverlayEvent::Hide);
                }
                _ => {}
            }
        });
    });

    event_loop.run(move |event, _, control_flow| {
        *control_flow = ControlFlow::Wait;
        match event {
            Event::UserEvent(OverlayEvent::Show) => {
                if buffer.lock().unwrap().is_some() {
                    center_on_target(&window, width, height);
                    window.set_visible(true);
                    window.request_redraw();
                }
            }
            Event::UserEvent(OverlayEvent::Hide) => {
                window.set_visible(false);
            }
            Event::RedrawRequested(_) => {
                if let Some(img) = &*buffer.lock().unwrap() {
                    let mut frame = surface.buffer_mut().unwrap();
                    for (i, pixel) in img.pixels().enumerate() {
                        let rgba = pixel.0;
                        let a = (rgba[3] as f32 * opacity) as u32;
                        frame[i] = (a << 24)
                            | ((rgba[0] as u32) << 16)
                            | ((rgba[1] as u32) << 8)
                            | (rgba[2] as u32);
                    }
                    frame.present().unwrap();
                }
            }
            Event::WindowEvent { event: WindowEvent::CloseRequested, .. } => {
                *control_flow = ControlFlow::Exit;
            }
            _ => {}
        }
    });
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

fn center_on_target(window: &Window, width: u32, height: u32) {
    if let Some((cx, cy)) = target_point() {
        if let Ok(info) = DisplayInfo::from_point(cx, cy) {
            let pos_x = info.x + (info.width as i32 - width as i32) / 2;
            let pos_y = info.y + (info.height as i32 - height as i32) / 2;
            window.set_outer_position(PhysicalPosition::new(pos_x, pos_y));
            window.set_inner_size(LogicalSize::new(width, height));
        }
    }
}
