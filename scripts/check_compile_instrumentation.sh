#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:?usage: check_compile_instrumentation.sh BUILD_DIR REQUIRED_FLAG}"
required_flag="${2:?usage: check_compile_instrumentation.sh BUILD_DIR REQUIRED_FLAG}"
compile_commands="${build_dir}/compile_commands.json"

if [ ! -f "${compile_commands}" ]; then
    echo "error: missing ${compile_commands}" >&2
    exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "error: jq is required to inspect compile_commands.json" >&2
    exit 2
fi

missing="$(jq -r --arg flag "${required_flag}" '
    .[] | select(.file | test("/src/.*\\.c$")) |
    select(((.command // (.arguments | join(" "))) | contains($flag)) | not) |
    .file
' "${compile_commands}")"

if [ -n "${missing}" ]; then
    echo "error: cxpr sources missing compile flag ${required_flag}:" >&2
    echo "${missing}" >&2
    exit 1
fi

count="$(jq '[.[] | select(.file | test("/src/.*\\.c$"))] | length' "${compile_commands}")"
if [ "${count}" -eq 0 ]; then
    echo "error: compile_commands.json contains no src/ compilation units" >&2
    exit 1
fi
echo "Compile instrumentation check passed: ${count} source unit(s) use ${required_flag}."
