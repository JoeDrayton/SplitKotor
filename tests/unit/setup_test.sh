# Tests for tools/setup-msvc-wine.sh. Nothing is downloaded: git and python3 are stubs on PATH, and the stub
# clone carries a stub install.sh.
_fail() { echo "expected: $1"; echo "actual:   $2"; exit 1; }
_eq() { [ "$1" = "$2" ] || _fail "$1" "$2"; }

_setup() {
    T="$(mktemp -d)"
    trap 'rm -rf "$T"' EXIT
    SCRIPT="$PWD/tools/setup-msvc-wine.sh"
    export LOG="$T/log" DOWNLOAD_RC=0 INSTALL_RC=0 MSVC_WINE_DIR="$T/msvc"
    : >"$LOG"
    mkdir "$T/bin"
    for tool in bash cat chmod dirname env find grep head mkdir mktemp rm touch; do ln -s "$(command -v $tool)" "$T/bin/$tool"; done
    for tool in wine msiextract unzip; do printf '#!/usr/bin/env bash\n' >"$T/bin/$tool"; done
    cat >"$T/bin/git" <<'GIT'
#!/usr/bin/env bash
echo "git $*" >>"$LOG"
[ "$1" = clone ] || exit 0
dir="${!#}"
mkdir -p "$dir"
cat >"$dir/install.sh" <<'INSTALL'
#!/usr/bin/env bash
echo "install $*" >>"$LOG"
[ "$INSTALL_RC" -eq 0 ] || exit "$INSTALL_RC"
mkdir -p "$1/bin/x86"
printf '#!/usr/bin/env bash\n' >"$1/bin/x86/cl"
chmod +x "$1/bin/x86/cl"
INSTALL
chmod +x "$dir/install.sh"
GIT
    cat >"$T/bin/python3" <<'PY'
#!/usr/bin/env bash
echo "python3 $*" >>"$LOG"
[ "$DOWNLOAD_RC" -eq 0 ] || exit "$DOWNLOAD_RC"
dest="${!#}"
mkdir -p "$dest/VC/Tools/MSVC/14.0/bin/Hostx64/x86" "$dest/Windows Kits/10/Include/10.0/um" "$dest/Windows Kits/10/Lib/10.0/um/x86"
touch "$dest/VC/Tools/MSVC/14.0/bin/Hostx64/x86/cl.exe" "$dest/Windows Kits/10/Include/10.0/um/windows.h" "$dest/Windows Kits/10/Lib/10.0/um/x86/user32.lib"
PY
    chmod +x "$T/bin/"{wine,msiextract,unzip,git,python3}
}
_run() { OUT="$(PATH="$T/bin" "$SCRIPT" 2>&1)" && RC=0 || RC=$?; }
_installs() { grep -c '^install ' "$LOG" || true; }

installs_x86_pinned_vs2022() {
    _setup; _run
    _eq 0 "$RC"
    grep -q '^git .*checkout.* 5dd115047b7ddcbb53cbc8b5b14163a20f4334fe$' "$LOG"
    grep -q "^python3 .*/vsdownload.py --accept-license --major 17 --architecture x86 --dest $T/msvc\$" "$LOG"
    grep -q "^install $T/msvc\$" "$LOG"
    [ -e "$T/msvc/.setup-complete" ]
}
second_run_changes_nothing() {
    _setup; _run
    _eq 0 "$RC"
    before="$(find "$T/msvc" -printf '%p %T@ %s\n' | sort)"
    : >"$LOG"
    _run
    _eq 0 "$RC"
    _eq "" "$(cat "$LOG")"
    _eq "$before" "$(find "$T/msvc" -printf '%p %T@ %s\n' | sort)"
}
names_missing_prerequisite() {
    for tool in wine python3 git msiextract unzip; do
        (
            _setup
            rm "$T/bin/$tool"
            _run
            _eq 1 "$RC"
            _eq "missing prerequisite: $tool" "$OUT"
            [ ! -e "$T/msvc" ]
        )
    done
}
failed_install_leaves_no_marker() {
    for step in DOWNLOAD_RC INSTALL_RC; do
        (
            _setup
            export "$step=3"
            _run
            _eq 3 "$RC"
            [ ! -e "$T/msvc/.setup-complete" ]
        )
    done
}
incomplete_install_is_reinstalled() {
    for gone in bin/x86/cl VC/Tools "Windows Kits/10/Include" "Windows Kits/10/Lib" .setup-complete; do
        (
            _setup; _run
            rm -r "${T:?}/msvc/$gone"
            _run
            _eq 0 "$RC"
            _eq 2 "$(_installs)"
            [ -e "$T/msvc/$gone" ]
        )
    done
}
failed_reinstall_removes_old_marker() {
    _setup; _run
    rm "$T/msvc/bin/x86/cl"
    INSTALL_RC=3 _run
    _eq 3 "$RC"
    [ ! -e "$T/msvc/.setup-complete" ]
}
incomplete_result_is_an_error() {
    _setup
    sed -i 's| "$dest/Windows Kits/10/Lib/10.0/um/x86/user32.lib"||' "$T/bin/python3"
    _run
    _eq 1 "$RC"
    [ ! -e "$T/msvc/.setup-complete" ]
}
default_dir_under_home() {
    _setup
    unset MSVC_WINE_DIR
    HOME="$T/home" _run
    _eq 0 "$RC"
    [ -e "$T/home/opt/msvc/.setup-complete" ]
}
dir_with_space() {
    _setup
    MSVC_WINE_DIR="$T/a dir" _run
    _eq 0 "$RC"
    [ -e "$T/a dir/.setup-complete" ]
}
