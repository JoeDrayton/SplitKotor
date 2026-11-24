#!/usr/bin/env bash
# R7: build.cmd and build_public.cmd are untouched. R8: README has a "Build on Linux" section with both commands.
. "$(dirname "$0")/_lib.sh"
cd "$ROOT" || exit 1
base="$(git merge-base HEAD origin/main 2>/dev/null || git merge-base HEAD main)" || fail "no merge base"
git diff --exit-code "$base" -- build.cmd build_public.cmd >/dev/null || fail "build.cmd or build_public.cmd changed"
sec="$(awk '/^#+[[:space:]]*Build on Linux/{f=1;next} f&&/^#+[[:space:]]/{exit} f' README.md)"
[ -n "$sec" ] || fail "README has no 'Build on Linux' section"
printf '%s\n' "$sec" | grep -qF 'tools/setup-msvc-wine.sh' || fail "section lacks tools/setup-msvc-wine.sh"
printf '%s\n' "$sec" | grep -qF './build.sh' || fail "section lacks ./build.sh"
pass
