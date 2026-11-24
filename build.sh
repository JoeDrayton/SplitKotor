#!/usr/bin/env bash
# Linux counterpart of build.cmd and build_public.cmd: real MSVC under Wine (tools/setup-msvc-wine.sh).
#   ./build.sh            -> out/dinput8.dll         (build.cmd)
#   ./build.sh --public   -> out_public/dinput8.dll  (build_public.cmd)
# The compiler arguments are read from the .cmd file, so the two builds cannot drift apart.
set -uo pipefail
cd "$(dirname "$0")" || exit 1

case "${1:-}" in
    "") cmd_file=build.cmd ;;
    --public) cmd_file=build_public.cmd ;;
    *) echo "usage: ./build.sh [--public]" >&2; exit 2 ;;
esac

junit=.build-truth/junit.xml
mkdir -p .build-truth
rm -f "$junit"

write_junit() { # write_junit <failure count> <failure element, or empty>
    local testcase='<testcase classname="build.sh" name="build"/>'
    [ -z "$2" ] || testcase="<testcase classname=\"build.sh\" name=\"build\">$2</testcase>"
    printf '<testsuite name="build.sh" tests="1" failures="%s">%s</testsuite>\n' "$1" "$testcase" >"$junit"
}

dll=""
fail() {
    [ -z "$dll" ] || rm -f "$dll"
    echo "$1" >&2
    write_junit 1 "<failure message=\"$1\"/>"
    exit 1
}

cl="${MSVC_WINE_DIR:-$HOME/opt/msvc}/bin/x86/cl"
[ -x "$cl" ] || fail "msvc-wine not found: run tools/setup-msvc-wine.sh"
command -v objdump >/dev/null || fail "objdump not found: install binutils"

cl_line="$(grep -m1 '^cl ' "$cmd_file")" || fail "no cl line in $cmd_file"
cl_line="${cl_line%%||*}"
cl_line="${cl_line//$'\r'/}"
read -ra args <<<"${cl_line//\\//}"
args=("${args[@]:1}")

for arg in "${args[@]}"; do
    case "$arg" in /Fe:*) dll="${arg#/Fe:}" ;; esac
done
[ -n "$dll" ] || fail "no /Fe: output in $cmd_file"

mkdir -p "$(dirname "$dll")"
rm -f "$dll"
"$cl" "${args[@]}" || fail "compile or link failed"
[ -f "$dll" ] || fail "$dll was not produced"

export LC_ALL=C
headers="$(objdump -p "$dll" 2>/dev/null)" && grep -q 'file format pei-i386$' <<<"$headers" \
    || fail "$dll is not a PE32 (i386) image"
grep -qx $'\tDLL' <<<"$headers" || fail "$dll is not a DLL"
exports="$(sed -n '/^\[Ordinal\/Name Pointer\] Table/,/^$/p' <<<"$headers")"
grep -qE '[[:space:]]DirectInput8Create$' <<<"$exports" || fail "$dll does not export DirectInput8Create"

write_junit 0 ""
echo "BUILD OK"
