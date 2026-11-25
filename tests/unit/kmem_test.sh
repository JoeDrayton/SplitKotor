# Tests for tools/kmem.py, run against a synthetic image (--image) or a fake /proc tree (KMEM_PROC).
_fail() { echo "expected: $1"; echo "actual:   $2"; exit 1; }
_eq() { [ "$1" = "$2" ] || _fail "$1" "$2"; }
_has() { [[ "$2" == *"$1"* ]] || _fail "contains: $1" "$2"; }

STOCK=8b4424043d20030000
PATCHED=b801000000c20800

_setup() {
    T="$(mktemp -d)"
    trap 'chmod -R u+rw "$T"; rm -rf "$T"' EXIT
    IMG="$T/image.bin"
    truncate -s $((0x1f2000)) "$IMG"
    _poke 400000 4d5a
    _poke 5f0c60 "$STOCK"
    _poke 401000 8b442404c3              # mov eax,[esp+4]; ret
    _poke 401010 568bf15ec20800          # push esi; mov esi,ecx; pop esi; ret 8
    _poke 401020 b8c3c20400c20c00        # mov eax,0x4c2c3; ret 12: C3 and C2 inside the operand
    _poke 401030 e9cbffffff909090909090909090909090c3  # jmp 0x401000, padding, then another function's ret
    _poke 401050 85c07401c3c20400        # test eax,eax; je +1; ret; ret 4
    _poke 401060 "$(printf 'Hello, world' | od -An -tx1 | tr -d ' \n')"
    export KMEM_PROC="$T/proc"
    mkdir -p "$KMEM_PROC/sys/kernel/yama"
    echo 1 >"$KMEM_PROC/sys/kernel/yama/ptrace_scope"
}

_poke() { # _poke <address hex> <bytes hex>
    printf "$(sed 's/../\\x&/g' <<<"$2")" | dd of="$IMG" bs=1 seek=$((0x$1 - 0x400000)) conv=notrunc status=none
}

_game() { # _game <pid>: a fake running game whose memory holds the image at 0x400000
    local dir="$KMEM_PROC/$1" exe=/games/swkotor/swkotor.exe
    mkdir -p "$dir"
    echo swkotor.exe >"$dir/comm"
    printf '%s\n' \
        "00340000-00400000 ---p 00000000 00:00 0 " \
        "00400000-0058d000 r-xp 00000000 00:24 7 $exe" \
        "0058d000-005e0000 rwxp 00000000 00:00 0 " \
        "005e0000-005f2000 r-xp 001e0000 00:24 7 $exe" \
        "21100000-21101000 r-xp 00000000 00:24 8 /games/swkotor/Mss32.dll" >"$dir/maps"
    dd if="$IMG" of="$dir/mem" bs=4096 seek=1024 conv=sparse,notrunc status=none
    truncate -s $((0x5f2000)) "$dir/mem"
}

_run() { # sets OUT, ERR and RC
    RC=0
    OUT="$(python3 tools/kmem.py "$@" 2>"$T/err")" || RC=$?
    ERR="$(cat "$T/err")"
}

_db() { # _db <version string>
    DB="$T/k.db"
    python3 -I - "$DB" "$1" <<'PY'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
c.execute('create table game_version (id integer primary key, sha256_hash text, game_name text, version_string text)')
c.execute("insert into game_version values (1, 'x', 'KOTOR1', ?)", (sys.argv[2],))
c.execute('create table functions (class_name text, function_name text, address integer,'
          ' calling_convention text, param_size_bytes integer)')
c.executemany('insert into functions values (?,?,?,?,?)', [
    ('CFoo', 'Set', 0x401010, '__thiscall', 8), ('Global', 'Print', 0x401000, '__cdecl', 4),
    ('CFoo', 'Wrong', 0x401010, '__thiscall', 4), ('CFoo', 'Unsized', 0x401000, None, None)])
c.commit()
PY
}

pid_prints_game_pid() { _setup; _game 4242; _run pid; _eq "0|4242" "$RC|$OUT"; }

pid_none_exits_3() { _setup; _run pid; _eq "3|KOTOR not running" "$RC|$ERR$OUT"; }

