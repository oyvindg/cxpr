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

declare -A display_by_key
declare -A covered_by_key
checked=0
source_count=0

while IFS= read -r source_file; do
    rel="${source_file#${root_dir}/}"
    obj="${build_path}/CMakeFiles/cxpr.dir/${rel}.o"
    gcno="${obj%.o}.gcno"
    gcda="${obj%.o}.gcda"
    source_count=$((source_count + 1))

    if [ ! -f "${gcno}" ]; then
        echo "error: no gcov notes for ${rel}" >&2
        exit 1
    fi

    if [ ! -f "${gcda}" ]; then
        echo "warning: no gcov data for ${rel}; run ctest --preset coverage first" >&2
    fi

    if ! output="$(cd "${tmp_dir}" && gcov -f -o "${obj}" "${source_file}" 2>&1)"; then
        echo "error: gcov failed for ${rel}" >&2
        echo "${output}" >&2
        exit 1
    fi

    while IFS= read -r line; do
        case "${line}" in
            Function\ \'*\')
                fn="${line#Function \'}"
                fn="${fn%\'}"
                ;;
            "Lines executed:0.00%"*)
                key="${rel}:${fn:-}"
                if [ -n "${fn:-}" ]; then
                    display_by_key["${key}"]="${rel}: uncovered function: ${fn}"
                    checked=$((checked + 1))
                fi
                fn=""
                ;;
            "Lines executed:"*)
                if [ -n "${fn:-}" ]; then
                    key="${rel}:${fn}"
                    display_by_key["${key}"]="${rel}: uncovered function: ${fn}"
                    covered_by_key["${key}"]=1
                    checked=$((checked + 1))
                    fn=""
                fi
                ;;
        esac
    done <<< "${output}"
done < <(find "${root_dir}/src" -name '*.c' | sort)

missing=0
missing_file="${tmp_dir}/missing.txt"
for key in "${!display_by_key[@]}"; do
    if [ -z "${covered_by_key[${key}]:-}" ]; then
        echo "${display_by_key[${key}]}" >> "${missing_file}"
        missing=$((missing + 1))
    fi
done

if [ -f "${missing_file}" ]; then
    sort "${missing_file}"
fi

if [ "${source_count}" -eq 0 ] || [ "${checked}" -eq 0 ]; then
    echo "error: coverage check examined no functions" >&2
    exit 1
fi

if [ "${missing}" -ne 0 ]; then
    echo "error: ${missing} function(s) have 0% coverage" >&2
    exit 1
fi

echo "Function coverage check passed: ${checked} function(s) executed at least once."
