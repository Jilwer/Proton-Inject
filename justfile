set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default: build

help:
    @echo "Proton Inject"
    @echo
    @echo "  just build      Debug build"
    @echo "  just release    Release build"
    @echo "  just run        Build and launch the GUI"
    @echo "  just test-cli   Build and run a CLI smoke test"
    @echo "  just windows    Build Windows PE artifacts only"
    @echo "  just clean      Remove build/"
    @echo
    @echo "Without just:"
    @echo "  cmake --preset debug && cmake --build --preset debug -j\$(nproc)"

build:
    cmake --preset debug
    cmake --build --preset debug -j"$(nproc)"

release:
    cmake --preset release
    cmake --build --preset release -j"$(nproc)"

windows:
    cmake --preset debug
    cmake --build --preset debug --target windows_artifacts -j"$(nproc)"
    ls -lh build/windows/loader.dll build/windows/injector.exe

clean:
    rm -rf build

run *args:
    just build
    ./build/proton-inject {{args}}

test-cli:
    just build
    ./build/proton-inject --profile-list
