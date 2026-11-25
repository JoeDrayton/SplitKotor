# kmem: read the running game

`tools/kmem.py` reads KOTOR 1 (steam 103) while it runs under Proton, through `/proc/<pid>/mem` opened read-only.
It never writes to the game and never changes a system setting. Python 3 standard library only.

```
tools/kmem.py pid                                         pid of the running swkotor.exe
tools/kmem.py read <addr> <len>                           bytes at <addr> (hex, 0x optional), printed as hex
tools/kmem.py dump <out>                                  the image, 0x400000 to the end of the exe's mapping
tools/kmem.py verify <list>                               check engine addresses
tools/kmem.py verify --db <kotor1_0_3.db> --names <list>  the same, addresses and sizes from the database
```

`--image <file>`, before the command, reads a dump (based at `0x400000`) instead of the running game.
`KMEM_PROC` replaces `/proc`; it exists for the tests.

| Code | Meaning |
|---|---|
| exit 0 | done; for `verify`, every row is `VERIFIED` |
| exit 1 | `verify`: at least one row is not `VERIFIED` |
| exit 2 | bad arguments or input, unreadable memory, or several games running |
| exit 3 | `KOTOR not running` |
| exit 4 | `dump`: `still packed` (SteamStub has not decrypted the code yet; wait for the main menu) |
| exit 5 | permission denied |

## Permission fix

Yama's `kernel.yama.ptrace_scope` can stop one process reading another's memory. The refusal prints the current
value and the command to run:

- 1 or 2: `sudo sysctl kernel.yama.ptrace_scope=0`, until the next reboot.
- 3: cannot be lowered while running. Set `kernel.yama.ptrace_scope=0` in `/etc/sysctl.d/` and reboot.
- Any value but 3: `sudo setcap cap_sys_ptrace=ep <python>` instead. Every script that interpreter runs then gets
  the capability, so prefer the `sysctl`; undo with `sudo setcap -r <python>`.

## verify

A list has one `address name expected_ret` per line: the address in hex, `expected_ret` as the argument bytes the
callee pops, decimal or `0x` hex. Blank lines and lines starting with `#` are skipped. A `--names` list has one
`Class::Function` per line (`Global::Function` for a free function); the database must list `steam 103`.
`__cdecl` functions expect 0, because the caller pops.

| Result | Meaning |
|---|---|
| `VERIFIED` | a function entry is there and its first `ret` pops the expected bytes |
| `RET-MISMATCH <seen>` | the first `ret` pops `<seen>` bytes; `none` when the function ends in a jump, so it has no `ret` of its own |
| `NOT-CODE` | no function entry there, or its bytes do not decode as code |

An entry is an address that is 16-aligned or follows padding or a `ret`, and whose first byte is one the game's
functions start with. A few compiler-runtime functions are not, and show as `NOT-CODE`. The first `ret` is not always
the only exit: the row reports what was read. The database's sizes are used as they are: one that is not a multiple
of 4 cannot match a real `ret`.

`verify` does not check for the packed state. Run `dump` first: if it exits 0, the code is unpacked.

## Tagging an address VERIFIED in a spike

1. Start the game, wait for the main menu, and run `tools/kmem.py dump <out>` (it must exit 0).
2. Run `tools/kmem.py verify` over the spike's addresses and keep the output as evidence.
3. An address whose row says `VERIFIED` may be tagged `VERIFIED`, citing that row. Any other result stays `INFERRED`;
   for `RET-MISMATCH`, record the bytes seen and read the function before relying on its signature.

`VERIFIED` covers the function entry and its argument bytes. It does not prove that the name fits the function.
