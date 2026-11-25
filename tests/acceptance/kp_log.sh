#!/usr/bin/env bash
# UNG-127: log prints KotorPlus.log; exit 0 only with both markers and no SKIP line.
. "$(dirname "$0")/_kp_lib.sh"
g="$(mkgame "$RPM_ROOT")"
A='game code unpacked after 0.50s - patches applied'   # src/kotorplus.cpp:1822
B='hooked SwapBuffers (frame cap, overlay, hotkeys)'    # src/kotorplus.cpp:1800
S='SKIP alt-tab patch (x): unexpected bytes'            # src/kotorplus.cpp:1890

printf 'KotorPlus start\n%s\n%s\n' "$A" "$B" > "$g/KotorPlus.log"
kp log; [ $RC -eq 0 ] || fail "clean log: rc $RC ($ERR)"
{ printf '%s\n' "$OUT" | grep -qF "$A" && printf '%s\n' "$OUT" | grep -qF "$B"; } || fail "clean log: text not printed: '$OUT'"

printf 'KotorPlus start\r\n%s\r\n%s\r\n' "$A" "$B" > "$g/KotorPlus.log"
kp log; [ $RC -eq 0 ] || fail "CRLF log: rc $RC"

rej() { kp log; [ $RC -eq 1 ] || fail "$1: rc $RC, want 1"; }
printf '%s\n' "$A" > "$g/KotorPlus.log"; rej "only unpacked marker"
printf '%s\n' "$B" > "$g/KotorPlus.log"; rej "only SwapBuffers marker"
printf '%s\n%s\n%s\n' "$A" "$B" "$S" > "$g/KotorPlus.log"; rej "SKIP line"
printf '%s\n%s\n%s\n' "$S" "$A" "$B" > "$g/KotorPlus.log"; rej "SKIP line first"
: > "$g/KotorPlus.log"; rej "empty log"
find "$g" -name KotorPlus.log -delete; rej "no log file"

reset_home; kp log; [ $RC -eq 3 ] || fail "no game: rc $RC, want 3"
pass
