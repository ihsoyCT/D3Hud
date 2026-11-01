"""Reads the game's English text out of the running Diablo III process.

The game keeps its StringLists (the text of items, skills, item powers, ...) as pooled strings in memory: a key such as
"DemonHunter_SmokeScreen_name" followed by its text "Smoke Screen", then the next key, separated only by NUL bytes. This
walks all readable memory, takes every identifier-like token that is directly followed by a text token, and writes
key<TAB>text lines (analysis/game_strings_en.tsv). Nothing is written to the game.

    python tools/extract_game_strings.py [pid] [out.tsv]

The pid defaults to "Diablo III64.exe". Run it with the character in the world (the lists are loaded then).
Findings: analysis/ENGINE_MINING.md, "English names".
"""
import ctypes
import ctypes.wintypes as w
import re
import subprocess
import sys

k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


class MBI(ctypes.Structure):
    _fields_ = [('Base', ctypes.c_void_p), ('AllocBase', ctypes.c_void_p), ('AllocProt', w.DWORD), ('pad', w.DWORD),
                ('Size', ctypes.c_size_t), ('State', w.DWORD), ('Prot', w.DWORD), ('Type', w.DWORD), ('pad2', w.DWORD)]


TOKEN = re.compile(rb'[\x20-\x7e\n\r\t]{2,400}')
KEY = re.compile(rb'^[A-Za-z][A-Za-z0-9_#.\-]{2,120}$')


def find_pid():
    out = subprocess.run(['tasklist', '/fi', 'imagename eq Diablo III64.exe', '/fo', 'csv', '/nh'],
                         capture_output=True, text=True).stdout
    m = re.search(r'"Diablo III64.exe","(\d+)"', out)
    if not m:
        sys.exit('Diablo III64.exe is not running')
    return int(m.group(1))


def main():
    pid = int(sys.argv[1]) if len(sys.argv) > 1 else find_pid()
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'analysis/game_strings_en.tsv'
    h = k.OpenProcess(0x1010, False, pid)
    pairs = {}   # key -> distinct texts: the same key can be in several lists (a title and the name itself)
    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF and k.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base, size = mbi.Base or 0, mbi.Size
        if mbi.State == 0x1000 and not (mbi.Prot & 0x101) and 0 < size < (1 << 31):
            buf = ctypes.create_string_buffer(size)
            got = ctypes.c_size_t()
            if k.ReadProcessMemory(h, ctypes.c_void_p(base), buf, size, ctypes.byref(got)):
                data = buf.raw[:got.value]
                toks = [(m.start(), m.end(), m.group()) for m in TOKEN.finditer(data)]
                for i in range(len(toks) - 1):
                    s0, e0, t0 = toks[i]
                    s1, e1, t1 = toks[i + 1]
                    if not KEY.match(t0) or (b'_' not in t0 and not t0[-1:].isdigit()):
                        continue
                    if data[e0:s1].strip(b'\x00') != b'' or not data[e0:s1]:   # only NULs between key and text
                        continue
                    if KEY.match(t1) and b'_' in t1:                            # another key, not a text
                        continue
                    texts = pairs.setdefault(t0.decode(), [])
                    text = t1.decode('latin-1')
                    if text not in texts and len(texts) < 6:
                        texts.append(text)
        addr = base + size
        if size == 0:
            break
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('key\ttext\n')
        for key in sorted(pairs):
            for text in pairs[key]:
                f.write(key + '\t' + text.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '').replace('\t', ' ') + '\n')
    print(sum(len(v) for v in pairs.values()), 'pairs written to', out_path)


if __name__ == '__main__':
    main()
