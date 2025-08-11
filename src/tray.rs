use std::thread;

use anyhow::Result;
use log::error;
use tray_icon::{
    menu::{Menu, MenuEvent, MenuItem},
    TrayIconBuilder,
};

use crate::{config::Config, gui};

const ICON_BYTES: &[u8] = include_bytes!("../assets/keymap.png");

fn load_icon() -> tray_icon::Icon {
    let image = image::load_from_memory(ICON_BYTES)
        .expect("icon")
        .into_rgba8();
    let (w, h) = image.dimensions();
    tray_icon::Icon::from_rgba(image.into_raw(), w, h).expect("icon")
}

pub fn run(cfg: Config) -> Result<()> {
    // spawn overlay thread
    let overlay_cfg = cfg.clone();
    thread::spawn(move || {
        if let Err(e) = crate::overlay::run(
            overlay_cfg.image_path.as_deref(),
            overlay_cfg.width,
            overlay_cfg.height,
            overlay_cfg.opacity,
            overlay_cfg.invert,
            overlay_cfg.persist,
            overlay_cfg.hotkey.clone(),
        ) {
            error!("overlay error: {e}");
        }
    });

    let menu = Menu::new();
    let settings_item = MenuItem::new("Settings", true, None);
    let quit_item = MenuItem::new("Quit", true, None);
    let _ = menu.append(&settings_item);
    let _ = menu.append(&quit_item);

    let _tray = TrayIconBuilder::new()
        .with_icon(load_icon())
        .with_menu(Box::new(menu))
        .with_tooltip("kbd_layout_overlay")
        .build()?;

    let rx = MenuEvent::receiver();
    for event in rx {
        if event.id == settings_item.id() {
            thread::spawn(|| {
                let _ = gui::run();
            });
        } else if event.id == quit_item.id() {
            std::process::exit(0);
        }
    }

    Ok(())
}
