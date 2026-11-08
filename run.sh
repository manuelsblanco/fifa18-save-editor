#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$project_dir/build/fifa18-save-editor" ]; then
    exec "$project_dir/build/fifa18-save-editor" "$@"
fi
if [ -x "$project_dir/.local-build/fifa18-save-editor" ]; then
    exec "$project_dir/.local-build/fifa18-save-editor" "$@"
fi
printf '%s\n' 'Build first: cmake -S . -B build && cmake --build build --parallel' >&2
exit 1
