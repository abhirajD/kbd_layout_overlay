use anyhow::Result;

#[cfg(target_os = "windows")]
mod imp {
    use super::*;
    use std::env;
    use winreg::enums::*;
    use winreg::RegKey;

    const RUN_KEY: &str = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const VALUE_NAME: &str = "kbd_layout_overlay";

    pub fn enable() -> Result<()> {
        let exe = env::current_exe()?;
        let exe_str = exe.to_string_lossy().into_owned();
        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        let (key, _) = hkcu.create_subkey_with_flags(RUN_KEY, KEY_WRITE)?;
        key.set_value(VALUE_NAME, &exe_str)?;
        Ok(())
    }

    pub fn disable() -> Result<()> {
        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        if let Ok(key) = hkcu.open_subkey_with_flags(RUN_KEY, KEY_WRITE) {
            let _ = key.delete_value(VALUE_NAME);
        }
        Ok(())
    }
}

#[cfg(target_os = "macos")]
mod imp {
    use super::*;
    use std::env;
    use std::fs;
    use std::path::PathBuf;
    use std::process::Command;

    const LABEL: &str = "com.kbd_layout_overlay";

    fn plist_path() -> PathBuf {
        let home = dirs::home_dir().expect("home directory");
        home.join("Library/LaunchAgents").join(format!("{}.plist", LABEL))
    }

    pub fn enable() -> Result<()> {
        let exe = env::current_exe()?;
        let plist = format!(r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>{label}</string>
    <key>ProgramArguments</key>
    <array>
        <string>{exe}</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
"#, label = LABEL, exe = exe.display());

        let path = plist_path();
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&path, plist)?;
        let _ = Command::new("launchctl").arg("load").arg(&path).output();
        Ok(())
    }

    pub fn disable() -> Result<()> {
        let path = plist_path();
        let _ = Command::new("launchctl").arg("unload").arg(&path).output();
        let _ = fs::remove_file(path);
        Ok(())
    }
}

#[cfg(not(any(target_os = "windows", target_os = "macos")))]
mod imp {
    use super::*;
    pub fn enable() -> Result<()> {
        log::warn!("Autostart not supported on this platform.");
        Ok(())
    }
    pub fn disable() -> Result<()> {
        log::warn!("Autostart not supported on this platform.");
        Ok(())
    }
}

pub use imp::{disable, enable};
