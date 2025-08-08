mod autostart;
mod config;

#[cfg(any(target_os = "windows", target_os = "macos"))]
mod overlay;

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
mod overlay {
    use anyhow::Result;
    use std::path::Path;
    pub fn run(_img: Option<&Path>, _w: u32, _h: u32, _o: f32) -> Result<()> {
        println!("overlay not supported on this platform");
        Ok(())
    }
}

use std::path::PathBuf;

use anyhow::Result;
use clap::{Parser, Subcommand, ValueEnum};

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
    /// Enable or disable autostart
    #[arg(long)]
    autostart: Option<bool>,
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
            if let Some(a) = cli.autostart {
                cfg.autostart = a;
            }
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
        println!("Monitor {i}: {name}, scale {}", m.scale_factor());
    }
}

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
fn diagnose() {
    println!("diagnose not supported on this platform");
}
