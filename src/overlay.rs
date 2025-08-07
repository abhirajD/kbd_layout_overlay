use std::path::Path;
use std::sync::{Arc, Mutex};
use std::num::NonZeroU32;

use anyhow::Result;
use winit::{
    dpi::{LogicalSize, PhysicalPosition, PhysicalSize},
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder},
    window::{WindowBuilder, WindowLevel},
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

    // Center window on primary monitor
    if let Some(monitor) = window.primary_monitor() {
        let scale = monitor.scale_factor();
        let size = LogicalSize::new(width, height);
        window.set_inner_size(size);
        let PhysicalSize { width, height }: PhysicalSize<u32> = size.to_physical(scale);
        let monitor_size = monitor.size();
        let pos_x = monitor.position().x + (monitor_size.width as i32 - width as i32) / 2;
        let pos_y = monitor.position().y + (monitor_size.height as i32 - height as i32) / 2;
        window.set_outer_position(PhysicalPosition::new(pos_x, pos_y));
    }

    // Load image data
    let img = image::open(image_path).ok().map(|i| i.to_rgba8());
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
                window.set_visible(true);
                window.request_redraw();
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
