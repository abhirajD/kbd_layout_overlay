use std::path::PathBuf;

use anyhow::{anyhow, Result};
use eframe::{egui, App, Frame};
use egui::{Color32, ColorImage, TextureHandle};
use image::{imageops::FilterType, RgbaImage};

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
    capturing: bool,
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
            capturing: false,
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
            ui.horizontal(|ui| {
                ui.text_edit_singleline(&mut self.hotkey_input);
                let btn_label = if self.capturing {
                    "Press keys..."
                } else {
                    "Capture"
                };
                if ui.button(btn_label).clicked() {
                    self.capturing = !self.capturing;
                }
            });
            if self.capturing {
                ctx.input(|i| {
                    for event in &i.events {
                        match event {
                            egui::Event::Key {
                                key, pressed: true, ..
                            } => {
                                if let Some(k) = egui_key_to_name(*key) {
                                    let mut keys = modifiers_to_names(i.modifiers);
                                    if !keys.contains(&k) {
                                        keys.push(k);
                                    }
                                    self.hotkey_input = hotkey_to_string(&keys);
                                    self.capturing = false;
                                    break;
                                }
                            }
                            egui::Event::Text(t) => {
                                if let Some(k) = text_to_name(t) {
                                    let mut keys = modifiers_to_names(i.modifiers);
                                    if !keys.contains(&k) {
                                        keys.push(k);
                                    }
                                    self.hotkey_input = hotkey_to_string(&keys);
                                    self.capturing = false;
                                    break;
                                }
                            }
                            _ => {}
                        }
                    }
                });
            }
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

fn hotkey_to_string(keys: &[String]) -> String {
    keys.join("+")
}

fn modifiers_to_names(m: egui::Modifiers) -> Vec<String> {
    let mut keys = Vec::new();
    if m.ctrl {
        keys.push("ControlLeft".into());
    }
    if m.shift {
        keys.push("ShiftLeft".into());
    }
    if m.alt {
        keys.push("Alt".into());
    }
    if m.mac_cmd {
        keys.push("Meta".into());
    }
    keys
}

fn egui_key_to_name(key: egui::Key) -> Option<String> {
    use egui::Key::*;
    let name = match key {
        ArrowDown => "DownArrow",
        ArrowLeft => "LeftArrow",
        ArrowRight => "RightArrow",
        ArrowUp => "UpArrow",
        Escape => "Escape",
        Tab => "Tab",
        Backspace => "Backspace",
        Enter => "Return",
        Space => "Space",
        Insert => "Insert",
        Delete => "Delete",
        Home => "Home",
        End => "End",
        PageUp => "PageUp",
        PageDown => "PageDown",
        Minus => "Minus",
        PlusEquals => "Equal",
        Num0 => "Num0",
        Num1 => "Num1",
        Num2 => "Num2",
        Num3 => "Num3",
        Num4 => "Num4",
        Num5 => "Num5",
        Num6 => "Num6",
        Num7 => "Num7",
        Num8 => "Num8",
        Num9 => "Num9",
        A => "KeyA",
        B => "KeyB",
        C => "KeyC",
        D => "KeyD",
        E => "KeyE",
        F => "KeyF",
        G => "KeyG",
        H => "KeyH",
        I => "KeyI",
        J => "KeyJ",
        K => "KeyK",
        L => "KeyL",
        M => "KeyM",
        N => "KeyN",
        O => "KeyO",
        P => "KeyP",
        Q => "KeyQ",
        R => "KeyR",
        S => "KeyS",
        T => "KeyT",
        U => "KeyU",
        V => "KeyV",
        W => "KeyW",
        X => "KeyX",
        Y => "KeyY",
        Z => "KeyZ",
        F1 => "F1",
        F2 => "F2",
        F3 => "F3",
        F4 => "F4",
        F5 => "F5",
        F6 => "F6",
        F7 => "F7",
        F8 => "F8",
        F9 => "F9",
        F10 => "F10",
        F11 => "F11",
        F12 => "F12",
        _ => return None,
    };
    Some(name.into())
}

fn text_to_name(t: &str) -> Option<String> {
    match t {
        "/" | "?" => Some("Slash".into()),
        "\\" | "|" => Some("BackSlash".into()),
        "," | "<" => Some("Comma".into()),
        "." | ">" => Some("Dot".into()),
        ";" | ":" => Some("SemiColon".into()),
        "'" | "\"" => Some("Quote".into()),
        "`" | "~" => Some("BackQuote".into()),
        "[" | "{" => Some("LeftBracket".into()),
        "]" | "}" => Some("RightBracket".into()),
        "-" | "_" => Some("Minus".into()),
        "=" | "+" => Some("Equal".into()),
        _ => None,
    }
}

fn parse_hotkey_str(input: &str) -> Result<Vec<String>> {
    if input.trim().is_empty() {
        return Ok(Vec::new());
    }
    let parts: Vec<String> = input.split('+').map(|s| s.trim().to_string()).collect();
    parse_hotkey(&parts)
}

fn parse_hotkey(names: &[String]) -> Result<Vec<String>> {
    names
        .iter()
        .map(|n| {
            if n.eq_ignore_ascii_case("option") || n.eq_ignore_ascii_case("opt") {
                Ok("Alt".to_string())
            } else if n.eq_ignore_ascii_case("command") || n.eq_ignore_ascii_case("cmd") {
                Ok("Meta".to_string())
            } else {
                Ok(n.clone())
            }
        })
        .collect()
}

fn validate_hotkey(keys: &[String]) -> Result<()> {
    if !keys.iter().any(|k| is_modifier(k)) {
        return Err(anyhow!("hotkey must include at least one modifier key"));
    }
    if !keys.iter().any(|k| !is_modifier(k)) {
        return Err(anyhow!("hotkey must include at least one non-modifier key"));
    }
    Ok(())
}

fn is_modifier(key: &str) -> bool {
    matches!(
        key.to_ascii_lowercase().as_str(),
        "alt" | "option" | "opt" | "shift" | "shiftleft" | "shiftright" | "control" |
        "ctrl" | "controlleft" | "controlright" | "meta" | "metaleft" | "metaright" | "command" | "cmd"
    )
}
