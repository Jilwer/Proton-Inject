use std::ffi::c_void;
use std::mem;
use std::path::{Path, PathBuf};
use std::process;

use windows::core::{PCSTR, PCWSTR};
use windows::Win32::Foundation::{CloseHandle, HANDLE, LUID, MAX_PATH};
use windows::Win32::Security::{
    AdjustTokenPrivileges, LookupPrivilegeValueW, SE_DEBUG_NAME, SE_PRIVILEGE_ENABLED,
    TOKEN_ADJUST_PRIVILEGES, TOKEN_PRIVILEGES, TOKEN_QUERY,
};
use windows::Win32::System::Diagnostics::Debug::WriteProcessMemory;
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows::Win32::System::LibraryLoader::{GetModuleHandleA, GetProcAddress};
use windows::Win32::System::Memory::{
    MEM_COMMIT, MEM_RELEASE, PAGE_READWRITE, VirtualAllocEx, VirtualFreeEx,
};
use windows::Win32::System::ProcessStatus::{EnumProcessModules, GetModuleFileNameExW};
use windows::Win32::System::Threading::{
    CreateRemoteThread, GetCurrentProcess, OpenProcess, OpenProcessToken, WaitForSingleObject,
    INFINITE, PROCESS_CREATE_THREAD, PROCESS_QUERY_INFORMATION, PROCESS_VM_OPERATION,
    PROCESS_VM_READ, PROCESS_VM_WRITE,
};

struct Args {
    process_name: String,
    dll_path: String,
}

fn parse_args() -> Args {
    let args: Vec<String> = std::env::args().collect();
    let mut process_name: Option<String> = None;
    let mut dll_path: Option<String> = None;
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-n" | "--process-name" => {
                i += 1;
                if i < args.len() {
                    process_name = Some(args[i].clone());
                }
            }
            "-i" | "--inject" => {
                i += 1;
                if i < args.len() {
                    dll_path = Some(args[i].clone());
                }
            }
            _ => {}
        }
        i += 1;
    }
    let process_name = process_name.unwrap_or_else(|| {
        eprintln!("Error: -n <process-name> is required");
        process::exit(1);
    });
    let dll_path = dll_path.unwrap_or_else(|| {
        eprintln!("Error: -i <dll-path> is required");
        process::exit(1);
    });
    Args { process_name, dll_path }
}

fn main() {
    let args = parse_args();

    let pid = match get_process_id_by_name(&args.process_name) {
        Ok(pid) => pid,
        Err(e) => {
            eprintln!("Error finding process '{}': {}", args.process_name, e);
            process::exit(2);
        }
    };

    // best-effort; often required on Windows, harmless if it fails under Wine/Proton
    let _ = enable_se_debug_privilege();

    let path = match resolve_module_path(&args.dll_path) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("Error resolving module '{}': {}", args.dll_path, e);
            process::exit(2);
        }
    };

    if let Err(e) = inject_dll(pid, &path) {
        eprintln!("Injection failed: {}", e);
        process::exit(2);
    }
}

fn get_process_id_by_name(name: &str) -> Result<u32, String> {
    unsafe {
        let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
            .map_err(|e| format!("CreateToolhelp32Snapshot: {}", e))?;

        let mut entry = PROCESSENTRY32W {
            dwSize: mem::size_of::<PROCESSENTRY32W>() as u32,
            ..Default::default()
        };

        let name_lower = name.to_lowercase();

        if Process32FirstW(snap, &mut entry).is_ok() {
            loop {
                let exe_name = String::from_utf16_lossy(
                    &entry.szExeFile[..entry
                        .szExeFile
                        .iter()
                        .position(|&c| c == 0)
                        .unwrap_or(entry.szExeFile.len())],
                );
                if exe_name.to_lowercase() == name_lower {
                    let pid = entry.th32ProcessID;
                    let _ = CloseHandle(snap);
                    return Ok(pid);
                }
                if Process32NextW(snap, &mut entry).is_err() {
                    break;
                }
            }
        }

        let _ = CloseHandle(snap);
        Err(format!("Could not find process '{}'", name))
    }
}

fn enable_se_debug_privilege() -> Result<(), String> {
    unsafe {
        let mut token = HANDLE::default();
        OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &mut token,
        )
        .map_err(|e| format!("OpenProcessToken: {}", e))?;

        let mut luid = LUID::default();
        LookupPrivilegeValueW(PCWSTR::null(), SE_DEBUG_NAME, &mut luid)
            .map_err(|e| format!("LookupPrivilegeValue: {}", e))?;

        let mut tp = TOKEN_PRIVILEGES {
            PrivilegeCount: 1,
            ..Default::default()
        };
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, false, Some(&tp), 0, None, None)
            .map_err(|e| format!("AdjustTokenPrivileges: {}", e))?;

        let _ = CloseHandle(token);
        Ok(())
    }
}

