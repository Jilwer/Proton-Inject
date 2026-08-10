# Proton Inject

DLL injector for Proton games, built with C++ and Qt 6.


<img width="650" height="750" alt="Screenshot_20260809_203348" src="https://github.com/user-attachments/assets/9b714f62-13d1-4640-abaa-6ed678683f7f" />

## Requirements
- Steam and Proton for Steam games
- [umu-run](https://github.com/Open-Wine-Components/umu-launcher) for non-Steam games

## Download
Download the latest release from the [release page](https://github.com/Jilwer/Proton-Inject/releases/latest).

## Architecture & Features

- C++ Qt 6 Widgets GUI that follows the system theme, with a shared CLI injection core
- MinGW injector and loader embedded in the Linux binary
- Steam games run via `proton runinprefix`
- Non-Steam games run via umu-run
- Optional loader DLL with a mods folder under the prefix Documents directory
- Profiles for saving AppIDs and settings
- Portable single binary

## Injection Methods

| Method | Function | Stealth | Compatibility |
|--------|----------|---------|---------------|
| `crt` | CreateRemoteThread | Low | Highest |
| `apc` | QueueUserAPC | Medium | High |
| `nt` | NtCreateThreadEx | High | High |

## Installation
Download a release binary or build from source. Not packaged for distro repos.

## F.A.Q
### Which injection method should I use?
Start with `crt`. It has the best Proton/Wine compatibility. See the table above for a quick comparison.

`apc` and `nt` are also available if needed.

### Where are mods stored?
`<Proton Prefix>/drive_c/users/steamuser/Documents/proton-inject-mods`

### Do I need to install it?
No. It is fully portable, just run the binary.

## Building

Requirements:

- CMake 3.25+ and Ninja
- C++23 compiler (GCC 13+ or Clang 17+)
- Qt 6.2+ Widgets (`qt6-base-dev` on Debian/Ubuntu)
- MinGW-w64 (`x86_64-w64-mingw32-g++`) and `objcopy`

```bash
cmake --preset release
cmake --build --preset release -j"$(nproc)"
```

Output: `build/proton-inject`

Optional: `just build`, `just release`, `just run`

## Third-party

Vendored under `third_party/`:

- [CLI11](https://github.com/CLIUtils/CLI11) 2.4.2 (BSD-3-Clause) for CLI argument parsing
- [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 (MIT) for profile config serialization

## Thanks
- [proton-injector](https://github.com/jokelbaf/proton-injector) (MIT) for reference material on apc and nt injection methods

## Contributing
Feel free to submit pull requests at will, given the time I will review them and merge them when the time becomes available

## AI Contributions
This codebase is NOT anti-llm, however, please do not submit any monolithic AI generated pull requests that are unreasonable to review as they will be ignored.
Treat any committed code as your own, review it, and make sure you have an understanding of its functionality before submitting any pull requests.

## License

This code is licensed under the GNU GPL v3. Please see the [LICENSE](LICENSE) file for more details.

---

<sub><b>AI Disclosure:</b> This project was originally an amalgamation of handwritten Go (Fyne GUI) and Rust. After a personal decision that this stack was unintuitive, overengineered, and extremely bloated (20MB binary vs 3MB binary), Claude Opus 5 was used for the initial port to C++ 23 and Qt 6. Afterwards, a thorough manual review was carried out, and code style and quality standards were enforced.</sub>
