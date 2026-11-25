#!/usr/bin/env bash
# with no game running, --image gives the same results as the process path.
. "$(dirname "$0")/_kmem_lib.sh"
fakeproc "$TMP/none" 0 none
export KMEM_PROC="$TMP/none"
run K pid; expect_rc 3 "no game present"
run K --image "$IMG" read 0x401000 8; expect_rc 0 "image read"; a="$out"
KMEM_PROC="$PROC" run K read 0x401000 8; [ "$a" = "$out" ] || fail "image read '$a' differs from process read '$out'"
run K --image "$IMG" dump "$TMP/a.bin"; expect_rc 0 "image dump"
KMEM_PROC="$PROC" run K dump "$TMP/b.bin"; cmp "$TMP/a.bin" "$TMP/b.bin" || fail "image dump differs from process dump"
run K --image "$IMG" verify "$VERIFY_LIST"; expect_rc 0 "image verify"; a="$out"
KMEM_PROC="$PROC" run K verify "$VERIFY_LIST"; [ "$a" = "$out" ] || fail "image verify differs from process verify"
pass
