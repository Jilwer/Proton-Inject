# Proton Inject

## Architecture & Features
- This program is primarily made using Go and Rust with the Fyne GUI library. 
- Both the Injector and Loader are embedded directly into the main program using Go's EmbedFS
- The GUI wraps a embedded CRT+LL injector that is built in rust to inject our DLLs.
- We have a frontend wrapper for our injector `injector_wrap.go` that is built directly into the main program, this utilizes `protontricks-launch` to run the injector in the correct proton context to achieve dll injection into the correct target process
- It ships with a fully optional loader dll that the injector may utilize. This creates a `proton-inject-go` mods folder under the target proton/wine prefix's Documents folder, with GUI helpers to access this directory
- Profiles system so you can save your game profiles without needing to recall game details like Steam AppID
## Building
Requirements
- A C compiler (fyne uses cgo)
- Go
- Rust
- Make
`make build` or `make release`

This code is licensed under the GNU GPL v3. Please see the [LICENSE](LICENSE) file for more details
