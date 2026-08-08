#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-$ROOT/../zig-aarch64-macos-0.16.0/zig}"
BUILD="$ROOT/build/tests"
CLASSPATH_OUT="$BUILD/classpath"
GEN_OUT="$BUILD/gen"

mkdir -p "$BUILD" "$CLASSPATH_OUT" "$GEN_OUT"

echo "==[ pure C++ round-trip ]=="
"$ZIG" c++ -std=c++17 -O0 -g \
    "$ROOT/native/classfile.cpp" \
    "$ROOT/native/class_edit.cpp" \
    "$ROOT/tests/edit_self_test.cpp" \
    -o "$BUILD/edit_self_test"
"$BUILD/edit_self_test"

echo "==[ compile Java stubs ]=="
javac -d "$CLASSPATH_OUT" \
    "$ROOT/tests/java/ChannelDuplexHandler.java" \
    "$ROOT/tests/java/Connection.java"
javac -d "$BUILD" "$ROOT/tests/java/Verify.java"

FAKE_CONN_CLASS="$CLASSPATH_OUT/net/minecraft/network/Connection.class"
if [[ ! -f "$FAKE_CONN_CLASS" ]]; then
    echo "expected $FAKE_CONN_CLASS after javac" >&2; exit 2
fi

echo "==[ emit_samples ]=="
"$ZIG" c++ -std=c++17 -O0 -g \
    "$ROOT/native/classfile.cpp" \
    "$ROOT/native/class_edit.cpp" \
    "$ROOT/tests/emit_samples.cpp" \
    -o "$BUILD/emit_samples"
"$BUILD/emit_samples" "$GEN_OUT" "$FAKE_CONN_CLASS"

echo "==[ JVM verify ]=="
java -Xverify:all \
     -cp "$BUILD:$CLASSPATH_OUT:$GEN_OUT" \
     Verify \
     HookBridge         "$GEN_OUT/HookBridge.class" \
     RelayHandler       "$GEN_OUT/RelayHandler.class" \
     PatchedConnection  "$GEN_OUT/PatchedConnection.class"

echo "==[ all tests passed ]=="
