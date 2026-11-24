#!/usr/bin/env bash
# R3: BUILD OK and exit 0 only on success; compile or link errors exit non-zero without BUILD OK.
. "$(dirname "$0")/_lib.sh"
need_real_msvc
junit_failed() { [ -f "$1/.build-truth/junit.xml" ] && grep -Eq '<failure|<error|failures="[1-9]' "$1/.build-truth/junit.xml"; }

d="$(mkcopy ok)" || exit 1
run_build "$d" "$REAL_MSVC"
[ $RC -eq 0 ] || fail "good build exited $RC: $OUT"
[ "$(count_build_ok)" = 1 ] || fail "expected exactly one BUILD OK line: $OUT"
[ -f "$d/.build-truth/junit.xml" ] || fail "no .build-truth/junit.xml after a good build"
grep -q 'failures="0"' "$d/.build-truth/junit.xml" || fail "junit of a good build reports failures"

bad() { # <name> <code appended to the copied source>
  local c; c="$(mkcopy "$1")" || exit 1
  printf '\n%s\n' "$2" >> "$c/src/kotorplus.cpp"
  mkdir -p "$c/out"; echo stale > "$c/out/dinput8.dll"
  run_build "$c" "$REAL_MSVC"
  [ $RC -ne 0 ] || fail "$1: build exited 0 on a broken source"
  printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "$1: BUILD OK printed on failure"
  [ ! -e "$c/out/dinput8.dll" ] || fail "$1: a stale or partial out/dinput8.dll was left behind"
  junit_failed "$c" || fail "$1: junit does not record the failure"
}
bad syntax 'this is not valid C++;'
bad link 'extern "C" int kp_undefined_symbol(void); int kp_force_ref = kp_undefined_symbol();'

# cl exiting non-zero is a failure even when it left a DLL behind.
stub="$(fake_msvc ex "for a in \"\$@\"; do case \$a in /Fe:*) o=\${a#/Fe:};; esac; done; mkdir -p \"\$(dirname \"\$o\")\"; cp '$d/out/dinput8.dll' \"\$o\"; exit 1")"
c="$(mkcopy exit1)" || exit 1
run_build "$c" "$stub"
[ $RC -ne 0 ] || fail "cl exit 1 with a DLL present: build exited 0"
printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "cl exit 1 with a DLL present: BUILD OK printed"

run_build "$d" "$REAL_MSVC" --bogus
[ $RC -eq 2 ] || fail "unknown argument: expected exit 2, got $RC"
printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "BUILD OK printed for an unknown argument"
pass