pid_ignores_cmdline_match() {
    _setup; mkdir "$KMEM_PROC/77"; echo bash >"$KMEM_PROC/77/comm"
    printf 'bash\0S:\\swkotor.exe\0' >"$KMEM_PROC/77/cmdline"; : >"$KMEM_PROC/77/maps"
    _run pid; _eq 3 "$RC"
}

pid_ignores_process_without_the_exe_mapped() {
    _setup; mkdir "$KMEM_PROC/77"; echo swkotor.exe >"$KMEM_PROC/77/comm"
    echo "00400000-00500000 r-xp 00000000 00:24 7 /usr/bin/bash" >"$KMEM_PROC/77/maps"
    _run pid; _eq 3 "$RC"
}

pid_two_games_exits_2() {
    _setup; _game 41; _game 42; _run pid
    _eq "2|" "$RC|$OUT"; _has "41, 42" "$ERR"
}

pid_skips_vanished_process() { _setup; mkdir "$KMEM_PROC/9"; _game 42; _run pid; _eq "0|42" "$RC|$OUT"; }

exe_at_another_base_exits_2() {
    _setup; _game 42; sed -i 's/^00400000-/00410000-/' "$KMEM_PROC/42/maps"
    _run read 401000 1; _eq 2 "$RC"; _has "0x400000" "$ERR"
}

read_prints_hex() {
    _setup; _game 42; _run read 0x401000 5; _eq "0|8b442404c3" "$RC|$OUT"
    _run read 58cfff 2; _eq "0|0000" "$RC|$OUT"
}

read_unmapped_exits_2() {
    _setup; _game 42
    _run read 10000000 4; _eq "2|" "$RC|$OUT"; _has "0x10000000" "$ERR"
    _run read 3ff000 4; _eq 2 "$RC"
}

read_failure_inside_a_mapping_exits_2() {
    _setup; _game 42; _run read 21100000 4; _eq "2|" "$RC|$OUT"
}

read_boundaries() {
    _setup
    _run --image "$IMG" read 401000 0; _eq "0|" "$RC|$OUT"
    _run --image "$IMG" read 5f1fff 1; _eq "0|00" "$RC|$OUT"
    _run --image "$IMG" read 5f1fff 2; _eq 2 "$RC"
    _run --image "$IMG" read 3fffff 1; _eq 2 "$RC"
    _run --image "$IMG" read zz 1; _eq 2 "$RC"
    _run --image "$IMG" read 401000 -- -1; _eq 2 "$RC"
}

missing_image_exits_2() { _setup; _run --image "$T/none" read 401000 1; _eq 2 "$RC"; _has "$T/none" "$ERR"; }

dump_writes_whole_image() { _setup; _game 42; _run dump "$T/out"; _eq 0 "$RC"; cmp "$IMG" "$T/out"; }

dump_packed_exits_4() {
    _setup; _poke 5f0c60 000000000000000000; _game 42
    _run dump "$T/out"; _eq "4|still packed" "$RC|$ERR"; [ ! -e "$T/out" ]
}

dump_one_wrong_signature_byte_is_packed() {
    _setup; _poke 5f0c68 01; _run --image "$IMG" dump "$T/out"; _eq 4 "$RC"
}

dump_image_too_small_for_the_signature_is_packed() {
    _setup; truncate -s 4096 "$IMG"; _run --image "$IMG" dump "$T/out"; _eq 4 "$RC"
}

dump_accepts_patched() {
    _setup; _poke 5f0c60 "$PATCHED"; _run --image "$IMG" dump "$T/out"; _eq 0 "$RC"; cmp "$IMG" "$T/out"
}

dump_spans_gap() {
    _setup; _poke 5a0000 11223344; _game 42
    _run dump "$T/out"; _eq 0 "$RC"; cmp "$IMG" "$T/out"
    sed -i '/^0058d000/d' "$KMEM_PROC/42/maps"; _poke 5a0000 00000000
    _run dump "$T/out"; _eq 0 "$RC"; cmp "$IMG" "$T/out"
}

dump_unwritable_exits_2() { _setup; _run --image "$IMG" dump "$T/none/out"; _eq 2 "$RC"; }

verify_all_verified() {
    _setup; _game 42
    printf '401000 A 0\n0x401010 B 8\n' >"$T/list"
    _run verify "$T/list"
    _eq "0|0x00401000 A VERIFIED"$'\n'"0x00401010 B VERIFIED" "$RC|$OUT"
}

