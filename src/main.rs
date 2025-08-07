mod autostart;

use anyhow::Result;
use clap::{Parser, Subcommand, ValueEnum};

#[derive(Parser)]
#[command(author, version, about)]
struct Cli {
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
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum AutostartAction {
    Enable,
    Disable,
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Some(Commands::Autostart { action }) => match action {
            AutostartAction::Enable => autostart::enable()?,
            AutostartAction::Disable => autostart::disable()?,
        },
        None => {
            // Placeholder for main application logic.
            println!("kbd_layout_overlay running. Use 'autostart enable' or 'autostart disable'.");
        }
    }

    Ok(())
}
