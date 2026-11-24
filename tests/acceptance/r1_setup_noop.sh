#!/usr/bin/env bash
# R1: a second run of the setup script on a complete install changes nothing and exits 0.
. "$(dirname "$0")/_lib.sh"
[ -f "$ROOT/tools/setup-msvc-wine.sh" ] || fail "tools/setup-msvc-wine.sh missing"
need_real_msvc
snap() { find "$REAL_MSVC" -printf '%P\t%T@\t%s\n' | sort; }
before="$(snap)"
check() {
  local start=$SECONDS out rc
  out="$("$@" 2>&1)"; rc=$?
  [ $rc -eq 0 ] || fail "setup exited $rc: $out"
  [ $((SECONDS - start)) -lt 60 ] || fail "setup took $((SECONDS - start))s on a complete install"
  printf '%s' "$out" | grep -qi 'downloading' && fail "setup downloaded on a complete install"
  [ "$(snap)" = "$before" ] || fail "files or mtimes under $REAL_MSVC changed"
}
check env MSVC_WINE_DIR="$REAL_MSVC" bash "$ROOT/tools/setup-msvc-wine.sh"
[ "$REAL_MSVC" = "$HOME/opt/msvc" ] && check env -u MSVC_WINE_DIR bash "$ROOT/tools/setup-msvc-wine.sh"
pass
