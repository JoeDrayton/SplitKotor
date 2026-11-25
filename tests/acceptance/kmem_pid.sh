#!/usr/bin/env bash
# pid prints the game's pid; exit 3 'KOTOR not running'; cmdline decoys ignored; two games exit 2 naming both.
. "$(dirname "$0")/_kmem_lib.sh"
export KMEM_PROC="$PROC"
run K pid;                       expect_rc 0 "pid"
[ "$(norm "$out")" = "4242" ] || fail "pid printed '$out', expected exactly 4242"
fakeproc "$TMP/none" 0 none
KMEM_PROC="$TMP/none" run K pid; expect_rc 3 "no game (only a cmdline decoy)"
case "$err$out" in *"KOTOR not running"*) ;; *) fail "no 'KOTOR not running' message: $err$out" ;; esac
fakeproc "$TMP/two" 0
mkdir -p "$TMP/two/4243"; cp "$PROC/4242"/comm "$PROC/4242"/cmdline "$PROC/4242"/maps "$TMP/two/4243/"
KMEM_PROC="$TMP/two" run K pid;  expect_rc 2 "two games"
case "$err$out" in *4242*4243*|*4243*4242*) ;; *) fail "two games: both pids not listed: $err$out" ;; esac
pass
