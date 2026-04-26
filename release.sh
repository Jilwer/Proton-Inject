#!/usr/bin/env bash
set -euo pipefail

# release: Rust and Go with optimizations (strip binary, trimpath).

APP_NAME="proton-inject"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"
VERSION=$(grep -E '^\s*(var|const) version' main.go | head -1 | cut -d'"' -f2)
OUT_DIR="build"
LOADER_DIR="embedded/loader"
INJECTOR_DIR="embedded/injector/injector_exe"
EMBED_DIR="embedded/assets"
CARGO_PROFILE="release"

mkdir -p "$OUT_DIR" "$EMBED_DIR"

echo "Building loader (release) ..."
(cd "$LOADER_DIR" && cargo build --release --target x86_64-pc-windows-gnu)
LOADER_DLL="$REPO_ROOT/target/x86_64-pc-windows-gnu/$CARGO_PROFILE/loader.dll"
if [[ ! -f "$LOADER_DLL" ]]; then
	echo "Error: loader build did not produce $LOADER_DLL" >&2
	exit 1
fi
cp "$LOADER_DLL" "$OUT_DIR/"
cp "$LOADER_DLL" "$EMBED_DIR/"
echo "Built: $OUT_DIR/loader.dll ($(du -h "$OUT_DIR/loader.dll" | cut -f1))"

echo "Building injector (release) ..."
(cd "$INJECTOR_DIR" && cargo build --release --target x86_64-pc-windows-gnu)
INJECTOR_EXE="$REPO_ROOT/target/x86_64-pc-windows-gnu/$CARGO_PROFILE/injector.exe"
if [[ ! -f "$INJECTOR_EXE" ]]; then
	echo "Error: injector build did not produce $INJECTOR_EXE" >&2
	exit 1
fi
cp "$INJECTOR_EXE" "$OUT_DIR/"
cp "$INJECTOR_EXE" "$EMBED_DIR/"
echo "Built: $OUT_DIR/injector.exe ($(du -h "$OUT_DIR/injector.exe" | cut -f1))"

echo "Building $APP_NAME v$VERSION (release) ..."
go build -trimpath -ldflags="-s -w -X main.version=${VERSION}" -o "$OUT_DIR/$APP_NAME" .

echo "Built: $OUT_DIR/$APP_NAME ($(du -h "$OUT_DIR/$APP_NAME" | cut -f1))"
