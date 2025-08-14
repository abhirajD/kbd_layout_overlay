mod autostart;
mod config;
mod gui;

#[cfg(any(target_os = "windows", target_os = "macos"))]
mod overlay;
#[cfg(all(feature = "tray", any(target_os = "windows", target_os = "macos")))]
mod tray;

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
mod overlay {
    use anyhow::Result;
    use std::path::Path;
    pub fn run(
        _img: Option<&Path>,
        _w: u32,
        _h: u32,
        _o: f32,
        _i: bool,
        _p: bool,
        _hotkey: Vec<String>,
    ) -> Result<()> {
        log::warn!("overlay not supported on this platform");
        Ok(())
    }
}

use std::path::PathBuf;

use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand, ValueEnum};
use log::warn;

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
    /// Log level (error, warn, info, debug, trace)
    #[arg(long = "log-level", alias = "logs", value_enum)]
    log_level: Option<LogLevel>,
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
    /// Launch graphical settings interface
    Gui,
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum AutostartAction {
    Enable,
    Disable,
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum LogLevel {
    Error,
    Warn,
    Info,
    Debug,
    Trace,
}

impl From<LogLevel> for log::LevelFilter {
    fn from(l: LogLevel) -> Self {
        match l {
            LogLevel::Error => log::LevelFilter::Error,
            LogLevel::Warn => log::LevelFilter::Warn,
            LogLevel::Info => log::LevelFilter::Info,
            LogLevel::Debug => log::LevelFilter::Debug,
            LogLevel::Trace => log::LevelFilter::Trace,
        }
    }
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    let mut cfg = config::Config::load()?;
    let level = cli
        .log_level
        .map(Into::into)
        .unwrap_or(cfg.log);
    cfg.log = level;
    env_logger::Builder::from_env(env_logger::Env::default())
        .filter_level(level)
        .init();

    match &cli.command {
        Some(Commands::Autostart { action }) => match action {
            AutostartAction::Enable => autostart::enable()?,
            AutostartAction::Disable => autostart::disable()?,
        },
        Some(Commands::Diagnose) => {
            diagnose();
        }
        Some(Commands::Gui) => {
            gui::run()?;
        }
        None => {
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
            #[cfg(all(feature = "tray", any(target_os = "windows", target_os = "macos")))]
            {
                tray::run(cfg)?;
            }
            #[cfg(not(all(feature = "tray", any(target_os = "windows", target_os = "macos"))))]
            {
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
