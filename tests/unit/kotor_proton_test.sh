# Tests for tools/kotor-proton.sh against a fake Steam tree under a throwaway HOME. pgrep is stubbed: the real
# process table belongs to the machine, where KOTOR may be running.
_fail() { echo "expected: $1"; echo "actual:   $2"; exit 1; }
_eq() { [ "$1" = "$2" ] || _fail "$1" "$2"; }
_has() { [[ "$2" == *"$1"* ]] || _fail "text containing $1" "$2"; }
_absent() { [ ! -e "$1" ] || _fail "no $1" "present"; }
_file() { [ -f "$1" ] || _fail "$1" "missing"; _eq "$2" "$(cat "$1")"; }

_setup() {
    T="$(mktemp -d)"
    trap 'rm -rf "$T"' EXIT
    export HOME="$T/home" GAME_RC=1
    RPM="$HOME/.local/share/Steam"
    FLATPAK="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    REPO="$T/repo"
    mkdir -p "$HOME" "$REPO/tools" "$REPO/out" "$T/bin"
    [ ! -f tools/kotor-proton.sh ] || cp tools/kotor-proton.sh "$REPO/tools/"
    echo "our dll" >"$REPO/out/dinput8.dll"
    echo "default ini" >"$REPO/KotorPlus.ini"
    cat >"$T/bin/pgrep" <<'STUB'
#!/usr/bin/env bash
[ "$*" = "-x swkotor.exe" ] || exit 1
exit "$GAME_RC"
STUB
    chmod +x "$T/bin/pgrep"
    export PATH="$T/bin:$PATH"
}

# _add_game <library> [<installdir>]: sets GAME
_add_game() {
    GAME="$1/steamapps/common/${2:-swkotor}"
    mkdir -p "$GAME"
    printf '"AppState"\n{\n\t"appid"\t\t"32370"\n\t"installdir"\t\t"%s"\n}\n' "${2:-swkotor}" \
        >"$1/steamapps/appmanifest_32370.acf"
    echo "stub exe" >"$GAME/swkotor.exe"
}

# _add_libraries <steam root> <library>...
_add_libraries() {
    local root="$1" i=0; shift
    mkdir -p "$root/steamapps"
    {
        printf '"libraryfolders"\n{\n'
        for library in "$@"; do printf '\t"%d"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n' "$i" "$library"; i=$((i + 1)); done
        printf '}\n'
    } >"$root/steamapps/libraryfolders.vdf"
}

_run() { OUT="$(cd "${CWD:-$T}" && bash "$REPO/tools/kotor-proton.sh" "$@" 2>"$T/err")" && RC=0 || RC=$?; ERR="$(cat "$T/err")"; }
_ok() { _run "$@"; _eq 0 "$RC"; }
_backups() { find "$GAME" -name 'dinput8.dll.bak-*' | sort; }
_snapshot() { (cd "$1" && find . -type f -exec sha256sum {} + | sort -k2); }

find_rpm_layout() { _setup; _add_game "$RPM"; _ok find; _eq "$GAME" "$OUT"; }
find_flatpak_layout() { _setup; _add_game "$FLATPAK"; _ok find; _eq "$GAME" "$OUT"; }
find_extra_library_with_spaces() {
    _setup; _add_libraries "$RPM" "$RPM" "$T/My Games"; _add_game "$T/My Games" "KOTOR one"
    _ok find; _eq "$GAME" "$OUT"
}
find_missing_game() { _setup; _add_libraries "$RPM" "$RPM"; _run find; _eq 3 "$RC"; _has "KOTOR not found" "$ERR"; _eq "" "$OUT"; }
find_ignores_manifest_without_exe() { _setup; _add_game "$RPM"; rm "$GAME/swkotor.exe"; _run find; _eq 3 "$RC"; }
find_ignores_exe_without_manifest() { _setup; _add_game "$RPM"; rm "$RPM/steamapps/appmanifest_32370.acf"; _run find; _eq 3 "$RC"; }
find_prefers_rpm() { _setup; _add_game "$FLATPAK"; _add_game "$RPM"; _ok find; _eq "$GAME" "$OUT"; }
find_skips_dead_library() {
    _setup; _add_libraries "$RPM" "/nonexistent/lib" "$T/lib2"; _add_game "$T/lib2"
    _ok find; _eq "$GAME" "$OUT"
}
find_reads_crlf_files() {
    _setup; _add_libraries "$RPM" "$T/lib2"; _add_game "$T/lib2"
    sed -i 's/$/\r/' "$RPM/steamapps/libraryfolders.vdf" "$T/lib2/steamapps/appmanifest_32370.acf"
    _ok find; _eq "$GAME" "$OUT"
}

