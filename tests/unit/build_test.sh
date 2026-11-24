# Tests for build.sh. Each one runs it in a throwaway copy of the repo against a stub cl that logs its
# arguments, one per line, and copies $STUB_DLL to the /Fe: path.
_fail() { echo "expected: $1"; echo "actual:   $2"; exit 1; }
_eq() { [ "$1" = "$2" ] || _fail "$1" "$2"; }

_setup() {
    T="$(mktemp -d)"
    trap 'rm -rf "$T"' EXIT
    cp -r build.sh build.cmd build_public.cmd src "$T/"
    export MSVC_WINE_DIR="$T/msvc" CL_LOG="$T/cl.log" STUB_DLL="$T/good.dll" STUB_RC=0
    _make_pe "$STUB_DLL" 014c 2102 DirectInput8Create
    mkdir -p "$MSVC_WINE_DIR/bin/x86"
    cat >"$MSVC_WINE_DIR/bin/x86/cl" <<'CL'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$CL_LOG"
[ "$STUB_RC" -eq 0 ] || exit "$STUB_RC"
for a in "$@"; do
    case "$a" in /Fe:*) [ ! -f "$STUB_DLL" ] || cp "$STUB_DLL" "${a#/Fe:}" ;; esac
done
CL
    chmod +x "$MSVC_WINE_DIR/bin/x86/cl"
}

# _make_pe <file> <machine hex> <characteristics hex> <export name>: a minimal image with one export.
_make_pe() {
    python3 -I - "$@" <<'PY'
import struct, sys
path, machine, chars, export = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4].encode()
wide = machine == 0x8664
rva = 0x1000
data = struct.pack('<IIHHIIIIIII', 0, 0, 0, 0, rva + 52, 1, 1, 1, rva + 40, rva + 44, rva + 48)
data += struct.pack('<IIHH', rva + 0x100, rva + 64, 0, 0) + b'dinput8.dll\0' + export + b'\0'
data = data.ljust(0x200, b'\0')
opt = struct.pack('<HBBIIIII', 0x20b if wide else 0x10b, 14, 0, 0, 0x200, 0, 0, rva)
opt += struct.pack('<Q', 0x10000000) if wide else struct.pack('<II', rva, 0x10000000)
opt += struct.pack('<IIHHHHHHIIIIHH', 0x1000, 0x200, 6, 0, 0, 0, 6, 0, 0, 0x2000, 0x200, 0, 2, 0)
opt += struct.pack('<QQQQ' if wide else '<IIII', 0x100000, 0x1000, 0x100000, 0x1000) + struct.pack('<II', 0, 16)
opt += struct.pack('<II', rva, 0x100) + b'\0' * (15 * 8)
image = b'MZ'.ljust(60, b'\0') + struct.pack('<I', 64)
image += b'PE\0\0' + struct.pack('<HHIIIHH', machine, 1, 0, 0, 0, len(opt), chars) + opt
image += struct.pack('<8sIIIIIIHHI', b'.rdata', 0x200, rva, 0x200, 0x200, 0, 0, 0, 0, 0x40000040)
open(path, 'wb').write(image.ljust(0x200, b'\0') + data)
PY
}

_build() { OUT="$(cd "$T" && ./build.sh "$@" 2>&1)" && RC=0 || RC=$?; }
_assert_failed() {
    [ "$RC" -ne 0 ] || _fail "a non-zero exit" "$RC"
    [[ "$OUT" != *"BUILD OK"* ]] || _fail "no BUILD OK" "$OUT"
    [ ! -e "$T/out/dinput8.dll" ] || _fail "no out/dinput8.dll" "present"
}
_assert_rejects_image() {
    _build
    _assert_failed
    [[ "$OUT" == *"$1"* ]] || _fail "$1" "$OUT"
}
_args() { printf '%s\n' "$@"; }

