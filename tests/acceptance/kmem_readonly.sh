#!/usr/bin/env bash
# the game's memory is opened read-only; nothing is written to it.
. "$(dirname "$0")/_kmem_lib.sh"
grep -nE 'O_RDWR|O_WRONLY|r\+b|rb\+|PTRACE_POKE|process_vm_writev|ptrace\(' "$KMEM" && fail "kmem.py contains a write-capable call to the process (lines above)"
m="$PROC/4242/mem"; chmod 0444 "$m"
before="$(sha256sum "$m"; stat -c '%Y %s' "$m")"
export KMEM_PROC="$PROC"
run K read 0x401000 8; expect_rc 0 "read on a read-only mem file"
[ "$(norm "$out")" = "123456789abcdef0" ] || fail "read printed '$out'"
run K dump "$TMP/d.bin"; expect_rc 0 "dump on a read-only mem file"
run K verify "$VERIFY_LIST"; expect_rc 0 "verify on a read-only mem file"
[ "$before" = "$(sha256sum "$m"; stat -c '%Y %s' "$m")" ] || fail "the mem file changed"
pass
