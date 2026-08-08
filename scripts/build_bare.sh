#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-/Users/mac/client/内置进服/zig-aarch64-macos-0.16.0/zig}"
ZL="/Users/mac/client/内置进服/zig-aarch64-macos-0.16.0/lib"
OUT="$ROOT/build"
mkdir -p "$OUT"

TARGET="x86_64-windows-gnu"

CFLAGS=(
    -target "$TARGET" -O2
    -DWIN_X64 -DREFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    -I "$ROOT/native/include"
)

CXX_ONLY=(
    -std=c++17 -fno-rtti
)

CS=(native/ReflectiveLoader.c)
CXXS=(
    native/loader.cpp native/env.cpp native/random_name.cpp
    native/classfile.cpp native/class_edit.cpp
    native/trampolines.cpp native/relay_handler.cpp native/connection_hook.cpp
)

OBJS=()
for s in "${CS[@]}"; do
    o="$OUT/$(basename $s).o"
    echo "  CC  $s"
    "$ZIG" cc "${CFLAGS[@]}" -c "$ROOT/$s" -o "$o"
    OBJS+=("$o")
done
for s in "${CXXS[@]}"; do
    o="$OUT/$(basename $s).o"
    echo "  CXX $s"
    "$ZIG" c++ "${CFLAGS[@]}" "${CXX_ONLY[@]}" -c "$ROOT/$s" -o "$o"
    OBJS+=("$o")
done

OUT_DLL="$OUT/MinecraftProxy_bare.dll"
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
