#!/usr/bin/env python3
"""Reads the running KOTOR 1 under Proton, read-only. Usage and exit codes: docs/tools/kmem.md."""
import argparse
import os
import sqlite3
import sys
from pathlib import Path

EXE = 'swkotor.exe'
BASE = 0x400000
SIGNATURE_AT = 0x5F0C60
# The same two byte runs as CodeUnpacked() in src/kotorplus.cpp: stock, and patched by KotorPlus.
UNPACKED = (bytes.fromhex('8b4424043d20030000'), bytes.fromhex('b801000000c20800'))
DB_VERSION = 'steam 103'

EXIT_NOT_VERIFIED, EXIT_INPUT, EXIT_NOT_RUNNING, EXIT_PACKED, EXIT_DENIED = 1, 2, 3, 4, 5

MAX_FUNCTION = 0x10000
# First bytes of the steam 103 function entries, measured in the running game.
ENTRY_OPCODES = frozenset(bytes.fromhex(
    '0f 32 33 51 52 53 55 56 57 64 66 68 6a 80 81 83 85 8a 8b 8d a0 a1 b0 b8 b9'
    'c2 c3 c6 c7 d9 db dd e8 e9 f6 fe ff'))
# Opcodes compiled 32-bit user code does not use; meeting one means the bytes are not code.
NOT_USED = frozenset(bytes.fromhex(
    '06 07 0e 16 17 1e 1f 27 2f 37 3f 60 61 62 63 67 6c 6d 6e 6f 82 9a c4 c5 ca cb cd ce cf d4 d5 d6'
    'e4 e5 e6 e7 ea ec ed ee ef f1 f4 fa fb'))
PREFIXES = frozenset(bytes.fromhex('26 2e 36 3e 64 65 66 f0 f2 f3'))
MODRM = frozenset([op for op in range(0x40) if op & 7 < 4]
                  + [0x69, 0x6B, 0xC0, 0xC1, 0xC6, 0xC7, 0xF6, 0xF7, 0xFE, 0xFF]
                  + [*range(0x80, 0x90), *range(0xD0, 0xD4), *range(0xD8, 0xE0)])
IMM8 = frozenset([op for op in range(0x40) if op & 7 == 4]
                 + [0x6A, 0x6B, 0x80, 0x83, 0xA8, 0xC0, 0xC1, 0xC6, 0xEB]
                 + [*range(0x70, 0x80), *range(0xB0, 0xB8), *range(0xE0, 0xE4)])
IMM_OPERAND = frozenset([op for op in range(0x40) if op & 7 == 5]
                        + [0x68, 0x69, 0x81, 0xA9, 0xC7, *range(0xB8, 0xC0)])
IMM32 = frozenset([0xA0, 0xA1, 0xA2, 0xA3, 0xE8, 0xE9])
NO_MODRM_0F = frozenset([0x05, 0x06, 0x08, 0x09, 0x0B, 0x77, 0xA0, 0xA1, 0xA2, 0xA8, 0xA9, 0xAA,
                         *range(0x30, 0x38), *range(0x80, 0x90), *range(0xC8, 0xD0)])
IMM8_0F = frozenset([0x3A, 0x70, 0x71, 0x72, 0x73, 0xA4, 0xAC, 0xBA, 0xC2, 0xC4, 0xC5, 0xC6])


def fail(code, message):
    print(message, file=sys.stderr)
    sys.exit(code)


def proc_root():
    return Path(os.environ.get('KMEM_PROC', '/proc'))


def fail_denied(path):
    try:
        scope = (proc_root() / 'sys/kernel/yama/ptrace_scope').read_text().strip()
    except OSError:
        scope = 'unknown'
    lines = [f'permission denied: {path}', f'kernel.yama.ptrace_scope is {scope}']
    if scope == '3':
        lines.append('3 cannot be lowered while running: set kernel.yama.ptrace_scope=0 in /etc/sysctl.d/ and reboot')
    elif scope in ('1', '2'):
        lines.append('fix, until the next reboot: sudo sysctl kernel.yama.ptrace_scope=0')
    if scope != '3':
        lines.append(f'or: sudo setcap cap_sys_ptrace=ep {os.path.realpath(sys.executable)}')
    fail(EXIT_DENIED, '\n'.join(lines))


def read_maps(pid):
    """[(start, end, readable, path)] from /proc/<pid>/maps."""
    rows = []
    for line in (proc_root() / str(pid) / 'maps').read_text(errors='surrogateescape').splitlines():
        fields = line.split(None, 5)
        start, end = (int(part, 16) for part in fields[0].split('-'))
        rows.append((start, end, fields[1].startswith('r'), fields[5] if len(fields) == 6 else ''))
    return rows


def exe_rows(maps):
    return [row for row in maps if row[3].rpartition('/')[2].lower() == EXE]


