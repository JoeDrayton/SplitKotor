#!/usr/bin/env bash
# docs/tools/kmem.md covers usage, the permission fix and tagging VERIFIED.
. "$(dirname "$0")/_lib.sh"
d="$ROOT/docs/tools/kmem.md"; [ -f "$d" ] || fail "docs/tools/kmem.md missing"
for w in pid read dump verify ptrace_scope sysctl setcap VERIFIED RET-MISMATCH NOT-CODE; do grep -q -- "$w" "$d" || fail "doc lacks '$w'"; done
for c in 3 4 5; do grep -Eq "(exit|rc|code)[^.]*\b$c\b|\b$c\b[^.]*(exit|code)" "$d" || fail "doc does not state exit code $c"; done
[ -f "$ROOT/tools/kmem.py" ] || fail "tools/kmem.py missing"
h="$(python3 "$ROOT/tools/kmem.py" --help 2>&1)" || fail "--help failed: $h"
for w in pid read dump verify; do case "$h" in *"$w"*) ;; *) fail "--help lacks $w" ;; esac; done
pass
