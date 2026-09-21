#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-$ROOT/../zig-aarch64-macos-0.16.0/zig}"
OUT="$ROOT/build"
BIN_DIR="$ROOT/proxy"
mkdir -p "$OUT" "$BIN_DIR"

TARGET="x86_64-windows-gnu"

CFLAGS=(
    -target "$TARGET" -O2
    -DWIN_X64 -DREFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    -I "$ROOT/native/include"
    -I "$ROOT/native"
)

CXX_ONLY=(
    -std=c++17 -fno-rtti
)

source "$ROOT/scripts/native_sources.sh"
load_native_sources "$ROOT"

OBJS=()
for s in "${C_SOURCES[@]}"; do
    o="$OUT/$(basename $s).o"
    echo "  CC  $s"
    "$ZIG" cc "${CFLAGS[@]}" -c "$s" -o "$o"
    OBJS+=("$o")
done
for s in "${CXX_SOURCES[@]}"; do
    o="$OUT/$(basename $s).o"
    echo "  CXX $s"
    "$ZIG" c++ "${CFLAGS[@]}" "${CXX_ONLY[@]}" -c "$s" -o "$o"
    OBJS+=("$o")
done

OUT_DLL="$BIN_DIR/Aurora_bare.dll"
echo "  LD  $OUT_DLL"
"$ZIG" c++ -target "$TARGET" -shared \
    -Wl,-e,DllMain \
    -static-libgcc -static-libstdc++ \
    "${OBJS[@]}" \
    -lkernel32 -luser32 -lpsapi -lws2_32 -ladvapi32 \
    -o "$OUT_DLL"

ls -lh "$OUT_DLL"
file "$OUT_DLL"

echo ""
echo "=== PE surface ==="
python3 -c "
import pefile
pe = pefile.PE('$OUT_DLL')
print('TLS   :', 'YES' if hasattr(pe,'DIRECTORY_ENTRY_TLS') else 'no')
print('Entry :', hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint))
print('Exports:', [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols])
print('Imports:')
for e in pe.DIRECTORY_ENTRY_IMPORT: print(' ', e.dll.decode(), len(e.imports))
"
