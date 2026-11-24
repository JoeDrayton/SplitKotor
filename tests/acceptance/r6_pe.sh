#!/usr/bin/env bash
# R6: out/dinput8.dll is a PE32 i386 DLL exporting DirectInput8Create, and build.sh checks that itself.
. "$(dirname "$0")/_lib.sh"
need_real_msvc
d="$(mkcopy r6)" || exit 1
run_build "$d" "$REAL_MSVC"
[ $RC -eq 0 ] || fail "build exited $RC: $OUT"
dump="$(objdump -p "$d/out/dinput8.dll")"
printf '%s\n' "$dump" | grep -q 'file format pei-i386' || fail "not pei-i386"
printf '%s\n' "$dump" | grep -qw 'DLL' || fail "DLL characteristic not set"
printf '%s\n' "$dump" | grep -qw 'DirectInput8Create' || fail "export DirectInput8Create missing"
good="$TMP/good.dll"; cp "$d/out/dinput8.dll" "$good"

# build.sh must reject bad images itself: a fake cl writes each one.
mutate() { python3 -I - "$good" "$1" "$2" <<'PY'
import struct, sys
src, mode, dst = sys.argv[1:4]
b = bytearray(open(src, "rb").read())
pe = struct.unpack_from("<I", b, 0x3C)[0]
if mode == "x64":
    struct.pack_into("<H", b, pe + 4, 0x8664)
elif mode == "noexport":
    struct.pack_into("<II", b, pe + 24 + 96, 0, 0)
elif mode == "notdll":
    ch = struct.unpack_from("<H", b, pe + 22)[0]
    struct.pack_into("<H", b, pe + 22, ch & ~0x2000)
elif mode == "text":
    b = bytearray(b"this is not a PE file\n")
open(dst, "wb").write(b)
PY
}
for mode in text x64 notdll noexport; do
  mutate "$mode" "$TMP/bad.$mode"
  stub="$(fake_msvc "bad.$mode" "for a in \"\$@\"; do case \$a in /Fe:*) o=\${a#/Fe:};; esac; done; mkdir -p \"\$(dirname \"\$o\")\"; cp '$TMP/bad.$mode' \"\$o\"")"
  c="$(mkcopy "bad.$mode")" || exit 1
  run_build "$c" "$stub"
  [ $RC -ne 0 ] || fail "$mode image accepted (exit 0)"
  printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "$mode image: BUILD OK printed"
done
# Header-level checks: a fake objdump on PATH prints the real headers, altered one field at a time,
# so each of build.sh's checks is tested on its own (a real x64 image would also trip the others).
canned() { printf '%s\n' "$dump" | sed "$1" > "$TMP/headers.$2"; }
canned 's/file format pei-i386/file format pei-x86-64/' x64
canned 's/DirectInput8Create/XDirectInput8Create/' similar
canned 's/DirectInput8Create/DirectInput8Creat/' prefix
for h in x64 similar prefix; do
  mkdir -p "$TMP/fakebin.$h"
  printf '#!/usr/bin/env bash\ncat "%s"\n' "$TMP/headers.$h" > "$TMP/fakebin.$h/objdump"; chmod +x "$TMP/fakebin.$h/objdump"
  c="$(mkcopy "hdr.$h")" || exit 1
  OUT="$(cd "$c" && PATH="$TMP/fakebin.$h:$PATH" MSVC_WINE_DIR="$REAL_MSVC" bash ./build.sh 2>&1)"; RC=$?
  [ $RC -ne 0 ] || fail "$h headers accepted (exit 0)"
  printf '%s\n' "$OUT" | grep -q 'BUILD OK' && fail "$h headers: BUILD OK printed"
  [ ! -e "$c/out/dinput8.dll" ] || fail "$h headers: rejected DLL left in out/"
done
pass
