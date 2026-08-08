#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-$ROOT/../zig-aarch64-macos-0.16.0/zig}"
OUT="$ROOT/build"

mkdir -p "$OUT"

"$ZIG" cc -target x86_64-windows-gnu -O2 \
    -DWIN_X64 -DREFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN \
    -I "$ROOT/native/include" \
    -shared \
    "$ROOT/mindll/mindll.c" \
    -lkernel32 -luser32 \
    -o "$OUT/mindll.dll"

echo "--- built ---"
ls -lh "$OUT/mindll.dll"
file "$OUT/mindll.dll"
echo ""
echo "--- imports / TLS ---"
python3 -c "
import pefile
pe = pefile.PE('$OUT/mindll.dll')
for e in pe.DIRECTORY_ENTRY_IMPORT:
    print('  IMP', e.dll.decode())
print('  TLS:', 'yes' if hasattr(pe, 'DIRECTORY_ENTRY_TLS') else 'no')
print('  EXPORTS:', [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols])
"
