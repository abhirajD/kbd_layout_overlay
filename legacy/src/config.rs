use std::fs;
use std::path::PathBuf;

use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Config {
    pub image_path: Option<PathBuf>,
    pub width: u32,
    pub height: u32,
    pub opacity: f32,
    #[serde(default)]
    pub invert: bool,
    #[serde(default)]
    pub persist: bool,
    #[serde(default = "default_hotkey")]
    pub hotkey: Vec<String>,
    #[serde(default)]
    pub autostart: bool,
    #[serde(default = "default_log")]
    pub log: log::LevelFilter,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            image_path: None,
            width: 742,
            height: 235,
            opacity: 0.3,
            invert: false,
            persist: false,
            hotkey: default_hotkey(),
            autostart: false,
            log: default_log(),
        }
    }
}

fn default_hotkey() -> Vec<String> {
    vec![
        "ControlLeft".into(),
        "Alt".into(),
        "ShiftLeft".into(),
        "Slash".into(),
    ]
}

fn default_log() -> log::LevelFilter {
    log::LevelFilter::Info
}

impl Config {
    pub fn load() -> Result<Self> {
        let dir = dirs::config_dir()
            .ok_or_else(|| anyhow!("no config directory"))?
            .join("kbd_overlay");
        let path = dir.join("config.json");
        if let Ok(bytes) = fs::read(&path) {
            if let Ok(mut cfg) = serde_json::from_slice::<Self>(&bytes) {
                if let Some(p) = &cfg.image_path {
                    if !p.exists() {
                        cfg.image_path = None;
                    }
                }
                return Ok(cfg);
            }
        }
        Ok(Self::default())
    }

    pub fn save(&self) -> Result<()> {
        let dir = dirs::config_dir()
            .ok_or_else(|| anyhow!("no config directory"))?
            .join("kbd_overlay");
        fs::create_dir_all(&dir)?;
        let path = dir.join("config.json");
        fs::write(path, serde_json::to_vec_pretty(self)?)?;
        Ok(())
    }
}
