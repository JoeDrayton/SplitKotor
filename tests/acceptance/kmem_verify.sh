#!/usr/bin/env bash
# verify rows VERIFIED / RET-MISMATCH <seen> / NOT-CODE; operands holding C3 or C2 are not rets; exit 0 only if all verified.
. "$(dirname "$0")/_kmem_lib.sh"
run K --image "$IMG" verify "$VERIFY_LIST"; expect_rc 0 "all-verified list"
[ "$(printf '%s\n' "$out" | grep -c VERIFIED)" -eq 4 ] || fail "want 4 VERIFIED rows, got: $out"
KMEM_PROC="$PROC" run K verify "$VERIFY_LIST"; expect_rc 0 "all-verified list through the process"
cat > "$TMP/mixed.txt" <<'L'
0x410000 A::ret0 0
0x410040 A::mismatch 4
0x410050 A::zeros 0
0x410060 A::text 0
0x410070 A::thunk 0
0x900000 A::range 0
L
run K --image "$IMG" verify "$TMP/mixed.txt"; expect_rc 1 "mixed list"
row 410000 >/dev/null; r="$(row 410000)"; case "$r" in *VERIFIED*) ;; *) fail "ret0 row: '$r'" ;; esac
r="$(row 410040)"; printf '%s\n' "$r" | grep -Eq 'RET-MISMATCH +(12|0x0*[cC])\b' || fail "mismatch row must show what was read (12): '$r'"
for a in 410050 410060 900000; do r="$(row $a)"; case "$r" in *NOT-CODE*) ;; *) fail "$a row: '$r'" ;; esac; done
r="$(row 410070)"; printf '%s\n' "$r" | grep -Eqi 'RET-MISMATCH +none' || fail "thunk row: '$r'"
[ "$(printf '%s\n' "$out" | grep -c .)" -eq 6 ] || fail "want one row per address (6), got: $out"
# Wrong expectation on operand bytes: a byte scan would see C3 (0) and C2 04 00 (4) first.
printf '0x410020 A::movop 0\n0x410030 A::pushop 4\n' > "$TMP/ops.txt"
run K --image "$IMG" verify "$TMP/ops.txt"; expect_rc 1 "operand decoding"
r="$(row 410020)"; printf '%s\n' "$r" | grep -Eq 'RET-MISMATCH +(4|0x0*4)\b' || fail "movop must read ret 4: '$r'"
r="$(row 410030)"; printf '%s\n' "$r" | grep -Eq 'RET-MISMATCH +(0|0x0*)( |$)' || fail "pushop must read ret 0: '$r'"
: > "$TMP/empty.txt"; run K --image "$IMG" verify "$TMP/empty.txt"; expect_rc 2 "empty list"
printf '0x410000 onlytwo\n' > "$TMP/bad1.txt"; run K --image "$IMG" verify "$TMP/bad1.txt"; expect_rc 2 "two fields"
printf '0x410000 A::x abc\n' > "$TMP/bad2.txt"; run K --image "$IMG" verify "$TMP/bad2.txt"; expect_rc 2 "non-numeric expected_ret"
printf 'zzzz A::x 0\n' > "$TMP/bad3.txt"; run K --image "$IMG" verify "$TMP/bad3.txt"; expect_rc 2 "non-hex address"
run K --image "$IMG" verify "$TMP/nonexistent.txt"; expect_rc 2 "missing list file"
pass
