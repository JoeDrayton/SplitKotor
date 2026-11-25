#!/usr/bin/env bash
# verify --db <db> --names <list> builds the list from the steam 103 rows; __cdecl expects 0; the DB is never modified.
. "$(dirname "$0")/_kmem_lib.sh"
# Schema measured from upstream kotor1_0_3.db (LaneDibello/Kotor-Patch-Manager fd639f1), checked by QA with sqlite_master.
python3 -I - "$TMP/ok.db" "$TMP/nover.db" <<'P'
import sqlite3, sys
def mk(path, versions):
    c = sqlite3.connect(path)
    c.execute("create table functions(id integer primary key autoincrement, class_name text not null, function_name text not null,"
              " address integer not null, notes text, calling_convention text, param_size_bytes integer, unique(class_name, function_name))")
    c.execute('create table game_version(id integer primary key, sha256_hash text not null unique, game_name text not null,'
              ' version_string text not null, description text, platform text)')
    for i, v in enumerate(versions):
        c.execute("insert into game_version(sha256_hash, game_name, version_string, platform) values (?,?,?,?)", ("H%d" % i, "KOTOR1", v, "Windows"))
    rows = [("A","ret0",0x410000,"__thiscall",0), ("A","ret8",0x410010,"__thiscall",8), ("A","mismatch",0x410040,"__thiscall",4),
            ("A","cdecl12",0x410000,"__cdecl",12), ("A","nosize",0x410010,"__thiscall",None)]
    c.executemany("insert into functions(class_name, function_name, address, calling_convention, param_size_bytes) values (?,?,?,?,?)", rows)
    c.commit(); c.close()
mk(sys.argv[1], ["gog 103", "steam 103", "cd crack 103"]); mk(sys.argv[2], ["gog 103", "cd crack 103"])
P
sum0="$(sha256sum "$TMP/ok.db")"
printf 'A::ret0\nA::ret8\nA::cdecl12\n' > "$TMP/n1.txt"
run K --image "$IMG" verify --db "$TMP/ok.db" --names "$TMP/n1.txt"; expect_rc 0 "db names"
[ "$(printf '%s\n' "$out" | grep -c VERIFIED)" -eq 3 ] || fail "want 3 VERIFIED rows (cdecl12 expects 0): $out"
printf 'A::mismatch\n' > "$TMP/n2.txt"
run K --image "$IMG" verify --db "$TMP/ok.db" --names "$TMP/n2.txt"; expect_rc 1 "db mismatch"
printf '%s\n' "$out" | grep -Eq 'RET-MISMATCH +(12|0x0*[cC])\b' || fail "db mismatch row: $out"
printf 'A::nope\n' > "$TMP/n3.txt"
run K --image "$IMG" verify --db "$TMP/ok.db" --names "$TMP/n3.txt"; expect_rc 2 "unknown name"
case "$err" in *nope*) ;; *) fail "unknown-name message does not name it: $err" ;; esac
printf 'A::nosize\n' > "$TMP/n4.txt"
run K --image "$IMG" verify --db "$TMP/ok.db" --names "$TMP/n4.txt"; expect_rc 2 "row without a size"
case "$err" in *nosize*) ;; *) fail "no-size message does not name it: $err" ;; esac
run K --image "$IMG" verify --db "$TMP/nover.db" --names "$TMP/n1.txt"; expect_rc 2 "no steam 103 row"
case "$err" in *"steam 103"*) ;; *) fail "no-version message: $err" ;; esac
run K --image "$IMG" verify --db "$TMP/missing.db" --names "$TMP/n1.txt"; expect_rc 2 "missing db"
[ "$sum0" = "$(sha256sum "$TMP/ok.db")" ] || fail "the DB file was modified"
pass
