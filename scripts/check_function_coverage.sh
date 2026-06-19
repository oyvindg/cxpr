#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-coverage}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${build_dir}" in
    /*) build_path="${build_dir}" ;;
    *) build_path="${root_dir}/${build_dir}" ;;
esac

if ! command -v gcov >/dev/null 2>&1; then
    echo "error: gcov not found; install GCC/gcov or use a GCC-compatible toolchain" >&2
    exit 2
fi

if [ ! -d "${build_path}" ]; then
    echo "error: coverage build directory not found: ${build_dir}" >&2
    echo "next step: cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage" >&2
    exit 2
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cxpr-gcov.XXXXXX")"
trap 'rm -rf "${tmp_dir}"' EXIT

declare -A first_file_by_fn
declare -A covered_by_fn
checked=0

while IFS= read -r source_file; do
    rel="${source_file#${root_dir}/}"
    obj="${build_path}/CMakeFiles/cxpr.dir/${rel}.o"
    gcno="${obj%.o}.gcno"
    gcda="${obj%.o}.gcda"

    if [ ! -f "${gcno}" ]; then
        echo "warning: no gcov notes for ${rel}" >&2
        continue
    fi

    if [ ! -f "${gcda}" ]; then
        echo "warning: no gcov data for ${rel}; run ctest --preset coverage first" >&2
    fi

    output="$(
        cd "${tmp_dir}"
        gcov -f -o "${obj}" "${source_file}" 2>/dev/null || true
    )"

    while IFS= read -r line; do
        case "${line}" in
            Function\ \'*\')
                fn="${line#Function \'}"
                fn="${fn%\'}"
                ;;
            "Lines executed:0.00%"*)
                if [ -n "${fn:-}" ] && [ -z "${first_file_by_fn[${fn}]:-}" ]; then
                    first_file_by_fn["${fn}"]="${rel}"
                fi
                fn=""
                ;;
            "Lines executed:"*)
                if [ -n "${fn:-}" ]; then
                    first_file_by_fn["${fn}"]="${first_file_by_fn[${fn}]:-${rel}}"
                    covered_by_fn["${fn}"]=1
                    checked=$((checked + 1))
                    fn=""
                fi
                ;;
        esac
    done <<< "${output}"
done < <(find "${root_dir}/src" -name '*.c' | sort)

missing=0
missing_file="${tmp_dir}/missing.txt"
for fn in "${!first_file_by_fn[@]}"; do
    if [ -z "${covered_by_fn[${fn}]:-}" ]; then
        echo "${first_file_by_fn[${fn}]}: uncovered function: ${fn}" >> "${missing_file}"
        missing=$((missing + 1))
    fi
done

if [ -f "${missing_file}" ]; then
    sort "${missing_file}"
fi

if [ "${missing}" -ne 0 ]; then
    echo "error: ${missing} function(s) have 0% coverage" >&2
    exit 1
fi

echo "Function coverage check passed: ${checked} function(s) executed at least once."
