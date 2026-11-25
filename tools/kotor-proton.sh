#!/usr/bin/env bash
# Installs KotorPlus into the Steam copy of KOTOR 1 for Proton. See docs/platform/linux.md.
set -uo pipefail

APP_ID=32370
EXE_SHA256=34E6D971C034222A417995D8E1E8FDD9F8781795C9C289BD86C499A439F34C88
LAUNCH_OPTION='WINEDLLOVERRIDES="dinput8=n,b" %command%'
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 1

die() { # die <exit code> <message>
    echo "$2" >&2
    exit "$1"
}

usage() { die 2 "usage: kotor-proton.sh find | check | install [dll] | uninstall | log"; }

vdf_values() { # vdf_values <file> <key>
    sed -nE "s/^[[:space:]]*\"$2\"[[:space:]]+\"(.*)\"[[:space:]]*\$/\1/p" "$1" | sed 's/\\\\/\\/g'
}

steam_libraries() {
    local root
    for root in "$HOME/.local/share/Steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        echo "$root"
        [ ! -f "$root/steamapps/libraryfolders.vdf" ] || vdf_values "$root/steamapps/libraryfolders.vdf" path
    done
}

find_game() {
    local library manifest dir
    while read -r library; do
        manifest="$library/steamapps/appmanifest_$APP_ID.acf"
        [ -f "$manifest" ] || continue
        dir="$library/steamapps/common/$(vdf_values "$manifest" installdir)"
        if [ -f "$dir/swkotor.exe" ]; then
            echo "$dir"
            return 0
        fi
    done < <(steam_libraries)
    return 1
}

# Matches the process name, not the command line: pgrep -f also finds any shell or editor that names swkotor.exe.
require_game_closed() {
    local rc=0
    pgrep -x swkotor.exe >/dev/null || rc=$?
    case "$rc" in
        0) die 5 "close KOTOR first" ;;
        1) ;;
        *) die 1 "cannot tell whether KOTOR is running: pgrep failed (install procps-ng)" ;;
    esac
}

sha256() { sha256sum <"$1" | cut -d' ' -f1 | tr a-f A-F; }

is_ours() { [ -f "$record" ] && grep -qxF "$(sha256 "$1")" "$record"; }

check() {
    local hash
    hash="$(sha256 "$game/swkotor.exe")" || die 1 "cannot read $game/swkotor.exe"
    if [ "$hash" = "$EXE_SHA256" ]; then
        echo "$hash match"
    else
        echo "$hash MISMATCH (expected $EXE_SHA256)"
        exit 4
    fi
}

install_mod() { # install_mod <dll>
    local source="$1" backup
    require_game_closed
    [ -f "$source" ] || die 1 "no DLL at $source: build it first or give its path"
    [ -f "$repo/KotorPlus.ini" ] || die 1 "no KotorPlus.ini at $repo/KotorPlus.ini"

    # Copied next to its target and renamed, so a full disk can't leave half a DLL in place.
    if ! cp -- "$source" "$dll.new"; then
        rm -f "$dll.new"
        die 1 "cannot copy $source to $game"
    fi
    sha256 "$dll.new" >>"$record" || die 1 "cannot write $record"
    if [ -e "$dll" ]; then
        # Names carry whole seconds and must stay unique and sortable.
        while backup="$dll.bak-$(date +%Y%m%d-%H%M%S)"; [ -e "$backup" ]; do sleep 0.2; done
        cp -p -- "$dll" "$backup" || die 1 "cannot back up $dll"
        echo "backed up the existing dinput8.dll to $backup"
    fi
    mv -f -- "$dll.new" "$dll" || die 1 "cannot write $dll"
    [ -e "$ini" ] || cp -- "$repo/KotorPlus.ini" "$ini" || die 1 "cannot write $ini"

    echo "installed into $game"
    echo "Paste this into Steam: KOTOR > Properties > General > Launch Options:"
    echo "$LAUNCH_OPTION"
}

uninstall_mod() {
    local newest
    require_game_closed
    if [ ! -f "$record" ]; then
        echo "nothing installed by this script in $game"
        return 0
    fi
    rm -f -- "$ini" || die 1 "cannot remove $ini"
    if [ -e "$dll" ] && ! is_ours "$dll"; then
        rm -f -- "$record"
        echo "left dinput8.dll in place: it is not the one this script installed"
        return 0
    fi
    newest="$(find "$game" -maxdepth 1 -name 'dinput8.dll.bak-*' | sort | tail -n 1)"
    if [ -n "$newest" ]; then
        mv -f -- "$newest" "$dll" || die 1 "cannot restore $newest"
        echo "restored $newest"
        # A restored earlier KotorPlus build still needs the record for the next uninstall.
        is_ours "$dll" || rm -f -- "$record"
    else
        rm -f -- "$dll" "$record" || die 1 "cannot remove $dll"
    fi
    echo "uninstalled from $game"
}

show_log() {
    local log="$game/KotorPlus.log" marker rc=0
    [ -f "$log" ] || die 1 "no KotorPlus.log in $game: the DLL did not load (is the launch option set?)"
    cat -- "$log"
    for marker in "game code unpacked after" "hooked SwapBuffers"; do
        grep -qF "$marker" "$log" || { echo "missing from the log: $marker" >&2; rc=1; }
    done
    if grep -qF SKIP "$log"; then
        echo "the log has a SKIP line: a patch was not applied" >&2
        rc=1
    fi
    return "$rc"
}

command="${1:-}"
case "$command:$#" in
    find:1 | check:1 | install:[12] | uninstall:1 | log:1) ;;
    *) usage ;;
esac

game="$(find_game)" || die 3 "KOTOR not found"
dll="$game/dinput8.dll"
ini="$game/KotorPlus.ini"
record="$game/dinput8.dll.kotorplus" # SHA-256 of every DLL install has written, one per line

case "$command" in
    find) echo "$game" ;;
    check) check ;;
    install) install_mod "${2:-$repo/out/dinput8.dll}" ;;
    uninstall) uninstall_mod ;;
    log) show_log ;;
esac
