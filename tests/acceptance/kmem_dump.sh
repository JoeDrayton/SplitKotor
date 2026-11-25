#!/usr/bin/env bash
# dump writes the whole mapped image (0x400000 to the end of the exe mapping); refuses a packed one (exit 4).
. "$(dirname "$0")/_kmem_lib.sh"
o="$TMP/out.bin"
run K --image "$IMG" dump "$o"; expect_rc 0 "dump (image)"
cmp "$o" "$IMG" || fail "dump from the image is not byte-identical"
rm -f "$o"
KMEM_PROC="$PROC" run K dump "$o"; expect_rc 0 "dump (process, three parts and a gap)"
cmp "$o" "$IMG" || fail "dump through the process is not byte-identical (size $(stat -c %s "$o"), want $(stat -c %s "$IMG"))"
rm -f "$o"
run K --image "$W/image-patched.bin" dump "$o"; expect_rc 0 "dump of a KotorPlus-patched image"
cmp "$o" "$W/image-patched.bin" || fail "patched dump differs"
rm -f "$o"
run K --image "$W/image-packed.bin" dump "$o"; expect_rc 4 "packed image"
case "$err$out" in *"still packed"*) ;; *) fail "no 'still packed': $err$out" ;; esac
[ ! -e "$o" ] || fail "packed refusal left a file behind"
fakeproc "$TMP/pk" 0; cp "$W/image-packed.bin" "$TMP/pk.img"
python3 -I -c "
import sys
d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(b'\0'*0x400000+d)" "$W/image-packed.bin" "$TMP/pk/4242/mem"
KMEM_PROC="$TMP/pk" run K dump "$o"; expect_rc 4 "packed process"
[ ! -e "$o" ] || fail "packed process refusal left a file behind"
pass
