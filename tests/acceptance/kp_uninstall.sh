#!/usr/bin/env bash
# UNG-127: uninstall removes our DLL and ini, restores the newest backup, leaves a DLL that is not ours.
. "$(dirname "$0")/_kp_lib.sh"
g="$(mkgame "$RPM_ROOT")"
printf 'DLL-OURS' > "$TMP/ours.dll"; printf 'FOREIGN' > "$g/dinput8.dll"

# Over a foreign DLL: uninstall brings it back byte for byte.
kp install "$TMP/ours.dll"; [ $RC -eq 0 ] || fail "install: rc $RC ($ERR)"
start_fake_game
kp uninstall; [ $RC -eq 5 ] || fail "game running: rc $RC, want 5"
[ "$(cat "$g/dinput8.dll")" = DLL-OURS ] || fail "game running: DLL changed"
stop_fake_game
kp uninstall; [ $RC -eq 0 ] || fail "uninstall: rc $RC ($ERR)"
[ "$(cat "$g/dinput8.dll")" = FOREIGN ] || fail "uninstall: foreign DLL not restored"
[ ! -e "$g/KotorPlus.ini" ] || fail "uninstall: ini left"
ls "$g"/dinput8.dll.bak-* >/dev/null 2>&1 && fail "uninstall: restored backup still listed as a backup"

# With no backup: both files gone.
clean_game "$g"
kp install "$TMP/ours.dll"; kp uninstall; [ $RC -eq 0 ] || fail "no backup: rc $RC"
{ [ ! -e "$g/dinput8.dll" ] && [ ! -e "$g/KotorPlus.ini" ]; } || fail "no backup: files left"

# Newest of several backups wins.
kp install "$TMP/ours.dll"; printf 'OLD' > "$g/dinput8.dll.bak-20200101-000000"; printf 'NEW' > "$g/dinput8.dll.bak-20300101-000000"
kp uninstall; [ "$(cat "$g/dinput8.dll" 2>/dev/null)" = NEW ] || fail "newest backup not restored"

# A DLL someone else put there after our install is not ours: it stays.
clean_game "$g"
kp install "$TMP/ours.dll"; printf 'SOMEONE-ELSE' > "$g/dinput8.dll"
kp uninstall; [ "$(cat "$g/dinput8.dll")" = SOMEONE-ELSE ] || fail "foreign DLL removed or replaced"

# Nothing installed: rc 0, nothing removed.
clean_game "$g"; printf 'PLAIN' > "$g/dinput8.dll"
kp uninstall; [ $RC -eq 0 ] || fail "nothing installed: rc $RC"
[ "$(cat "$g/dinput8.dll")" = PLAIN ] || fail "nothing installed: DLL removed"

reset_home; kp uninstall; [ $RC -eq 3 ] || fail "no game: rc $RC, want 3"
pass