verify_ret_mismatch() {
    _setup; printf '401010 B 4\n401000 A 0\n' >"$T/list"
    _run --image "$IMG" verify "$T/list"
    _eq "1|0x00401010 B RET-MISMATCH 8"$'\n'"0x00401000 A VERIFIED" "$RC|$OUT"
}

verify_not_code() {
    _setup; printf '402000 Zeros 0\n401060 Text 0\n700000 Outside 0\n401001 Inside 0\n' >"$T/list"
    _run --image "$IMG" verify "$T/list"
    _eq 1 "$RC"; _eq 4 "$(grep -c ' NOT-CODE$' <<<"$OUT")"
}

verify_bad_list_exits_2() {
    _setup
    for line in '' '# only a comment' '401000 A' '401000 A x' 'zz A 0' '401000 A 0 extra' '401000 A -4'; do
        printf '%s\n' "$line" >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq "$line:2|" "$line:$RC|$OUT"
    done
    _run --image "$IMG" verify "$T/none"; _eq 2 "$RC"
    _run --image "$IMG" verify; _eq 2 "$RC"
    _run --image "$IMG" verify "$T/list" --db x --names y; _eq 2 "$RC"
    _run --image "$IMG" verify --db x; _eq 2 "$RC"
}

verify_list_formats() {
    _setup; printf '# list\n\n401020 Hex 0xC\n401020 Dec 12  \n' >"$T/list"
    _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"; _eq 2 "$(grep -c VERIFIED <<<"$OUT")"
}

verify_decodes_operands() {
    _setup; printf '401020 A 12\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"
}

verify_reports_the_first_ret() {
    _setup; printf '401050 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"
}

verify_thunk_has_no_ret() {
    _setup; printf '401030 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"
    _eq "1|0x00401030 A RET-MISMATCH none" "$RC|$OUT"
}

verify_code_running_past_the_image_is_not_code() {
    _setup; _poke 5f1ff0 56565656565656565656565656565656
    printf '5f1ff0 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _has NOT-CODE "$OUT"
}

verify_db_names() {
    _setup; _db 'steam 103'; printf '# names\nCFoo::Set\n\n' >"$T/names"
    _run --image "$IMG" verify --db "$DB" --names "$T/names"
    _eq "0|0x00401010 CFoo::Set VERIFIED" "$RC|$OUT"
    echo CFoo::Wrong >"$T/names"; _run --image "$IMG" verify --db "$DB" --names "$T/names"
    _eq "1|0x00401010 CFoo::Wrong RET-MISMATCH 8" "$RC|$OUT"
}

verify_db_errors() {
    _setup; _db 'steam 103'
    for name in CFoo::Nope CFoo::Unsized Set ''; do
        echo "$name" >"$T/names"; _run --image "$IMG" verify --db "$DB" --names "$T/names"
        _eq "$name:2|" "$name:$RC|$OUT"; _has "$name" "$ERR"
    done
    echo CFoo::Set >"$T/names"
    _run --image "$IMG" verify --db "$T/none.db" --names "$T/names"; _eq 2 "$RC"; [ ! -e "$T/none.db" ]
    echo text >"$T/bad.db"; _run --image "$IMG" verify --db "$T/bad.db" --names "$T/names"; _eq 2 "$RC"
    rm "$DB"; _db 'gog 103'; _run --image "$IMG" verify --db "$DB" --names "$T/names"
    _eq 2 "$RC"; _has "steam 103" "$ERR"
}

verify_db_read_only() {
    _setup; _db 'steam 103'; echo CFoo::Set >"$T/names"; chmod 444 "$DB"
    before="$(sha256sum "$DB")"; _run --image "$IMG" verify --db "$DB" --names "$T/names"
    _eq "0|$before" "$RC|$(sha256sum "$DB")"
}

verify_db_cdecl_expects_zero() {
    _setup; _db 'steam 103'; echo Global::Print >"$T/names"
    _run --image "$IMG" verify --db "$DB" --names "$T/names"; _eq 0 "$RC"
}

