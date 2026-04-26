use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::mpsc::channel;
use std::sync::Mutex;
use std::thread;
use std::time::Duration;

use notify::{Event, RecursiveMode, Watcher};
use once_cell::sync::Lazy;
use walkdir::WalkDir;
use windows::core::*;
use windows::Win32::Foundation::{FreeLibrary, HMODULE, HINSTANCE};
use windows::Win32::System::Console::{AllocConsole, GetStdHandle, WriteConsoleA, STD_OUTPUT_HANDLE};
use windows::Win32::System::LibraryLoader::{GetModuleHandleA, LoadLibraryA};
use windows::Win32::System::SystemServices::*;
use windows::Win32::UI::WindowsAndMessaging::MessageBoxA;

#[derive(Clone, Copy)]
struct SafeHMODULE(HMODULE);

unsafe impl Send for SafeHMODULE {}
unsafe impl Sync for SafeHMODULE {}

static LOADED_MODS: Lazy<Mutex<HashMap<String, SafeHMODULE>>> = Lazy::new(|| Mutex::new(HashMap::new()));

#[unsafe(no_mangle)]
#[allow(non_snake_case, unused_variables)]
extern "system" fn DllMain(
    dll_module: HINSTANCE,
    call_reason: u32,
    _: *mut ())
    -> bool
{
    match call_reason {
        DLL_PROCESS_ATTACH => attach(),
        DLL_PROCESS_DETACH => detach(),
        _ => ()
    }
    true
}

fn attach() {
    unsafe {
        AllocConsole().expect("AllocConsole failed:");
        
        let main_module = GetModuleHandleA(None).expect("GetModuleHandle failed:");
        write_console_line(&format!("Main Module: {:?}", main_module.0));

        init_mod_loader();
    }
}

fn init_mod_loader() {
    write_console_line("Initializing mod system...");

    let mods_dir = get_mods_directory();
    create_mods_directory(&mods_dir);

    load_existing_mods(&mods_dir);
    start_mod_watcher(mods_dir);
}

fn get_mods_directory() -> PathBuf {
    let mut mods_dir = dirs::document_dir()
        .expect("Failed to get user's Documents directory");
    mods_dir.push("proton-inject-mods");
    mods_dir
}

fn create_mods_directory(mods_dir: &Path) {
    if !mods_dir.exists() {
        std::fs::create_dir_all(mods_dir)
            .unwrap_or_else(|e| {
                write_console_line(&format!("Failed to create mods directory: {}", e));
            });
        write_console_line(&format!("Created mods directory: {:?}", mods_dir));
    } else {
        write_console_line(&format!("Mods directory already exists: {:?}", mods_dir));
    }
}

fn load_existing_mods(mods_dir: &Path) {
    write_console_line("Loading existing mods...");
    
    if !mods_dir.exists() {
        write_console_line("Mods directory doesn't exist, skipping existing mod load");
        return;
    }
    
    for entry in WalkDir::new(mods_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().is_file())
        .filter(|e| e.path().extension().and_then(|s| s.to_str()) == Some("dll"))
    {
        load_mod(entry.path());
    }
}

fn start_mod_watcher(mods_dir: PathBuf) {
    thread::spawn(move || {
        let (tx, rx) = channel();
        
        let mut watcher = notify::recommended_watcher(tx)
            .expect("Failed to create file watcher");
        
        watcher.watch(&mods_dir, RecursiveMode::Recursive)
            .expect("Failed to start watching mods directory");
        
        write_console_line("Started watching mods directory for changes...");
        
        loop {
            match rx.recv() {
                Ok(event) => {
                    match event {
                        Ok(Event { kind: notify::EventKind::Create(_), paths, .. }) => {
                            for path in paths {
                                if path.extension().and_then(|s| s.to_str()) == Some("dll") {
                                    thread::sleep(Duration::from_millis(100));
                                    load_mod(&path);
                                }
                            }
                        }
                        Ok(Event { kind: notify::EventKind::Remove(_), paths, .. }) => {
                            for path in paths {
                                if path.extension().and_then(|s| s.to_str()) == Some("dll") {
                                    unload_mod(&path);
                                }
                            }
                        }
                        _ => {}
                    }
                },
                Err(e) => {
                    write_console_line(&format!("Watch error: {:?}", e));
                    break;
                }
            }
        }
    });
}

fn load_mod(dll_path: &Path) {
    let path_str = dll_path.to_string_lossy();
    
    {
        let loaded_mods = LOADED_MODS.lock().unwrap();
        if loaded_mods.contains_key(path_str.as_ref()) {
            return;
        }
    }
    
    write_console_line(&format!("Loading mod: {}", path_str));

    let path_cstring = match std::ffi::CString::new(path_str.as_ref()) {
        Ok(cstring) => cstring,
        Err(e) => {
            write_console_line(&format!("Failed to convert path to CString: {}", e));
            return;
        }
    };
    
    unsafe {
        match LoadLibraryA(PCSTR(path_cstring.as_ptr() as *const u8)) {
            Ok(module) => {
                write_console_line(&format!("Successfully loaded mod: {} (Handle: {:?})", path_str, module.0));
                LOADED_MODS.lock().unwrap().insert(path_str.to_string(), SafeHMODULE(module));
            },
            Err(e) => {
                write_console_line(&format!("Failed to load mod {}: {:?}", path_str, e));
            }
        }
    }
}

fn unload_mod(dll_path: &Path) {
    let path_str = dll_path.to_string_lossy();
    
    write_console_line(&format!("Unloading mod: {}", path_str));
    
    let mut loaded_mods = LOADED_MODS.lock().unwrap();
    if let Some(handle) = loaded_mods.remove(path_str.as_ref()) {
        unsafe {
            match FreeLibrary(handle.0) {
                Ok(_) => {
                    write_console_line(&format!("Successfully unloaded mod: {}", path_str));
                },
                Err(e) => {
                    write_console_line(&format!("Failed to unload mod {}: {:?}", path_str, e));
                    loaded_mods.insert(path_str.to_string(), handle);
                }
            }
        }
    } else {
        write_console_line(&format!("Mod not found in loaded mods: {}", path_str));
    }
}

fn write_console_line(line: &str) {
    unsafe {
        let std_handle = GetStdHandle(STD_OUTPUT_HANDLE).expect("GetStdHandle failed:");
        let message = format!("{}\r\n", line);
        WriteConsoleA(std_handle, message.as_bytes(), None, None ).unwrap()
        
    }
}


fn detach() {
    unsafe {
        MessageBoxA(None,
                    s!("Proton-Inject Loader Detached"),
                    s!("Loader"),
                    Default::default()
        );
    };
}
