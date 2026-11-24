#!/usr/bin/env bash
# R2: build.sh runs cl with exactly build.cmd's arguments and writes out/dinput8.dll.
. "$(dirname "$0")/_lib.sh"
need_real_msvc
d="$(mkcopy r2)" || exit 1; log="$TMP/cl.args"
run_build "$d" "$(logging_msvc "$log" r2)"
[ $RC -eq 0 ] || fail "build exited $RC: $OUT"
[ -s "$d/out/dinput8.dll" ] || fail "out/dinput8.dll not written"
[ -s "$log" ] || fail "cl was never run"
diff <(expected_args "$d/build.cmd") "$log" || fail "cl arguments differ from build.cmd's (diff above)"
grep -qx '/DKP_PUBLIC' "$log" && fail "plain build passed /DKP_PUBLIC"
# build.sh follows build.cmd: a changed flag reaches cl.
sed -i 's#/W3 #/W4 #' "$d/build.cmd"
rm -rf "$d/out"; run_build "$d" "$(logging_msvc "$log" r2b)"
grep -qx '/W4' "$log" || fail "a flag changed in build.cmd did not reach cl"
# Runs from any directory.
rm -rf "$d/out"; OUT="$(cd / && MSVC_WINE_DIR="$(logging_msvc "$log" r2c)" bash "$d/build.sh" 2>&1)"; RC=$?
[ $RC -eq 0 ] && [ -s "$d/out/dinput8.dll" ] || fail "build from another directory failed ($RC): $OUT"
pass