def find_game():
    """(pid, maps). Matched on the mapped exe: command lines that only mention swkotor.exe are common."""
    games = []
    for entry in proc_root().iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if (entry / 'comm').read_text(errors='replace').strip().lower() != EXE:
                continue
            maps = read_maps(entry.name)
        except PermissionError as error:
            fail_denied(error.filename)
        except (FileNotFoundError, ProcessLookupError):
            continue
        if exe_rows(maps):
            games.append((int(entry.name), maps))
    if not games:
        fail(EXIT_NOT_RUNNING, 'KOTOR not running')
    if len(games) > 1:
        fail(EXIT_INPUT, f'several games running: {", ".join(str(pid) for pid, _ in sorted(games))}')
    return games[0]


def open_memory(image):
    """(read, end): read(address, length) gives bytes or None; end is the end of the exe's image."""
    if image:
        path, offset = image, BASE
        try:
            end = BASE + os.stat(path).st_size
        except OSError as error:
            fail(EXIT_INPUT, str(error))
        readable = [(BASE, end)]
    else:
        pid, maps = find_game()
        path, offset = proc_root() / str(pid) / 'mem', 0
        rows = exe_rows(maps)
        if rows[0][0] != BASE:
            fail(EXIT_INPUT, f'{EXE} is mapped at {rows[0][0]:#x}, not {BASE:#x}')
        end = rows[-1][1]
        readable = [(start, stop) for start, stop, can_read, _ in maps if can_read]
    try:
        fd = os.open(path, os.O_RDONLY)
    except PermissionError as error:
        if image:
            fail(EXIT_INPUT, str(error))
        fail_denied(path)
    except OSError as error:
        fail(EXIT_INPUT, str(error))

    def read(address, length):
        parts, at = [], address
        while at < address + length:
            stop = next((stop for start, stop in readable if start <= at < stop), None)
            if stop is None:
                return None
            try:
                part = os.pread(fd, min(stop, address + length) - at, at - offset)
            except PermissionError:
                fail_denied(path)
            except OSError:
                return None
            if not part:
                return None
            parts.append(part)
            at += len(part)
        return b''.join(parts)

    return read, end


def modrm_length(code, at):
    """Bytes of a 32-bit ModRM operand at `at`, the ModRM byte included."""
    mod, rm = code[at] >> 6, code[at] & 7
    if mod == 3:
        return 1
    length, base = 1, rm
    if rm == 4:
        length, base = 2, code[at + 1] & 7
    if mod == 1:
        return length + 1
    if mod == 2 or base == 5:
        return length + 4
    return length


def decode(code, at):
    """(opcode, opcode offset, next offset) of the instruction at `at`; None when it is not one."""
    operand = 4
    while code[at] in PREFIXES:
        operand = 2 if code[at] == 0x66 else operand
        at += 1
    op, start = code[at], at
    at += 1
    if op in NOT_USED or (op == 0 and code[at] == 0):
        return None
    if op == 0x0F:
        op2 = code[at]
        at += 1
        if op2 in (0x38, 0x3A):
            at += 1
        if op2 not in NO_MODRM_0F:
            at += modrm_length(code, at)
        at += 4 if 0x80 <= op2 < 0x90 else 1 if op2 in IMM8_0F else 0
        return 0x0F00 | op2, start, at
    modrm = code[at]
    if op in MODRM:
        at += modrm_length(code, at)
    if op in IMM8 or (op == 0xF6 and modrm & 0x38 < 0x10):
        at += 1
    if op in IMM_OPERAND or (op == 0xF7 and modrm & 0x38 < 0x10):
        at += operand
    if op in IMM32:
        at += 4
    if op == 0xC2:
        at += 2
    if op == 0xC8:
        at += 3
    return op, start, at


def first_ret(code):
    """Argument bytes of the first ret, 'none' when the function ends without one, None when not code."""
    at = furthest = 0
    try:
        while at < len(code):
            step = decode(code, at)
            if step is None:
                return None
            op, start, at = step
            if op == 0xC3:
                return 0
            if op == 0xC2:
                return int.from_bytes(code[start + 1:start + 3], 'little') if at <= len(code) else None
            if 0x70 <= op < 0x80 or op in (0xE0, 0xE1, 0xE2, 0xE3, 0xEB):
                furthest = max(furthest, at + int.from_bytes(code[at - 1:at], 'little', signed=True))
            elif 0x0F80 <= op < 0x0F90 or (op == 0xE9 and code[at] not in (0x90, 0xCC)):
                # Padding after a jmp ends the function: the jump is a tail call, so its target is another one.
                target = at + int.from_bytes(code[at - 4:at], 'little', signed=True)
                if target < MAX_FUNCTION:
                    furthest = max(furthest, target)
            # A jump table (ff /4 with a SIB byte) is a switch, whose cases follow it.
            indirect_jump = op == 0xFF and code[start + 1] & 0x38 == 0x20 and code[start + 1] & 7 != 4
            if op == 0xCC or ((op in (0xE9, 0xEB) or indirect_jump) and furthest < at):
                return 'none'
    except IndexError:
        pass
    return None


def at_entry_boundary(read, address):
    if address % 16 == 0:
        return True
    before = read(address - 3, 3) or b'\0\0\0'
    return before[2] in (0x90, 0xCC, 0xC3) or before[0] == 0xC2


