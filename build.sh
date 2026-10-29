#!/usr/bin/env bash
# Build cow-portal-in-town.dll (and optionally the diagnostic probe) with MinGW-w64.
#
# Works on Linux (cross-compile) and on Windows inside MSYS2 (UCRT64/MINGW64 shell).
# The result is fully static: the DLL imports only KERNEL32.dll and msvcrt.dll.
#
# Usage:
#   ./build.sh            # plugin only            -> build/cow-portal-in-town.dll
#   ./build.sh probe      # plugin + probe         -> build/cowportal-probe.dll
#   ./build.sh package    # plugin + release zips  -> dist/
#
# Requirements: x86_64-w64-mingw32-g++ / -windres / -strip (GCC >= 13, C++20),
#               the PluginSDK submodule (git submodule update --init).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${SDK:-$ROOT/external/PluginSDK}"
OUT="$ROOT/build"
PREFIX="${MINGW_PREFIX_TRIPLE:-x86_64-w64-mingw32}"
CXX="${CXX:-$PREFIX-g++}"
WINDRES="${WINDRES:-$PREFIX-windres}"
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -shared -static)
# Reproducible output: fixed image base (GNU ld would otherwise derive it from the output path)
# and no link timestamp; symbols are stripped at link time (a separate strip would add a
# timestamp again). The DLL is relocatable either way.
LDFLAGS=(-s -Wl,--disable-auto-image-base -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp)

if [[ ! -f "$SDK/include/D2RLPlugin/api.h" ]]; then
	echo "PluginSDK not found at $SDK - run: git submodule update --init" >&2
	exit 1
fi
mkdir -p "$OUT"

build_plugin() {
	# windres runs inside src/ so the .rc can embed cow_portal_in_town.toml (resource 1002).
	(cd "$ROOT/src" && "$WINDRES" -I"$SDK/include" cow_portal_in_town.rc -O coff -o "$OUT/cow_portal_in_town.res.o")
	"$CXX" "${CXXFLAGS[@]}" -I"$SDK/include" "$ROOT/src/cow_portal_in_town.cpp" "$OUT/cow_portal_in_town.res.o" \
		"${LDFLAGS[@]}" -o "$OUT/cow-portal-in-town.dll"
	echo "built $OUT/cow-portal-in-town.dll"
}

build_probe() {
	"$WINDRES" -I"$SDK/include" "$ROOT/tools/probe/cowportal_probe.rc" -O coff -o "$OUT/cowportal_probe.res.o"
	"$CXX" "${CXXFLAGS[@]}" -I"$SDK/include" "$ROOT/tools/probe/cowportal_probe.cpp" "$OUT/cowportal_probe.res.o" \
		"${LDFLAGS[@]}" -o "$OUT/cowportal-probe.dll"
	echo "built $OUT/cowportal-probe.dll"
}

package() {
	local version
	version="$(sed -n 's/.*kPluginVersion *= *"\([^"]*\)".*/\1/p' "$ROOT/src/cow_portal_in_town.cpp")"
	python3 "$ROOT/tools/package.py" "$version" "$OUT/cow-portal-in-town.dll" "$ROOT/src/cow_portal_in_town.toml" \
		"$ROOT/docs/RELEASE_README.md" "$ROOT/dist"
}

build_plugin
case "${1:-}" in
	probe)   build_probe ;;
	package) package ;;
	"")      ;;
	*)       echo "unknown target: $1" >&2; exit 2 ;;
esac
