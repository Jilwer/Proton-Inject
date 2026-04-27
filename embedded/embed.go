package embedded

import _ "embed"

// built Windows loader DLL; populate via ./scripts/build.sh or ./scripts/release.sh (copies from the loader crate output).
//
//go:embed assets/loader.dll
var LoaderDLL []byte

// built Windows injector EXE; populate via ./scripts/build.sh or ./scripts/release.sh (copies from injector_exe output).
//
//go:embed assets/injector.exe
var InjectorEXE []byte
