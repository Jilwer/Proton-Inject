package embedded

import _ "embed"

// built Windows loader DLL; populate via ./build.sh or ./release.sh (copies from the loader crate output).
//
//go:embed assets/loader.dll
var LoaderDLL []byte

// built Windows injector EXE; populate via ./build.sh or ./release.sh (copies from injector_exe output).
//
//go:embed assets/injector.exe
var InjectorEXE []byte