fn resolve_module_path(module: &str) -> Result<PathBuf, String> {
    let candidate = PathBuf::from(module);
    if let Ok(canonical) = std::fs::canonicalize(&candidate) {
        if canonical.exists() {
            return Ok(canonical);
        }
    }
    if candidate.is_relative() {
        if let Ok(exe_path) = std::env::current_exe() {
            if let Some(exe_dir) = exe_path.parent() {
                let beside_exe = exe_dir.join(module);
                if let Ok(canonical) = std::fs::canonicalize(&beside_exe) {
                    if canonical.exists() {
                        return Ok(canonical);
                    }
                }
            }
        }
    }
    Ok(std::fs::canonicalize(&candidate).unwrap_or_else(|_| PathBuf::from(module)))
}

fn inject_dll(pid: u32, dll_path: &Path) -> Result<(), String> {
    unsafe {
        let process = OpenProcess(
            PROCESS_QUERY_INFORMATION
                | PROCESS_VM_READ
                | PROCESS_CREATE_THREAD
                | PROCESS_VM_OPERATION
                | PROCESS_VM_WRITE,
            false,
            pid,
        )
        .map_err(|e| format!("OpenProcess({}): {}", pid, e))?;

        let wide_path: Vec<u16> = dll_path
            .as_os_str()
            .to_string_lossy()
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
        let size = wide_path.len() * mem::size_of::<u16>();

        let remote_mem = VirtualAllocEx(process, None, size, MEM_COMMIT, PAGE_READWRITE);
        if remote_mem.is_null() {
            let _ = CloseHandle(process);
            return Err("VirtualAllocEx failed".into());
        }

        if WriteProcessMemory(
            process,
            remote_mem,
            wide_path.as_ptr() as *const c_void,
            size,
            None,
        )
        .is_err()
        {
            VirtualFreeEx(process, remote_mem, 0, MEM_RELEASE).ok();
            let _ = CloseHandle(process);
            return Err("WriteProcessMemory failed".into());
        }

        let kernel32 = GetModuleHandleA(PCSTR(b"kernel32.dll\0".as_ptr()))
            .map_err(|e| format!("GetModuleHandleA(kernel32): {}", e))?;

        let load_library_addr = GetProcAddress(kernel32, PCSTR(b"LoadLibraryW\0".as_ptr()))
            .ok_or("Could not find LoadLibraryW in kernel32")?;

        let thread_start: unsafe extern "system" fn(*mut c_void) -> u32 =
            mem::transmute(load_library_addr);

        let thread = CreateRemoteThread(
            process,
            None,
            0,
            Some(thread_start),
            Some(remote_mem),
            0,
            None,
        )
        .map_err(|e| format!("CreateRemoteThread: {}", e))?;

        let _ = WaitForSingleObject(thread, INFINITE);

        if get_module_base_address(process, dll_path).is_none() {
            let _ = CloseHandle(thread);
            VirtualFreeEx(process, remote_mem, 0, MEM_RELEASE).ok();
            let _ = CloseHandle(process);
            return Err("LoadLibraryW in remote process did not load the module".into());
        }

        let _ = CloseHandle(thread);
        VirtualFreeEx(process, remote_mem, 0, MEM_RELEASE).ok();
        let _ = CloseHandle(process);

        Ok(())
    }
}

fn get_module_base_address(process: HANDLE, dll_path: &Path) -> Option<*mut u8> {
    unsafe {
        let file_name_lower = dll_path
            .file_name()?
            .to_string_lossy()
            .to_lowercase();

        let mut modules = vec![windows::Win32::Foundation::HMODULE::default(); 1024];
        let mut needed: u32 = 0;
        if EnumProcessModules(
            process,
            modules.as_mut_ptr(),
            (modules.len() * mem::size_of::<windows::Win32::Foundation::HMODULE>()) as u32,
            &mut needed,
        )
        .is_err()
        {
            return None;
        }

        let count = needed as usize / mem::size_of::<windows::Win32::Foundation::HMODULE>();
        modules.truncate(count);

        for module in &modules {
            let mut name_buf = [0u16; MAX_PATH as usize];
            let len = GetModuleFileNameExW(Some(process), Some(*module), &mut name_buf);
            if len == 0 {
                continue;
            }
            let full_path = String::from_utf16_lossy(&name_buf[..len as usize]);

            if let Some(fname) = Path::new(&full_path).file_name() {
                if fname.to_string_lossy().to_lowercase() == file_name_lower {
                    return Some(module.0 as *mut u8);
                }
            }
            if full_path.to_lowercase().replace('/', "\\")
                == dll_path.to_string_lossy().to_lowercase().replace('/', "\\")
            {
                return Some(module.0 as *mut u8);
            }
        }

        None
    }
}
