#!/usr/bin/env bash
# a permission failure exits 5 and prints the exact fix; the tool never changes system settings itself.
. "$(dirname "$0")/_kmem_lib.sh"
is_root && { echo "SKIP [$NAME]: root can read a mode-000 file"; exit 0; }
mkdir -p "$TMP/bin"; : > "$TMP/calls.log"
for t in sysctl setcap; do printf '#!/bin/sh\necho %s "$@" >> "%s"\nexit 0\n' "$t" "$TMP/calls.log" > "$TMP/bin/$t"; chmod +x "$TMP/bin/$t"; done
export PATH="$TMP/bin:$PATH"
for scope in 1 2 3; do
  fakeproc "$TMP/p$scope" "$scope"; chmod 000 "$TMP/p$scope/4242/mem"
  KMEM_PROC="$TMP/p$scope" run K read 0x401000 4; expect_rc 5 "scope $scope"
  case "$err" in *ptrace_scope*) ;; *) fail "scope $scope: no ptrace_scope in: $err" ;; esac
  case "$err" in *sysctl*|*setcap*) ;; *) fail "scope $scope: no sysctl or setcap command in: $err" ;; esac
  printf '%s\n' "$err" | grep -Eq 'ptrace_scope[ =]*0|ptrace_scope=0|=0' || fail "scope $scope: the value to set (0) is not stated: $err"
done
printf '%s\n' "$err" | grep -qi reboot || fail "scope 3 must say a reboot is needed: $err"
KMEM_PROC="$TMP/p1" run K dump "$TMP/d.bin"; expect_rc 5 "dump denied"; [ ! -e "$TMP/d.bin" ] || fail "denied dump left a file"
KMEM_PROC="$TMP/p1" run K verify "$VERIFY_LIST"; expect_rc 5 "verify denied"
[ ! -s "$TMP/calls.log" ] || fail "kmem ran a system command: $(cat "$TMP/calls.log")"
[ "$(cat "$TMP/p1/sys/kernel/yama/ptrace_scope")" = "1" ] || fail "kmem changed ptrace_scope"
pass
