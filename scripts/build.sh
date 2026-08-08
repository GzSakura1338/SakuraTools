#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-$ROOT/../zig-aarch64-macos-0.16.0/zig}"
OUT="$ROOT/build"
NATIVE="$ROOT/native"

if [[ ! -x "$ZIG" ]]; then
    echo "zig not found at $ZIG" >&2
    exit 1
fi

mkdir -p "$OUT"

TARGET="x86_64-windows-gnu"

CFLAGS=(
    -target "$TARGET"
    -O2
    -fno-stack-protector
    -fno-sanitize=undefined
    -DWIN_X64
    -DREFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    -I "$NATIVE/include"
    -I "$NATIVE"
)

C_SOURCES=(
    "$NATIVE/ReflectiveLoader.c"
)

CXX_SOURCES=(
    "$NATIVE/loader.cpp"
    "$NATIVE/env.cpp"
    "$NATIVE/random_name.cpp"
    "$NATIVE/classfile.cpp"
    "$NATIVE/class_edit.cpp"
    "$NATIVE/trampolines.cpp"
    "$NATIVE/relay_handler.cpp"
    "$NATIVE/connection_hook.cpp"
    "$NATIVE/world_cache.cpp"
    "$NATIVE/b_server.cpp"
)

OBJS=()
for src in "${C_SOURCES[@]}"; do
    [[ -f "$src" ]] || continue
    obj="$OUT/$(basename "$src").o"
    echo "CC  $(basename "$src")"
    "$ZIG" cc "${CFLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

for src in "${CXX_SOURCES[@]}"; do
    [[ -f "$src" ]] || continue
    obj="$OUT/$(basename "$src").o"
    echo "CXX $(basename "$src")"
    "$ZIG" c++ "${CFLAGS[@]}" -std=c++17 -c "$src" -o "$obj"
    OBJS+=("$obj")
done

OUT_DLL="$OUT/MinecraftProxy.dll"
echo "LD  $(basename "$OUT_DLL")"
"$ZIG" c++ -target "$TARGET" -shared \
    -static-libgcc -static-libstdc++ \
    "${OBJS[@]}" \
    -lpsapi -lws2_32 -lkernel32 -luser32 -ladvapi32 \
    -o "$OUT_DLL"

ls -lh "$OUT_DLL"
echo "OK  $OUT_DLL"

INJECTOR_DIR="$ROOT/injector"
INJECTOR_SOURCES=(
    "$INJECTOR_DIR/cli.c"
    "$INJECTOR_DIR/Inject.c"
    "$INJECTOR_DIR/LoadLibraryR.c"
    "$INJECTOR_DIR/GetProcAddressR.c"
)
INJ_OUT="$OUT/injector.exe"
echo "CC/LD $(basename "$INJ_OUT")"
"$ZIG" cc -target "$TARGET" -O2 \
    -DWIN_X64 -DWIN32_LEAN_AND_MEAN -DREFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN \
    -I "$INJECTOR_DIR" \
    "${INJECTOR_SOURCES[@]}" \
    -ladvapi32 -lkernel32 -luser32 \
    -o "$INJ_OUT"
ls -lh "$INJ_OUT"
echo "OK  $INJ_OUT"