def check(read, end, address, expected):
    if not BASE <= address < end or not at_entry_boundary(read, address):
        return 'NOT-CODE'
    code = read(address, min(MAX_FUNCTION, end - address))
    for size in (0x1000, 0x100, 0x10):
        code = code or read(address, min(size, end - address))
    if not code or code[0] not in ENTRY_OPCODES:
        return 'NOT-CODE'
    seen = first_ret(code)
    if seen is None:
        return 'NOT-CODE'
    return 'VERIFIED' if seen == expected else f'RET-MISMATCH {seen}'


def content_lines(path):
    try:
        with open(path, encoding='utf-8') as file:
            lines = [line.strip() for line in file]
    except (OSError, UnicodeDecodeError) as error:
        fail(EXIT_INPUT, f'{path}: {error}')
    lines = [line for line in lines if line and not line.startswith('#')]
    if not lines:
        fail(EXIT_INPUT, f'{path}: nothing to verify')
    return lines


def rows_from_list(path):
    rows = []
    for line in content_lines(path):
        try:
            address, name, expected = line.split()
            row = int(address, 16), name, int(expected, 0)
        except ValueError:
            row = None
        if row is None or row[0] < 0 or row[2] < 0:
            fail(EXIT_INPUT, f'{path}: not "address name expected_ret": {line}')
        rows.append(row)
    return rows


def rows_from_db(db_path, names_path):
    names = content_lines(names_path)
    rows = []
    try:
        db = sqlite3.connect(Path(db_path).absolute().as_uri() + '?mode=ro', uri=True)
        # Function rows carry no version: a database covers every build its game_version table lists.
        if not db.execute('select 1 from game_version where version_string = ?', (DB_VERSION,)).fetchone():
            fail(EXIT_INPUT, f'{db_path}: not a {DB_VERSION} database')
        for name in names:
            class_name, _, function = name.partition('::')
            found = db.execute('select address, calling_convention, param_size_bytes from functions'
                               ' where class_name = ? and function_name = ?', (class_name, function)).fetchone()
            if not found:
                fail(EXIT_INPUT, f'{db_path}: no function {name}')
            address, convention, size = found
            if convention == '__cdecl':
                size = 0
            if size is None:
                fail(EXIT_INPUT, f'{db_path}: no argument size for {name}')
            rows.append((address, name, size))
    except sqlite3.Error as error:
        fail(EXIT_INPUT, f'{db_path}: {error}')
    return rows


def cmd_pid(args):
    print(find_game()[0])


def cmd_read(args):
    read, _ = open_memory(args.image)
    data = read(args.address, args.length)
    if data is None:
        fail(EXIT_INPUT, f'cannot read {args.length} bytes at {args.address:#x}')
    print(data.hex())


def cmd_dump(args):
    read, end = open_memory(args.image)
    if not any(read(SIGNATURE_AT, len(expected)) == expected for expected in UNPACKED):
        fail(EXIT_PACKED, 'still packed')
    page = 0x1000
    image = b''.join(read(at, min(page, end - at)) or bytes(min(page, end - at)) for at in range(BASE, end, page))
    try:
        Path(args.out).write_bytes(image)
    except OSError as error:
        fail(EXIT_INPUT, str(error))


def cmd_verify(args):
    rows = rows_from_list(args.list) if args.list else rows_from_db(args.db, args.names)
    read, end = open_memory(args.image)
    results = [check(read, end, address, expected) for address, _, expected in rows]
    for (address, name, _), result in zip(rows, results):
        print(f'{address:#010x} {name} {result}')
    if any(result != 'VERIFIED' for result in results):
        sys.exit(EXIT_NOT_VERIFIED)


def hex_address(text):
    value = int(text, 16)
    if value < 0:
        raise ValueError(text)
    return value


def byte_count(text):
    value = int(text, 0)
    if value < 0:
        raise ValueError(text)
    return value


def parse_args():
    parser = argparse.ArgumentParser(prog='kmem.py', description=__doc__)
    parser.add_argument('--image', help=f'read this image file, based at {BASE:#x}, instead of the running game')
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('pid').set_defaults(run=cmd_pid)
    read = commands.add_parser('read')
    read.add_argument('address', type=hex_address)
    read.add_argument('length', type=byte_count)
    read.set_defaults(run=cmd_read)
    dump = commands.add_parser('dump')
    dump.add_argument('out')
    dump.set_defaults(run=cmd_dump)
    verify = commands.add_parser('verify')
    verify.add_argument('list', nargs='?')
    verify.add_argument('--db')
    verify.add_argument('--names')
    verify.set_defaults(run=cmd_verify)
    args = parser.parse_args()
    if args.command == 'verify' and (bool(args.list), bool(args.db), bool(args.names)) not in (
            (True, False, False), (False, True, True)):
        verify.error('give either <list> or --db with --names')
    return args


if __name__ == '__main__':
    arguments = parse_args()
    arguments.run(arguments)