check_match() {
    _setup; _add_game "$RPM"
    # The real exe can't be shipped in a test, so the stub answers with the expected hash.
    printf '#!/usr/bin/env bash\necho "34e6d971c034222a417995d8e1e8fdd9f8781795c9c289bd86c499a439f34c88  -"\n' >"$T/bin/sha256sum"
    chmod +x "$T/bin/sha256sum"
    _ok check
    _eq "34E6D971C034222A417995D8E1E8FDD9F8781795C9C289BD86C499A439F34C88 match" "$OUT"
}
check_mismatch() {
    _setup; _add_game "$RPM"; _run check; _eq 4 "$RC"
    _has "$(sha256sum "$GAME/swkotor.exe" | cut -d' ' -f1 | tr a-f A-F) MISMATCH" "$OUT"
}
check_no_game() { _setup; _run check; _eq 3 "$RC"; _has "KOTOR not found" "$ERR"; }

install_copies_dll_and_ini() {
    _setup; _add_game "$RPM"; _ok install
    _file "$GAME/dinput8.dll" "our dll"; _file "$GAME/KotorPlus.ini" "default ini"; _eq "" "$(_backups)"
}
install_backs_up_existing_dll() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; _ok install
    _file "$GAME/dinput8.dll" "our dll"; _file "$(_backups)" foreign
}
install_refuses_while_game_runs() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; before="$(_snapshot "$GAME")"; GAME_RC=0
    _run install; _eq 5 "$RC"; _has "close KOTOR first" "$ERR"; _eq "$before" "$(_snapshot "$GAME")"
}
install_fails_closed_when_pgrep_errors() { _setup; _add_game "$RPM"; GAME_RC=3; _run install; _eq 1 "$RC"; _absent "$GAME/dinput8.dll"; }
install_missing_dll() {
    _setup; _add_game "$RPM"; rm "$REPO/out/dinput8.dll"; echo foreign >"$GAME/dinput8.dll"; before="$(_snapshot "$GAME")"
    _run install; _eq 1 "$RC"; _has "no DLL at $REPO/out/dinput8.dll" "$ERR"; _eq "$before" "$(_snapshot "$GAME")"
}
install_missing_ini() {
    _setup; _add_game "$RPM"; rm "$REPO/KotorPlus.ini"; _run install; _eq 1 "$RC"; _has "KotorPlus.ini" "$ERR"
    _absent "$GAME/dinput8.dll"
}
install_keeps_existing_ini() { _setup; _add_game "$RPM"; echo edited >"$GAME/KotorPlus.ini"; _ok install; _file "$GAME/KotorPlus.ini" edited; }
install_given_relative_path() {
    _setup; _add_game "$RPM"; mkdir "$T/else where"; echo other >"$T/else where/my.dll"
    CWD="$T/else where" _ok install my.dll; _file "$GAME/dinput8.dll" other
}
install_default_dll() { _setup; _add_game "$RPM"; CWD=/ _ok install; _file "$GAME/dinput8.dll" "our dll"; }
install_twice_keeps_both_backups() {
    _setup; _add_game "$RPM"
    # The clock stays in one second for both installs, then moves on.
    cat >"$T/bin/date" <<'STUB'
#!/usr/bin/env bash
n=$(($(cat "$STUB_COUNT" 2>/dev/null || echo 0) + 1)); echo "$n" >"$STUB_COUNT"
[ "$n" -le 2 ] && echo 20260101-000000 || echo 20260101-000001
STUB
    chmod +x "$T/bin/date"; export STUB_COUNT="$T/count"
    echo foreign >"$GAME/dinput8.dll"; _ok install; _ok install
    _eq 2 "$(_backups | wc -l)"
    _file "$(_backups | head -1)" foreign; _file "$(_backups | tail -1)" "our dll"
}
install_no_game() { _setup; _run install; _eq 3 "$RC"; _has "KOTOR not found" "$ERR"; }
install_prints_launch_option() {
    _setup; _add_game "$RPM"; _ok install
    grep -qxF 'WINEDLLOVERRIDES="dinput8=n,b" %command%' <<<"$OUT" || _fail "the launch option on its own line" "$OUT"
}
install_leaves_steam_config() {
    _setup; _add_libraries "$RPM" "$RPM"; _add_game "$RPM"; mkdir -p "$RPM/userdata/1/config"
    echo cfg >"$RPM/userdata/1/config/localconfig.vdf"
    before="$(_snapshot "$RPM" | grep -v 'common/swkotor')"; _ok install; _ok uninstall
    _eq "$before" "$(_snapshot "$RPM" | grep -v 'common/swkotor')"
}

