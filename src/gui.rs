use std::path::PathBuf;

use anyhow::{anyhow, Result};
use eframe::{egui, App, Frame};
use egui::{Color32, ColorImage, TextureHandle};
use image::{imageops::FilterType, RgbaImage};
use rdev::Key;

use crate::config::Config;

const DEFAULT_IMAGE: &[u8] = include_bytes!("../assets/keymap.png");

pub fn run() -> Result<()> {
    let cfg = Config::load()?;
    let native_options = eframe::NativeOptions::default();
    eframe::run_native(
        "kbd_layout_overlay settings",
        native_options,
        Box::new(move |cc| {
            cc.egui_ctx.set_visuals(fixed_visuals());
            Box::new(GuiApp::new(cfg))
        }),
    )
    .map_err(|e| anyhow!(e.to_string()))?;
    Ok(())
}

fn fixed_visuals() -> egui::Visuals {
    let mut visuals = egui::Visuals::dark();
    visuals.override_text_color = Some(Color32::WHITE);
    visuals.panel_fill = Color32::from_rgb(30, 30, 30);
    visuals
}

struct GuiApp {
    cfg: Config,
    image_input: String,
    hotkey_input: String,
    preview: Option<TextureHandle>,
    dirty_preview: bool,
    error: Option<String>,
}

impl GuiApp {
    fn new(cfg: Config) -> Self {
        let image_input = cfg
            .image_path
            .as_ref()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_default();
        let hotkey_input = hotkey_to_string(&cfg.hotkey);
        Self {
            cfg,
            image_input,
            hotkey_input,
            preview: None,
            dirty_preview: true,
            error: None,
        }
    }

    fn refresh_preview(&mut self, ctx: &egui::Context) {
        let img = load_image(self.cfg.image_path.as_ref());
        if let Some(mut img) = img {
            if img.width() != self.cfg.width || img.height() != self.cfg.height {
                img = image::imageops::resize(
                    &img,
                    self.cfg.width,
                    self.cfg.height,
                    FilterType::Triangle,
                );
            }
            if self.cfg.invert {
                for p in img.pixels_mut() {
                    p.0[0] = 255 - p.0[0];
                    p.0[1] = 255 - p.0[1];
                    p.0[2] = 255 - p.0[2];
                }
            }
            for p in img.pixels_mut() {
                p.0[3] = ((p.0[3] as f32) * self.cfg.opacity) as u8;
            }
            let color = ColorImage::from_rgba_unmultiplied(
                [img.width() as usize, img.height() as usize],
                img.as_raw(),
            );
            self.preview =
                Some(ctx.load_texture("preview", color, egui::TextureOptions::default()));
        } else {
            self.preview = None;
        }
    }
}

impl App for GuiApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut Frame) {
        if self.dirty_preview {
            self.refresh_preview(ctx);
            self.dirty_preview = false;
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            if let Some(err) = &self.error {
                ui.colored_label(egui::Color32::RED, err);
            }
            ui.label("Image Path");
            if ui.text_edit_singleline(&mut self.image_input).changed() {
                self.cfg.image_path = if self.image_input.is_empty() {
                    None
                } else {
                    Some(PathBuf::from(self.image_input.clone()))
                };
                self.dirty_preview = true;
            }
            ui.horizontal(|ui| {
                ui.label("Width");
                if ui
                    .add(egui::DragValue::new(&mut self.cfg.width).clamp_range(1..=5000))
                    .changed()
                {
                    self.dirty_preview = true;
                }
                ui.label("Height");
                if ui
                    .add(egui::DragValue::new(&mut self.cfg.height).clamp_range(1..=5000))
                    .changed()
                {
                    self.dirty_preview = true;
                }
            });
            if ui
                .add(egui::Slider::new(&mut self.cfg.opacity, 0.0..=1.0).text("Opacity"))
                .changed()
            {
                self.dirty_preview = true;
            }
            if ui.checkbox(&mut self.cfg.invert, "Invert").changed() {
                self.dirty_preview = true;
            }
            ui.checkbox(&mut self.cfg.persist, "Persist");
            ui.label("Hotkey (e.g., ControlLeft+Alt+ShiftLeft+Slash)");
            ui.text_edit_singleline(&mut self.hotkey_input);
            ui.separator();
            ui.label("Preview");
            if let Some(tex) = &self.preview {
                let size = tex.size_vec2();
                ui.add(egui::Image::new(tex).fit_to_exact_size(size));
            }
            if ui.button("Save").clicked() {
                match parse_hotkey_str(&self.hotkey_input) {
                    Ok(keys) => {
                        if let Err(e) = validate_hotkey(&keys) {
                            self.error = Some(e.to_string());
                            return;
                        }
                        self.cfg.hotkey = keys;
                        if let Err(e) = self.cfg.save() {
                            self.error = Some(e.to_string());
                        } else {
                            self.error = None;
                        }
                    }
                    Err(e) => self.error = Some(e.to_string()),
                }
            }
        });
    }
}

fn load_image(path: Option<&PathBuf>) -> Option<RgbaImage> {
    if let Some(p) = path {
        if let Ok(img) = image::open(p) {
            return Some(img.to_rgba8());
        }
    }
    image::load_from_memory(DEFAULT_IMAGE)
        .ok()
        .map(|i| i.to_rgba8())
}

fn hotkey_to_string(keys: &[Key]) -> String {
    keys.iter()
        .map(|k| format!("{:?}", k))
        .collect::<Vec<_>>()
        .join("+")
}

fn parse_hotkey_str(input: &str) -> Result<Vec<Key>> {
    if input.trim().is_empty() {
        return Ok(Vec::new());
    }
    let parts: Vec<String> = input.split('+').map(|s| s.trim().to_string()).collect();
    parse_hotkey(&parts)
}

fn parse_hotkey(names: &[String]) -> Result<Vec<Key>> {
    names
        .iter()
        .map(|n| {
            let name = if n.eq_ignore_ascii_case("option") || n.eq_ignore_ascii_case("opt") {
                "Alt"
            } else if n.eq_ignore_ascii_case("command") || n.eq_ignore_ascii_case("cmd") {
                "MetaLeft"
            } else {
                n.as_str()
            };
            serde_json::from_str::<Key>(&format!("\"{}\"", name))
                .map_err(|_| anyhow!("invalid key name: {n}"))
        })
        .collect()
}

fn validate_hotkey(keys: &[Key]) -> Result<()> {
    if !keys.iter().any(|&k| is_modifier(k)) {
        return Err(anyhow!("hotkey must include at least one modifier key"));
    }
    if !keys.iter().any(|&k| !is_modifier(k)) {
        return Err(anyhow!("hotkey must include at least one non-modifier key"));
    }
    Ok(())
}

fn is_modifier(key: Key) -> bool {
    matches!(
        key,
        Key::Alt
            | Key::AltGr
            | Key::ShiftLeft
            | Key::ShiftRight
            | Key::ControlLeft
            | Key::ControlRight
            | Key::MetaLeft
            | Key::MetaRight
    )
}
