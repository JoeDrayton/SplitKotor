#!/usr/bin/env bash
# UNG-127: check prints the exe's SHA-256 and match / MISMATCH against the Steam v1.03 hash; exit 4 on mismatch.
. "$(dirname "$0")/_kp_lib.sh"
GOOD=34E6D971C034222A417995D8E1E8FDD9F8781795C9C289BD86C499A439F34C88
REAL="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/swkotor/swkotor.exe"

g="$(mkgame "$RPM_ROOT" "NOT THE REAL EXE")"
want="$(sha256sum "$g/swkotor.exe" | cut -d' ' -f1)"
kp check
[ $RC -eq 4 ] || fail "mismatch: rc $RC, want 4"
printf '%s\n' "$OUT" | grep -qF MISMATCH || fail "mismatch: no MISMATCH in stdout: '$OUT'"
printf '%s\n' "$OUT" | grep -qiF "$want" || fail "mismatch: actual hash $want not printed: '$OUT'"
printf '%s\n' "$OUT" | grep -qw match && fail "mismatch: printed 'match'"

# Match needs bytes with the real hash: only the installed game has them (read only, copied into the temp tree).
[ -f "$REAL" ] || fail "no real swkotor.exe at $REAL to prove the match path (CANNOT CONFIRM without it)"
[ "$(sha256sum "$REAL" | cut -d' ' -f1 | tr a-f A-F)" = "$GOOD" ] || fail "the installed exe is not Steam v1.03; cannot prove match"
cp "$REAL" "$g/swkotor.exe"
kp check
[ $RC -eq 0 ] || fail "match: rc $RC ($ERR)"
printf '%s\n' "$OUT" | grep -qiF "$GOOD" || fail "match: hash not printed: '$OUT'"
printf '%s\n' "$OUT" | grep -qw match || fail "match: no 'match' word: '$OUT'"
printf '%s\n' "$OUT" | grep -qF MISMATCH && fail "match: printed MISMATCH"

reset_home
kp check; [ $RC -eq 3 ] || fail "no game: rc $RC, want 3"
printf '%s\n' "$ERR" | grep -qF 'KOTOR not found' || fail "no game: stderr lacks 'KOTOR not found'"
pass
