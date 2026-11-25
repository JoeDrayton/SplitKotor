# Builds the synthetic game for the kmem acceptance tests (stdlib only; run as: python3 -I _kmem_build.py <dir>).
# <dir>/image.bin      the exe image, byte 0 = address 0x400000, size 0x1F2000 (to 0x5F2000)
# <dir>/proc/<pid>/... a fake /proc: comm, cmdline, maps (3 file-backed parts, anonymous gap), mem (offset == address)
# <dir>/proc/sys/kernel/yama/ptrace_scope
import os, sys
d = sys.argv[1]
BASE, END = 0x400000, 0x5F2000
STOCK = bytes([0x8B, 0x44, 0x24, 0x04, 0x3D, 0x20, 0x03, 0x00, 0x00])
PATCHED = bytes([0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00])
def put(img, addr, data): img[addr - BASE:addr - BASE + len(data)] = data
def image(sig):
    img = bytearray(END - BASE)
    put(img, 0x401000, bytes.fromhex("123456789abcdef0"))
    put(img, 0x5F0C60, sig)
    put(img, 0x410000, bytes.fromhex("558bec33c05dc3"))                 # ret 0
    put(img, 0x410010, bytes.fromhex("8b442404c20800"))                 # ret 8
    put(img, 0x410020, bytes.fromhex("558becb8c30000005dc20400"))       # C3 inside a mov operand, real ret 4
    put(img, 0x410030, bytes.fromhex("558bec68c2040000" "5dc3"))        # C2 04 00 inside a push operand, real ret 0
    put(img, 0x410040, bytes.fromhex("558bec5dc20c00"))                 # ret 12
    put(img, 0x410060, b"ERROR_STRING_ABC")                             # text
    put(img, 0x410070, bytes.fromhex("e900100000") + b"\xcc" * 11)      # jmp thunk, no ret
    return img
os.makedirs(d, exist_ok=True)
for name, sig in (("image.bin", STOCK), ("image-patched.bin", PATCHED), ("image-packed.bin", bytes(range(0x30, 0x39)))):
    open(os.path.join(d, name), "wb").write(image(sig))
def proc(root, pid, comm, cmdline, mem, exe=True):
    p = os.path.join(root, str(pid)); os.makedirs(p, exist_ok=True)
    open(os.path.join(p, "comm"), "w").write(comm + "\n")
    open(os.path.join(p, "cmdline"), "wb").write(b"\0".join(c.encode() for c in cmdline) + b"\0")
    path = "/games/swkotor/swkotor.exe"
    if exe:
        maps = ("00400000-00500000 r-xp 00000000 08:01 11 %s\n"
                "00500000-00501000 ---p 00000000 00:00 0\n"
                "00501000-005f1000 r--p 00100000 08:01 11 %s\n"
                "005f1000-005f2000 rw-p 001f1000 08:01 11 %s\n") % (path, path, path)
    else:
        maps = ""
    open(os.path.join(p, "maps"), "w").write(maps)
    if mem is not None:
        with open(os.path.join(p, "mem"), "wb") as f:
            f.write(b"\0" * BASE); f.write(mem)
def tree(root, pid, scope="0", image_name="image.bin"):
    os.makedirs(os.path.join(root, "sys/kernel/yama"), exist_ok=True)
    open(os.path.join(root, "sys/kernel/yama/ptrace_scope"), "w").write(scope + "\n")
    proc(root, pid, "swkotor.exe", ["Z:\\games\\swkotor\\swkotor.exe"], open(os.path.join(d, image_name), "rb").read())
tree(os.path.join(d, "proc"), 4242)
proc(os.path.join(d, "proc"), 4300, "bash", ["bash", "-c", "echo swkotor.exe"], None, exe=False)   # decoy: name only in cmdline
