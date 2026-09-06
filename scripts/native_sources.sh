#!/usr/bin/env bash

# Source this file from build scripts after resolving the repository root.
load_native_sources() {
    local root="$1" source_path
    C_SOURCES=()
    CXX_SOURCES=()
    while IFS= read -r source_path || [[ -n "$source_path" ]]; do
        source_path="${source_path%$'\r'}"
        [[ -n "$source_path" ]] || continue
        if [[ ! -f "$root/$source_path" ]]; then
            echo "Missing source from native/sources.txt: $source_path" >&2
            return 1
        fi
        case "$source_path" in
            *.c) C_SOURCES+=("$root/$source_path") ;;
            *.cpp) CXX_SOURCES+=("$root/$source_path") ;;
            *) echo "Unsupported source: $source_path" >&2; return 1 ;;
        esac
    done < "$root/native/sources.txt"
}
