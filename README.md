# Proton Inject
Comprehensive DLL injector for proton games, made with Go, Rust, and Fyne with a clean GUI, profile system, and mod loader.

<p align="center">
  <img width="800" height="776" alt="Screenshot_20260426_181132" src="https://github.com/user-attachments/assets/f610966b-23a6-4c3c-9985-39847ab490ef" />
</p>


## Download

Download the latest release from the [release page](https://github.com/Jilwer/Proton-Inject/releases/latest).

## Architecture & Features

- This program is primarily written in Go and Rust and uses the Fyne GUI library.
- Both the injector and loader are embedded directly into the main program using Go's `embed.FS`.
- The GUI wraps an embedded CRT+LL injector written in Rust, which is used to inject our DLLs.
- We have a frontend wrapper for the injector, `injector_wrap.go`, that is built directly into the main program. This uses `protontricks-launch` to run the injector in the correct Proton context, ensuring DLL injection into the correct target process.
- The tool ships with an optional loader DLL that the injector can use. This creates a `proton-inject-mods` folder under the target Proton/Wine prefix's `Documents` directory, with GUI helpers to quickly access this folder.
- A profiles system lets you save game profiles so you do not need to remember details such as the Steam AppID.
- It ships as one fully portable binary, installation not required, but can be achieved manually
- Other injection methods are not supported at the moment because CRT+LL on linux at the present is sufficiently stealthy, and other methods have poor compatibility through wine/proton


## Installation
Either download the compiled binary from the release page, or build yourself. I will not be maintaining this on any package repo's, and system installation lies upon the user if it is wanted.

## F.A.Q
### Why CRT+LL only?
CRT+LL (CreateRemoteThread + LoadLibrary) is currently the most reliable method under Proton/Wine. Other injection methods tend to break due to Wine translation layers, and have poor compatibility across games. In my experience CRT+LL is sufficiently stealthy, and I have not run across any detections thus far, more injection methods may be added if the story changes at a later date.

### Where are mods stored?
`<Proton Prefix>/drive_c/users/steamuser/Documents/proton-inject-mods`

### Do I need to install it?
No. It’s fully portable—just run the binary.

## Building

Requirements:

- A C compiler (Fyne uses cgo)
- Go
- Rust
- Make

Run `make build` or `make release`.


# Contributing
Feel free to submit pull requests at will, given the time I will review them and merge them when the time becomes available


# License

This code is licensed under the GNU GPL v3. Please see the [LICENSE](LICENSE) file for more details.