uninstall_restores_newest_backup() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; _ok install
    echo older >"$GAME/dinput8.dll.bak-20000101-000000"
    _ok uninstall; _file "$GAME/dinput8.dll" foreign; _absent "$GAME/KotorPlus.ini"
    _eq "$GAME/dinput8.dll.bak-20000101-000000" "$(_backups)"
}
uninstall_unwinds_repeated_installs() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; _ok install; echo "our dll 2" >"$REPO/out/dinput8.dll"; _ok install
    _ok uninstall; _file "$GAME/dinput8.dll" "our dll"
    _ok uninstall; _file "$GAME/dinput8.dll" foreign; _eq "" "$(_backups)"; _absent "$GAME/dinput8.dll.kotorplus"
}
uninstall_without_backup() {
    _setup; _add_game "$RPM"; _ok install; _ok uninstall
    _eq "./swkotor.exe" "$(cd "$GAME" && find . -type f)"
}
uninstall_leaves_foreign_dll() {
    _setup; _add_game "$RPM"; echo older >"$GAME/dinput8.dll"; _ok install; echo foreign >"$GAME/dinput8.dll"; _ok uninstall
    _file "$GAME/dinput8.dll" foreign; _file "$(_backups)" older
}
uninstall_restores_when_our_dll_is_gone() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; _ok install; rm "$GAME/dinput8.dll"
    _ok uninstall; _file "$GAME/dinput8.dll" foreign
}
uninstall_refuses_while_game_runs() {
    _setup; _add_game "$RPM"; _ok install; before="$(_snapshot "$GAME")"; GAME_RC=0
    _run uninstall; _eq 5 "$RC"; _has "close KOTOR first" "$ERR"; _eq "$before" "$(_snapshot "$GAME")"
}
uninstall_no_game() { _setup; _run uninstall; _eq 3 "$RC"; }
uninstall_nothing_installed() {
    _setup; _add_game "$RPM"; echo foreign >"$GAME/dinput8.dll"; echo mine >"$GAME/KotorPlus.ini"; before="$(_snapshot "$GAME")"
    _ok uninstall; _eq "$before" "$(_snapshot "$GAME")"
}

_log() { printf '%s\n' "$@" >"$GAME/KotorPlus.log"; }
UNPACKED="[10:00:00] game code unpacked after 0.35s - patches applied"
HOOKED="[10:00:01] hooked SwapBuffers (frame cap, overlay, hotkeys)"
log_clean() { _setup; _add_game "$RPM"; _log "$UNPACKED" "$HOOKED"; _ok log; _eq "$(cat "$GAME/KotorPlus.log")" "$OUT"; }
log_accepts_crlf() { _setup; _add_game "$RPM"; _log "$UNPACKED"$'\r' "$HOOKED"$'\r'; _ok log; }
log_rejects() {
    _setup; _add_game "$RPM"
    _run log; _eq 1 "$RC"; _has "KotorPlus.log" "$ERR"
    _log "$UNPACKED"; _run log; _eq 1 "$RC"; _has "hooked SwapBuffers" "$ERR"; _eq "$UNPACKED" "$OUT"
    _log "$HOOKED"; _run log; _eq 1 "$RC"; _has "game code unpacked after" "$ERR"
    _log "$UNPACKED" "$HOOKED" "[10:00:02] SKIP free look: unexpected bytes"; _run log; _eq 1 "$RC"; _has "SKIP" "$ERR"
}
log_no_game() { _setup; _run log; _eq 3 "$RC"; }

usage_errors() {
    _setup; _add_game "$RPM"
    for args in "" bogus "find extra" "install a b"; do
        # shellcheck disable=SC2086
        _run $args; _eq 2 "$RC"; _has usage "$ERR"
    done
}
