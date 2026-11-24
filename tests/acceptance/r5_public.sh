#!/usr/bin/env bash
# R5: build.sh --public builds what build_public.cmd builds.
. "$(dirname "$0")/_lib.sh"
need_real_msvc
d="$(mkcopy r5)" || exit 1; log="$TMP/cl.args"
run_build "$d" "$(logging_msvc "$log" r5)" --public
[ $RC -eq 0 ] || fail "--public exited $RC: $OUT"
[ "$(count_build_ok)" = 1 ] || fail "expected one BUILD OK line: $OUT"
[ -s "$d/out_public/dinput8.dll" ] || fail "out_public/dinput8.dll not written"
[ ! -e "$d/out/dinput8.dll" ] || fail "--public wrote out/dinput8.dll"
grep -qx '/DKP_PUBLIC' "$log" || fail "/DKP_PUBLIC not passed to cl"
diff <(expected_args "$d/build_public.cmd") "$log" || fail "cl arguments differ from build_public.cmd's"
pass
