# Shared helpers for the UNG-128 kmem acceptance tests. Sourced; defines functions and builds the synthetic game once per test.
. "$(dirname "${BASH_SOURCE[0]}")/_lib.sh"
KMEM="$ROOT/tools/kmem.py"
[ -f "$KMEM" ] || fail "tools/kmem.py missing"
W="$TMP/w"
python3 -I "$ROOT/tests/acceptance/_kmem_build.py" "$W" || fail "could not build the synthetic game"
IMG="$W/image.bin"; PROC="$W/proc"

# run <cmd...>: sets out, err, rc. The real program, started as a process.
run() { out="$("$@" 2>"$TMP/err")"; rc=$?; err="$(cat "$TMP/err")"; }
K()   { python3 "$KMEM" "$@"; }
norm() { printf '%s' "$1" | tr -d ' \n\t' | tr 'A-Z' 'a-z' | sed 's/0x//g'; }
expect_rc() { [ "$rc" -eq "$1" ] || fail "$2: expected rc $1, got $rc (stdout: $out) (stderr: $err)"; }
is_root() { [ "$(id -u)" -eq 0 ]; }

# A second fake /proc: $1 = dir, $2 = scope; with the game as pid 4242 unless $3 = none.
fakeproc() {
  rm -rf "$1"; mkdir -p "$1"
  if [ "${3:-}" = none ]; then
    mkdir -p "$1/sys/kernel/yama" "$1/4300"; echo "$2" > "$1/sys/kernel/yama/ptrace_scope"
    cp "$PROC/4300"/* "$1/4300/"
  else cp -r "$PROC"/. "$1"/; echo "$2" > "$1/sys/kernel/yama/ptrace_scope"; fi
}

VERIFY_LIST="$TMP/list.txt"
cat > "$VERIFY_LIST" <<'L'
0x410000 A::ret0 0
0x410010 A::ret8 8
0x410020 A::movop 4
0x410030 A::pushop 0
L
# The row of an output line for an address (hex, no 0x).
row() { printf '%s\n' "$out" | grep -i "$1" | head -1; }
