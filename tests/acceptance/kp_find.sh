#!/usr/bin/env bash
# UNG-127: find prints the KOTOR 1 folder for RPM, Flatpak and extra-library layouts; exit 3 when absent.
. "$(dirname "$0")/_kp_lib.sh"

g="$(mkgame "$RPM_ROOT")"
kp find; [ $RC -eq 0 ] || fail "rpm: rc $RC ($ERR)"; [ "$OUT" = "$g" ] || fail "rpm: stdout '$OUT' != '$g'"
drop_rpm

g="$(mkgame "$FLAT_ROOT")"
kp find; [ $RC -eq 0 ] || fail "flatpak: rc $RC ($ERR)"; [ "$OUT" = "$g" ] || fail "flatpak: stdout '$OUT' != '$g'"

# Game only in a second library with a space in its path, named by libraryfolders.vdf (Flatpak root has no game).
reset_home; lib="$TMP/Extra Lib/Steam Games"; mkdir -p "$lib"
g="$(mkgame "$lib")"; vdf "$FLAT_ROOT" "$FLAT_ROOT" "$lib"
kp find; [ $RC -eq 0 ] || fail "extra library: rc $RC ($ERR)"; [ "$OUT" = "$g" ] || fail "extra library: stdout '$OUT' != '$g'"

# RPM and Flatpak both hold it: RPM wins.
grpm="$(mkgame "$RPM_ROOT")"; mkgame "$FLAT_ROOT" >/dev/null
kp find; [ "$OUT" = "$grpm" ] || fail "both layouts: stdout '$OUT' != rpm '$grpm'"

# A dead library entry in the vdf is not fatal (no game in either Steam root; only the extra library has it).
reset_home; vdf "$FLAT_ROOT" "$FLAT_ROOT" "$TMP/gone" "$lib"
kp find; [ $RC -eq 0 ] || fail "dead library entry: rc $RC ($ERR)"; [ "$OUT" = "$g" ] || fail "dead library entry: stdout '$OUT'"

# No libraryfolders.vdf at all: the root's own steamapps still counts.
reset_home; gf="$(mkgame "$FLAT_ROOT")"
kp find; { [ $RC -eq 0 ] && [ "$OUT" = "$gf" ]; } || fail "no vdf: rc $RC out '$OUT'"

# Manifest left behind but the exe gone: not found.
find "$gf" -name swkotor.exe -delete
kp find; [ $RC -eq 3 ] || fail "manifest without exe: rc $RC, want 3"

# Empty tree.
reset_home
kp find
[ $RC -eq 3 ] || fail "empty tree: rc $RC, want 3"
[ -z "$OUT" ] || fail "empty tree: stdout not empty: '$OUT'"
printf '%s\n' "$ERR" | grep -qF 'KOTOR not found' || fail "empty tree: stderr lacks 'KOTOR not found': '$ERR'"
pass
