#!/usr/bin/env bash
# UNG-127: install copies DLL and ini, refuses while the game runs, backs up, keeps an existing ini.
. "$(dirname "$0")/_kp_lib.sh"
g="$(mkgame "$RPM_ROOT")"
printf 'DLL-ONE' > "$TMP/one.dll"; printf 'DLL-TWO' > "$TMP/two.dll"

before="$(tree_hash "$g")"
start_fake_game
kp install "$TMP/one.dll"
[ $RC -eq 5 ] || fail "game running: rc $RC, want 5"
printf '%s\n%s\n' "$OUT" "$ERR" | grep -qF 'close KOTOR first' || fail "game running: no 'close KOTOR first'"
[ "$(tree_hash "$g")" = "$before" ] || fail "game running: game folder changed"
stop_fake_game

kp install "$TMP/one.dll"; [ $RC -eq 0 ] || fail "install: rc $RC ($ERR)"
[ "$(cat "$g/dinput8.dll")" = DLL-ONE ] || fail "install: dll bytes wrong"
cmp -s "$g/KotorPlus.ini" "$REPO/KotorPlus.ini" || fail "install: ini not copied"
ls "$g"/dinput8.dll.bak-* >/dev/null 2>&1 && fail "install: backup made with nothing to back up"

# Second install with another DLL: backup of the first; edited ini kept.
printf 'FpsCap=999\n' > "$g/KotorPlus.ini"
kp install "$TMP/two.dll"; [ $RC -eq 0 ] || fail "reinstall: rc $RC ($ERR)"
[ "$(cat "$g/dinput8.dll")" = DLL-TWO ] || fail "reinstall: dll not replaced"
set -- "$g"/dinput8.dll.bak-*; [ -f "$1" ] || fail "reinstall: no dinput8.dll.bak-<timestamp>"
[ "$(cat "$1")" = DLL-ONE ] || fail "reinstall: backup does not hold the first DLL"
[ "$(cat "$g/KotorPlus.ini")" = 'FpsCap=999' ] || fail "reinstall: existing ini overwritten"

# Installs inside one second keep every backup.
printf 'DLL-THREE' > "$TMP/three.dll"
kp install "$TMP/three.dll"; kp install "$TMP/one.dll"
n=$(ls "$g"/dinput8.dll.bak-* | wc -l); [ "$n" -ge 3 ] || fail "rapid installs: $n backups, want at least 3"
for want in DLL-ONE DLL-TWO DLL-THREE; do cat "$g"/dinput8.dll.bak-* | grep -qF "$want" || fail "rapid installs: backup of $want lost"; done

# Relative DLL path from another folder.
clean_game "$g"; mkdir -p "$TMP/elsewhere"; cp "$TMP/two.dll" "$TMP/elsewhere/rel.dll"
(cd "$TMP/elsewhere" && HOME="$HOME_T" "$KP" install rel.dll >/dev/null 2>&1) || fail "relative path: install failed"
[ "$(cat "$g/dinput8.dll")" = DLL-TWO ] || fail "relative path: wrong bytes"

# No argument: out/dinput8.dll of the script's repo, from any cwd.
clean_game "$g"
(cd "$TMP/elsewhere" && HOME="$HOME_T" "$KP" install >/dev/null 2>&1) || fail "default dll: install failed"
[ "$(cat "$g/dinput8.dll")" = DEFAULT-DLL-BYTES ] || fail "default dll: not taken from <repo>/out/dinput8.dll"

# Missing source DLL: rc 1, nothing changed. Missing source ini: rc 1, nothing changed.
before="$(tree_hash "$g")"
kp install "$TMP/nope.dll"; [ $RC -eq 1 ] || fail "missing dll: rc $RC, want 1"
[ "$(tree_hash "$g")" = "$before" ] || fail "missing dll: folder changed"
mv "$REPO/KotorPlus.ini" "$TMP/ini.saved"; clean_game "$g"; before="$(tree_hash "$g")"
kp install "$TMP/one.dll"; [ $RC -eq 1 ] || fail "missing ini: rc $RC, want 1"
[ "$(tree_hash "$g")" = "$before" ] || fail "missing ini: folder changed"
mv "$TMP/ini.saved" "$REPO/KotorPlus.ini"

# No game folder: rc 3, nothing written anywhere under HOME.
reset_home
kp install "$TMP/one.dll"; [ $RC -eq 3 ] || fail "no game: rc $RC, want 3"
[ -z "$(find "$HOME_T" -type f)" ] || fail "no game: files written"

# Usage: no command and an unknown command: rc 2 and a usage message.
kp; [ $RC -eq 2 ] || fail "no command: rc $RC, want 2"
printf '%s\n%s\n' "$OUT" "$ERR" | grep -qi usage || fail "no command: no usage text"
kp bogus; [ $RC -eq 2 ] || fail "bogus: rc $RC, want 2"
printf '%s\n%s\n' "$OUT" "$ERR" | grep -qi usage || fail "bogus: no usage text"
pass