mem_opened_read_only() {
    _setup; _game 42; chmod 444 "$KMEM_PROC/42/mem"; before="$(sha256sum "$KMEM_PROC/42/mem")"
    cat >"$T/trace.py" <<'PY'
import os, runpy, sys
flags = []
def hook(event, args):
    if event == 'open' and str(args[0]).endswith('/mem'):
        flags.append(args[2] & os.O_ACCMODE)
sys.addaudithook(hook)
sys.argv = sys.argv[1:]
try:
    runpy.run_path(sys.argv[0], run_name='__main__')
finally:
    print(sorted(set(flags)), file=sys.stderr)
PY
    printf '401000 A 0\n' >"$T/list"
    for args in "read 401000 5" "dump $T/out" "verify $T/list"; do
        python3 "$T/trace.py" tools/kmem.py $args >/dev/null 2>"$T/flags"
        _eq "[0]" "$(cat "$T/flags")"
    done
    _eq "$before" "$(sha256sum "$KMEM_PROC/42/mem")"
}

denied_exits_5() {
    _setup; _game 42; chmod 000 "$KMEM_PROC/42/mem"; printf '401000 A 0\n' >"$T/list"
    for args in "read 401000 1" "dump $T/out" "verify $T/list"; do
        _run $args; _eq "5|" "$RC|$OUT"
        _has "ptrace_scope is 1" "$ERR"; _has "sudo sysctl kernel.yama.ptrace_scope=0" "$ERR"
    done
    _eq 1 "$(cat "$KMEM_PROC/sys/kernel/yama/ptrace_scope")"
}

denied_maps_exits_5() { _setup; _game 42; chmod 000 "$KMEM_PROC/42/maps"; _run pid; _eq 5 "$RC"; }

denied_fix_by_scope() {
    _setup; _game 42; chmod 000 "$KMEM_PROC/42/mem"; scope="$KMEM_PROC/sys/kernel/yama/ptrace_scope"
    echo 2 >"$scope"; _run read 401000 1; _has "ptrace_scope is 2" "$ERR"; _has "sudo sysctl kernel.yama.ptrace_scope=0" "$ERR"
    echo 3 >"$scope"; _run read 401000 1; _has "reboot" "$ERR"
    [[ "$ERR" != *"sudo sysctl"* ]]
    echo 0 >"$scope"; _run read 401000 1; _has "setcap cap_sys_ptrace" "$ERR"
    rm "$scope"; _run read 401000 1; _eq 5 "$RC"; _has "ptrace_scope is unknown" "$ERR"
}

image_reader_same_path() {
    _setup; _game 42; printf '401000 A 0\n401010 B 4\n' >"$T/list"
    for args in "read 401000 20" "verify $T/list" "dump $T/out"; do
        _run $args; live="$RC|$OUT|$(sha256sum "$T/out" 2>/dev/null || true)"; rm -f "$T/out"
        mv "$KMEM_PROC/42" "$T/42"; _run --image "$IMG" $args; mv "$T/42" "$KMEM_PROC/42"
        _eq "$live" "$RC|$OUT|$(sha256sum "$T/out" 2>/dev/null || true)"; rm -f "$T/out"
    done
}

verify_branch_over_a_tail_jump_reaches_the_ret() {
    _setup; _poke 401080 85c07402ff22c20400      # test eax,eax; je +2; jmp [edx]; ret 4
    printf '401080 A 4\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"
}

verify_forward_thunk_has_no_ret() {
    _setup; _poke 4010a0 e91b0000009090909090909090909090c3   # jmp 0x4010c0, padding, another function's ret
    printf '4010a0 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"
    _eq "1|0x004010a0 A RET-MISMATCH none" "$RC|$OUT"
}

verify_jump_into_the_loop_condition_reaches_the_ret() {
    _setup; _poke 4010e0 33c0eb01904875fdc3      # xor eax,eax; jmp +1; nop; dec eax; jne back; ret
    printf '4010e0 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"
}

verify_switch_jump_table_reaches_the_ret() {
    _setup; _poke 401100 ff248500204000c20800    # jmp [eax*4+0x402000]; ret 8
    printf '401100 A 8\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _eq 0 "$RC"
}

verify_unlikely_first_byte_is_not_code() {
    _setup; _poke 401120 40c3                    # inc eax; ret: decodes, but no game function starts so
    printf '401120 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _has NOT-CODE "$OUT"
}

verify_zeros_before_a_ret_are_not_code() {
    _setup; _poke 401140 560000c3                # push esi; then zeroed memory
    printf '401140 A 0\n' >"$T/list"; _run --image "$IMG" verify "$T/list"; _has NOT-CODE "$OUT"
}
