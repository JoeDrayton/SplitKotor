#!/usr/bin/env bash
# R4: with msvc-wine missing, build.sh exits non-zero with the exact message.
. "$(dirname "$0")/_lib.sh"
MSG='msvc-wine not found: run tools/setup-msvc-wine.sh'
mkdir -p "$TMP/empty" "$TMP/marker_only"; touch "$TMP/marker_only/.setup-complete"
for case in "$TMP/empty" "$TMP/does-not-exist" "$TMP/marker_only"; do
  d="$(mkcopy "$(basename "$case")")" || exit 1
  run_build "$d" "$case"
  [ $RC -ne 0 ] || fail "$case: exited 0"
  printf '%s\n' "$OUT" | grep -qxF "$MSG" || fail "$case: message not found as a line: $OUT"
  printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "$case: BUILD OK printed"
  [ ! -e "$d/out/dinput8.dll" ] || fail "$case: wrote a DLL"
done
pass
