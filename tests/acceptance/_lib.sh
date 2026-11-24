# Shared helpers for the UNG-126 acceptance tests. Sourced; defines functions only.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REAL_MSVC="${MSVC_WINE_DIR:-$HOME/opt/msvc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
NAME="$(basename "$0" .sh)"

fail() { echo "FAIL [$NAME]: $*" >&2; exit 1; }
pass() { echo "PASS [$NAME]"; exit 0; }

# A copy of the build inputs, so fault injection never touches the tree under test.
mkcopy() {
  local d="$TMP/copy.$1"; mkdir -p "$d"
  for f in build.sh build.cmd build_public.cmd; do [ -f "$ROOT/$f" ] || fail "$f missing from the tree"; cp "$ROOT/$f" "$d/"; done
  cp -r "$ROOT/src" "$d/src"
  echo "$d"
}

# The cl arguments build.cmd/build_public.cmd pass: the cl line, minus `cl` and `|| exit /b 1`, backslashes to slashes.
expected_args() {
  grep -E '^cl ' "$1" | tr -d '\r' | sed -E 's/^cl +//; s/ *\|\| *exit \/b 1 *$//; s#\\#/#g' | tr -s ' ' '\n'
}

# A fake MSVC_WINE_DIR with bin/x86/cl running the body given as $2 (a bash snippet; "$@" are cl's arguments).
fake_msvc() {
  local d="$TMP/fake.$1"; mkdir -p "$d/bin/x86"; touch "$d/.setup-complete"
  printf '#!/usr/bin/env bash\n%s\n' "$2" > "$d/bin/x86/cl"; chmod +x "$d/bin/x86/cl"
  echo "$d"
}

# cl wrapper that logs one argument per line to $1, then runs the real cl.
logging_msvc() {
  fake_msvc "log.$2" "printf '%s\n' \"\$@\" > '$1'; exec '$REAL_MSVC/bin/x86/cl' \"\$@\""
}

need_real_msvc() { [ -x "$REAL_MSVC/bin/x86/cl" ] || fail "real msvc-wine not installed at $REAL_MSVC (run tools/setup-msvc-wine.sh)"; }

# run_build <dir> <msvc dir> [args]: sets OUT (stdout+stderr) and RC.
run_build() {
  local d="$1" m="$2"; shift 2
  OUT="$(cd "$d" && MSVC_WINE_DIR="$m" bash ./build.sh "$@" 2>&1)"; RC=$?
}

count_build_ok() { printf '%s\n' "$OUT" | grep -c '^BUILD OK$' || true; }
