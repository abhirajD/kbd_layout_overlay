mod autostart;
mod config;

#[cfg(any(target_os = "windows", target_os = "macos"))]
mod overlay;

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
mod overlay {
    use anyhow::Result;
    use rdev::Key;
    use std::path::Path;
    pub fn run(
        _img: Option<&Path>,
        _w: u32,
        _h: u32,
        _o: f32,
        _i: bool,
        _p: bool,
        _hotkey: Vec<Key>,
    ) -> Result<()> {
        log::warn!("overlay not supported on this platform");
        Ok(())
    }
}

use std::path::PathBuf;

use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand, ValueEnum};
use log::warn;
use rdev::Key;

#[derive(Parser)]
#[command(author, version, about)]
struct Cli {
    /// Path to overlay image
    #[arg(long)]
    image: Option<PathBuf>,
    /// Overlay width in pixels
    #[arg(long)]
    width: Option<u32>,
    /// Overlay height in pixels
    #[arg(long)]
    height: Option<u32>,
    /// Overlay opacity (0.0 - 1.0)
    #[arg(long)]
    opacity: Option<f32>,
    /// Invert image colors
    #[arg(long)]
    invert: Option<bool>,
    /// Persist overlay until hotkey is pressed again
    #[arg(long)]
    persist: Option<bool>,
    /// Enable or disable autostart
    #[arg(long)]
    autostart: Option<bool>,
    /// Hotkey combination (e.g., ControlLeft+Alt+ShiftLeft+Slash)
    #[arg(long, value_delimiter = '+')]
    hotkey: Option<Vec<String>>,
    #[command(subcommand)]
    command: Option<Commands>,
}

#[derive(Subcommand)]
enum Commands {
    /// Manage autostart registration
    Autostart {
        #[arg(value_enum)]
        action: AutostartAction,
    },
    /// Print monitor and DPI information
    Diagnose,
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum AutostartAction {
    Enable,
    Disable,
}

fn main() -> Result<()> {
    env_logger::init();
    let cli = Cli::parse();

    match &cli.command {
        Some(Commands::Autostart { action }) => match action {
            AutostartAction::Enable => autostart::enable()?,
            AutostartAction::Disable => autostart::disable()?,
        },
        Some(Commands::Diagnose) => {
            diagnose();
        }
        None => {
            let mut cfg = config::Config::load()?;
            if let Some(p) = cli.image {
                cfg.image_path = Some(p);
            }
            if let Some(w) = cli.width {
                cfg.width = w;
            }
            if let Some(h) = cli.height {
                cfg.height = h;
            }
            if let Some(o) = cli.opacity {
                cfg.opacity = o;
            }
            if let Some(i) = cli.invert {
                cfg.invert = i;
            }
            if let Some(persist) = cli.persist {
                cfg.persist = persist;
            }
            if let Some(a) = cli.autostart {
                cfg.autostart = a;
            }
            if let Some(h) = cli.hotkey {
                let keys = parse_hotkey(&h)?;
                validate_hotkey(&keys)?;
                cfg.hotkey = keys;
            }
            validate_hotkey(&cfg.hotkey)?;
            cfg.save()?;
            if cfg.autostart {
                autostart::enable()?;
            } else {
                autostart::disable()?;
            }
            overlay::run(
                cfg.image_path.as_deref(),
                cfg.width,
                cfg.height,
                cfg.opacity,
                cfg.invert,
                cfg.persist,
                cfg.hotkey.clone(),
            )?;
        }
    }

    Ok(())
}

#[cfg(any(target_os = "windows", target_os = "macos"))]
fn diagnose() {
    let event_loop = winit::event_loop::EventLoop::<()>::new();
    for (i, m) in event_loop.available_monitors().enumerate() {
        let name = m.name().unwrap_or_else(|| "unknown".into());
        log::info!("Monitor {i}: {name}, scale {}", m.scale_factor());
    }
}

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
fn diagnose() {
    warn!("diagnose not supported on this platform");
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
