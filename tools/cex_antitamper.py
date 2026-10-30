"""Static ConfuserEx (normal-mode) anti-tamper remover.

Reads the anti-tamper initializer from a dnSpy decompile of <Module> (-Module-.cs), translates its
key derivation to Python, decrypts the protected section in a COPY of the exe and writes it out.
Nothing from the target is executed.

  python tools/cex_antitamper.py <Module.cs> <in.exe> <out.exe>
"""
import re
import struct
import sys

M = 0xFFFFFFFF


def u32(s):
    return int(s.rstrip("U")) if s != "uint.MaxValue" else M


def parse_routine(src):
    # the routine that walks section headers: starts at GetHINSTANCE, ends at the decrypt loop constant
    start = src.index("Marshal.GetHINSTANCE(module);", src.index("internal unsafe static void"))
    body = src[start:]
    init = dict(re.findall(r"uint (num[4-7]) = (\d+)U;", body[:2000]))
    name_hash = int(re.search(r"if \(num8 == (\d+)U\)", body).group(1))
    a = body.index("uint[] array = new uint[16];")
    expand = body[a:body.index("uint num12", a)]
    mix = body[body.index("uint num12", a):body.index("uint num16 = 64U;", a)]
    step = int(re.search(r"\^ \*\(ptr3\+\+\)\) \+ (\d+)U;", body).group(1))
    return init, name_hash, expand, mix, step


def to_py(block):
    """Translate the C# uint arithmetic block into Python statements with 32-bit masking."""
    out = []
    for stmt in block.split(";"):
        s = " ".join(stmt.split())
        if not s or s.startswith(("uint[]", "for", "{", "}")):
            continue
        s = re.sub(r"^uint ", "", s)
        s = s.replace("uint.MaxValue", str(M))
        s = re.sub(r"(\d+)U\b", r"\1", s)
        m = re.match(r"^(\S+) (\+|\*|\^|\||&)?= (.+)$", s)
        if not m:
            raise ValueError(f"cannot translate: {s!r}")
        lhs, op, rhs = m.groups()
        rhs = rhs.replace("~", "M ^ ")  # 32-bit NOT
        out.append(f"{lhs} = (({lhs}) {op} ({rhs})) & M" if op else f"{lhs} = ({rhs}) & M")
    return "\n".join(out)


def sections(pe):
    peo = struct.unpack_from("<I", pe, 60)[0]
    n = struct.unpack_from("<H", pe, peo + 6)[0]
    opt = struct.unpack_from("<H", pe, peo + 20)[0]
    so = peo + 24 + opt
    out = []
    for i in range(n):
        name1, name2, vsize, va, rawsize, rawptr = struct.unpack_from("<IIIIII", pe, so + i * 40)
        out.append(dict(hash=(name1 * name2) & M, vsize=vsize, va=va, rawsize=rawsize, rawptr=rawptr,
                        name=pe[so + i * 40:so + i * 40 + 8].rstrip(b"\0").decode("latin1"), hdr=so + i * 40))
    return out


def main(mod_cs, exe_in, exe_out):
    src = open(mod_cs, encoding="utf-8", errors="replace").read()
    init, name_hash, expand, mix, step = parse_routine(src)
    pe = bytearray(open(exe_in, "rb").read())
    secs = sections(pe)
    n4, n5, n6, n7 = (int(init[k]) for k in ("num4", "num5", "num6", "num7"))
    enc = None
    for s in secs:
        if s["hash"] == name_hash:
            enc = s
        elif s["hash"] != 0:
            words = struct.unpack_from(f"<{s['rawsize'] >> 2}I", pe, s["rawptr"])
            for w in words:
                t = ((n4 ^ w) + n5 + n6 * n7) & M
                n4, n5, n7 = n5, n7, t
    print("sections:", [(s["name"], hex(s["va"]), s["vsize"]) for s in secs])
    if not enc:
        sys.exit("encrypted section not found")
    print(f"encrypted section: {enc['name']!r} va=0x{enc['va']:X} vsize={enc['vsize']}")

    array, array2 = [0] * 16, [0] * 16
    for j in range(16):
        array[j], array2[j] = n7, n5
        n4 = ((n5 >> 5) | (n5 << 27)) & M
        n5 = ((n6 >> 3) | (n6 << 29)) & M
        n6 = ((n7 >> 7) | (n7 << 25)) & M
        n7 = ((n4 >> 11) | (n4 << 21)) & M
    env = dict(array=array, array2=array2, M=M, num12=0, num13=0, num14=0, num15=0)
    exec(to_py(mix), {}, env)  # our own translation of the derivation arithmetic (no target code)
    key = env["array"]

    off, cnt = enc["rawptr"], enc["vsize"] >> 2
    k = 0
    for i in range(cnt):
        w = struct.unpack_from("<I", pe, off + i * 4)[0]
        d = w ^ key[k & 15]
        key[k & 15] = ((key[k & 15] ^ d) + step) & M
        struct.pack_into("<I", pe, off + i * 4, d)
        k += 1
    open(exe_out, "wb").write(pe)
    print(f"decrypted {cnt} dwords -> {exe_out}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
