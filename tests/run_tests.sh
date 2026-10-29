#!/usr/bin/env bash
# Build and run the host/game simulator against the built plugin.
#
# The harness answers D2RLoader's CheckExpectedBytes / InstallInlineHook from a REAL runtime
# image dump of D2R build 93847, so every expected-byte constant in the plugin is validated
# against the actual game code. The dump is Blizzard's code and is NOT in this repository:
# create your own with tools/probe (see docs/UPDATING.md) and pass its path.
#
# Usage: tests/run_tests.sh /path/to/d2r_image_93847.bin
# Needs: MinGW-w64 (to build the harness) and Wine 64-bit (on Linux) or a Windows shell.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DUMP="${1:?usage: tests/run_tests.sh /path/to/d2r_image_93847.bin}"
SDK="${SDK:-$ROOT/external/PluginSDK}"
CXX="${CXX:-x86_64-w64-mingw32-g++}"

"$ROOT/build.sh"
"$CXX" -std=c++20 -O1 -static -Wno-missing-field-initializers -I"$SDK/include" \
	"$ROOT/tests/test_harness.cpp" -o "$ROOT/build/plugin_harness.exe"

if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
	"$ROOT/build/plugin_harness.exe" "$ROOT/build/cow-portal-in-town.dll" "$DUMP"
else
	WINE="${WINE:-$(command -v wine64 || command -v wine || echo /usr/lib/wine/wine64)}"
	WINEDEBUG=-all "$WINE" "$ROOT/build/plugin_harness.exe" "$(winepath -w "$ROOT/build/cow-portal-in-town.dll" 2>/dev/null || echo "Z:$ROOT/build/cow-portal-in-town.dll")" \
		"$(winepath -w "$DUMP" 2>/dev/null || echo "Z:$DUMP")"
fi