passes_build_cmd_arguments() {
    _setup; _build
    _eq 0 "$RC"
    _eq "$(_args /nologo /O2 /MT /W3 /LD src/kotorplus.cpp /Fo:out/ /Fe:out/dinput8.dll /link /DLL user32.lib gdi32.lib kernel32.lib)" "$(cat "$CL_LOG")"
    [ -f "$T/out/dinput8.dll" ]
}
follows_build_cmd_changes() {
    _setup
    sed -i 's|/W3|/W4 /DNEW_FLAG|' "$T/build.cmd"
    _build
    _eq 0 "$RC"
    _eq "/W4 /DNEW_FLAG" "$(sed -n '4,5p' "$CL_LOG" | paste -sd' ')"
}
runs_from_any_directory() {
    _setup
    OUT="$(cd / && "$T/build.sh" 2>&1)" && RC=0 || RC=$?
    _eq 0 "$RC"
    [ -f "$T/out/dinput8.dll" ]
}
rejects_build_cmd_without_cl_line() {
    _setup
    sed -i '/^cl /d' "$T/build.cmd"
    _build
    _assert_failed
    _eq 1 "$RC"
    _eq "no cl line in build.cmd" "$OUT"
}
strips_cmd_syntax_from_cl_line() {
    _setup
    sed -i 's/$/\r/' "$T/build.cmd"
    _build
    _eq 0 "$RC"
    _eq kernel32.lib "$(tail -n 1 "$CL_LOG")"
}
strips_cr_when_nothing_follows_the_arguments() {
    _setup
    sed -i 's/ || exit.*//; s/$/\r/' "$T/build.cmd"
    _build
    _eq 0 "$RC"
    _eq kernel32.lib "$(tail -n 1 "$CL_LOG")"
}
rejects_cl_line_without_output() {
    _setup
    sed -i 's| /Fe:[^ ]*||' "$T/build.cmd"
    _build
    _assert_failed
    _eq "no /Fe: output in build.cmd" "$OUT"
}
prints_build_ok_and_exits_0() {
    _setup; _build
    _eq 0 "$RC"
    _eq "BUILD OK" "$OUT"
    _eq '<testsuite name="build.sh" tests="1" failures="0"><testcase classname="build.sh" name="build"/></testsuite>' "$(cat "$T/.build-truth/junit.xml")"
}
compile_failure_is_reported() {
    _setup
    STUB_RC=2
    _build
    _assert_failed
    _eq "compile or link failed" "$OUT"
    grep -q 'failures="1".*<failure message="compile or link failed"/>' "$T/.build-truth/junit.xml"
}
failed_build_leaves_no_stale_dll() {
    _setup; _build
    _eq 0 "$RC"
    STUB_RC=2
    _build
    _assert_failed
}
missing_output_fails() {
    _setup
    rm "$STUB_DLL"
    _assert_rejects_image "out/dinput8.dll was not produced"
}
rejects_unknown_argument() {
    _setup; _build --bogus
    _eq 2 "$RC"
    _assert_failed
    [ ! -e "$CL_LOG" ]
}
build_ok_follows_pe_check() {
    _setup
    echo "not a PE file" >"$STUB_DLL"
    _assert_rejects_image "not a PE32 (i386) image"
}
missing_msvc_wine_message() {
    _setup
    rm -r "$MSVC_WINE_DIR"
    _build
    _assert_failed
    _eq "msvc-wine not found: run tools/setup-msvc-wine.sh" "$OUT"
    [ ! -e "$T/out" ]
}
build_default_dir_under_home() {
    _setup
    mkdir -p "$T/home/opt"
    mv "$MSVC_WINE_DIR" "$T/home/opt/msvc"
    unset MSVC_WINE_DIR
    HOME="$T/home" _build
    _eq 0 "$RC"
    [ -f "$CL_LOG" ]
}
public_passes_build_public_cmd_arguments() {
    _setup; _build --public
    _eq 0 "$RC"
    _eq "$(_args /nologo /O2 /MT /W3 /LD src/kotorplus.cpp /DKP_PUBLIC /Fo:out_public/ /Fe:out_public/dinput8.dll /link /DLL user32.lib gdi32.lib kernel32.lib)" "$(cat "$CL_LOG")"
    [ -f "$T/out_public/dinput8.dll" ]
    [ ! -e "$T/out" ]
}
accepts_valid_dll() {
    _setup; _build
    _eq 0 "$RC"
    cmp "$STUB_DLL" "$T/out/dinput8.dll"
}
rejects_x64_image() {
    _setup
    _make_pe "$STUB_DLL" 8664 2022 DirectInput8Create
    _assert_rejects_image "not a PE32 (i386) image"
}
rejects_non_dll() {
    _setup
    _make_pe "$STUB_DLL" 014c 0102 DirectInput8Create
    _assert_rejects_image "not a DLL"
}
rejects_missing_export() {
    _setup
    _make_pe "$STUB_DLL" 014c 2102 SomethingElse
    _assert_rejects_image "does not export DirectInput8Create"
}
rejects_similar_export_name() {
    _setup
    _make_pe "$STUB_DLL" 014c 2102 DirectInput8CreateEx
    _assert_rejects_image "does not export DirectInput8Create"
}
missing_objdump_fails() {
    _setup
    mkdir "$T/bin"
    for tool in bash cat cp dirname env grep mkdir rm sed; do ln -s "$(command -v $tool)" "$T/bin/$tool"; done
    PATH="$T/bin" _build
    _assert_failed
    _eq "objdump not found: install binutils" "$OUT"
}
