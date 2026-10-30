#!/usr/bin/env python3
"""ilmine.py — mine .NET IL from TurboHUD's engine: methods, calls, int
constants (the offset tables), and string literals. Obfuscated names are fine:
we correlate by call structure, not names."""
import sys
import json
import struct

import dnfile
from dncil.cil.body import CilMethodBody
from dncil.cil.body.reader import CilMethodBodyReaderBase
from dncil.cil.error import MethodBodyFormatError

ASM = r"C:\Users\arne\Downloads\For Diablo 3-20251206T123242Z-3-001\For Diablo 3\TurboHUD_LightningMOD_For_D3\TurboHUD\TurboHUD.exe"


class PeReader(CilMethodBodyReaderBase):
    """Reads method IL from raw file bytes; tell/seek use FILE offsets
    (matching dncil's reference implementation)."""

    def __init__(self, raw, off, rva):
        self.raw = raw
        self.off = off
        self.base_off = off
        self.base_rva = rva

    def read(self, n):
        data = self.raw[self.off:self.off + n]
        self.off += len(data)
        return bytes(data)

    def tell(self):
        return self.off

    def seek(self, rva):
        # body parser may seek relative RVAs; convert via the method base
        self.off = self.base_off + (rva - self.base_rva)
        return self.off


def name_of(row, attr):
    try:
        v = getattr(row, attr)
        return v.value if v is not None else ""
    except Exception:
        return ""


def valid_body_header(raw, off):
    """Anti-tamper guard: many MethodDef RVAs point at garbage (fake fat
    headers with absurd code sizes). Accept only sane tiny/fat headers and
    return (kind, code_size, code_off) or None."""
    if off + 12 > len(raw):
        return None
    b = raw[off]
    if (b & 3) == 2:  # tiny format
        size = (b >> 2) & 0x3F
        if off + 1 + size > len(raw):
            return None
        return ("tiny", size, off + 1)
    import struct as _s
    flags_size = _s.unpack_from("<H", raw, off)[0]
    if (flags_size & 3) != 3:
        return None
    if ((flags_size >> 12) & 0xF) != 3:  # header size must be 3 dwords = 12
        return None
    maxstack, codesize, localssig = _s.unpack_from("<HII", raw, off + 2)
    if codesize == 0 or codesize > 0x100000:
        return None
    if off + 12 + codesize > len(raw):
        return None
    return ("fat", codesize, off + 12)


def build_index(asm=ASM):
    pe = dnfile.dnPE(asm)

    # method rid -> owning type display name
    type_of_method = {}
    for t in pe.net.mdtables.TypeDef:
        ns, nm = name_of(t, "TypeNamespace"), name_of(t, "TypeName")
        full = f"{ns}.{nm}" if ns else (nm or "?")
        for m in t.MethodList:
            if m.row_index:
                type_of_method[m.row_index] = full

    # memberref rid -> "Parent.Name"
    memberref_name = {}
    for i, row in enumerate(pe.net.mdtables.MemberRef, start=1):
        try:
            parent = ""
            if row.Class is not None and row.Class.row is not None:
                prow = row.Class.row
                parent = name_of(prow, "TypeName") or name_of(prow, "Name") or "?"
            memberref_name[i] = f"{parent}.{name_of(row, 'Name')}"
        except Exception:
            memberref_name[i] = "?"

    # methoddef rid -> name
    methoddef_name = {}
    for i, row in enumerate(pe.net.mdtables.MethodDef, start=1):
        methoddef_name[i] = name_of(row, "Name")

    # pinvoke target rids (MethodDef rows with ImplMap entries)
    pinvoke_rids = set()
    if pe.net.mdtables.ImplMap:
        for row in pe.net.mdtables.ImplMap:
            mf = row.MemberForwarded
            if mf is not None and mf.table is not None and mf.table.name == "MethodDef" and mf.row_index:
                pinvoke_rids.add(mf.row_index)

    methods = []
    skipped = 0
    raw = open(asm, "rb").read()
    for rid, row in enumerate(pe.net.mdtables.MethodDef, start=1):
        if not row.Rva:
            continue
        if rid % 2000 == 0:
            print(f"  ...method {rid} (skipped {skipped} bad bodies)", file=sys.stderr)
        entry = {"rid": rid, "type": type_of_method.get(rid, "?"), "name": methoddef_name.get(rid, "?"),
                 "pinvoke": rid in pinvoke_rids, "calls": [], "consts": [], "strs": []}
        try:
            off = pe.get_offset_from_rva(row.Rva)
            hdr = valid_body_header(raw, off)
            if hdr is None:
                skipped += 1
                continue
            reader = PeReader(raw, hdr[2], row.Rva)
            body = CilMethodBody(reader)
            for insn in body.instructions:
                op = insn.opcode.name
                if op in ("call", "callvirt") and insn.operand is not None:
                    tok = insn.operand
                    tname = getattr(tok, "table_name", None)
                    if tname == "MemberRef" or (hasattr(tok, "rid") and getattr(tok, "table", None) and tok.table.name == "MemberRef"):
                        entry["calls"].append(memberref_name.get(tok.rid, f"mref:{tok.rid}"))
                    elif hasattr(tok, "rid"):
                        t = getattr(getattr(tok, "table", None), "name", "")
                        if t == "MethodDef":
                            entry["calls"].append(f"def:{methoddef_name.get(tok.rid, tok.rid)}")
                elif op.startswith("ldc.i4") and insn.operand is not None:
                    try:
                        v = insn.operand if isinstance(insn.operand, int) else int(insn.operand.value)
                        if v != 0 and v != 1 and abs(v) < 0x10000000:
                            entry["consts"].append(v)
                    except Exception:
                        pass
                elif op == "ldstr" and insn.operand is not None:
                    try:
                        s = pe.net.user_strings.get(insn.operand.rid)
                        if s is not None:
                            entry["strs"].append(str(s.value) if hasattr(s, "value") else str(s))
                    except Exception:
                        pass
        except (MethodBodyFormatError, Exception):
            skipped += 1
            continue
        methods.append(entry)
    print(f"  body-header guard skipped {skipped} methods", file=sys.stderr)
    return methods


if __name__ == "__main__":
    print("indexing IL (may take a minute)...", file=sys.stderr)
    methods = build_index()
    with open(r"C:\Users\arne\Projects\D3Hud\analysis\il_index.json", "w", encoding="utf-8") as f:
        json.dump(methods, f, ensure_ascii=False)
    print(f"indexed {len(methods)} methods with bodies")
