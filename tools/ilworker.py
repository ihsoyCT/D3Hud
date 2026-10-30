#!/usr/bin/env python3
"""ilworker.py — parse methods [start, end) and stream one JSON line per
method. The driver kills this process if the stream stalls (wedged method)."""
import json
import sys

sys.path.insert(0, r"C:\Users\arne\Projects\D3Hud\tools")
import ilmine  # noqa: E402
import dnfile  # noqa: E402
from dncil.cil.body import CilMethodBody  # noqa: E402


def main(start, end):
    pe = dnfile.dnPE(ilmine.ASM)
    raw = open(ilmine.ASM, "rb").read()

    type_of_method = {}
    for t in pe.net.mdtables.TypeDef:
        ns = ilmine.name_of(t, "TypeNamespace")
        nm = ilmine.name_of(t, "TypeName")
        full = f"{ns}.{nm}" if ns else (nm or "?")
        for m in t.MethodList:
            if m.row_index:
                type_of_method[m.row_index] = full

    memberref_name = {}
    for i, row in enumerate(pe.net.mdtables.MemberRef, start=1):
        try:
            parent = ""
            if row.Class is not None and row.Class.row is not None:
                prow = row.Class.row
                parent = ilmine.name_of(prow, "TypeName") or ilmine.name_of(prow, "Name") or "?"
            memberref_name[i] = f"{parent}.{ilmine.name_of(row, 'Name')}"
        except Exception:
            memberref_name[i] = "?"

    methoddef_name = {}
    for i, row in enumerate(pe.net.mdtables.MethodDef, start=1):
        methoddef_name[i] = ilmine.name_of(row, "Name")

    pinvoke_rids = set()
    if pe.net.mdtables.ImplMap:
        for row in pe.net.mdtables.ImplMap:
            mf = row.MemberForwarded
            if mf is not None and mf.table is not None and mf.table.name == "MethodDef" and mf.row_index:
                pinvoke_rids.add(mf.row_index)

    for rid, row in enumerate(pe.net.mdtables.MethodDef, start=1):
        if rid < start or rid >= end:
            continue
        if not row.Rva:
            continue
        entry = {"rid": rid, "type": type_of_method.get(rid, "?"),
                 "name": methoddef_name.get(rid, "?"), "pinvoke": rid in pinvoke_rids,
                 "calls": [], "consts": [], "strs": []}
        try:
            off = pe.get_offset_from_rva(row.Rva)
            hdr = ilmine.valid_body_header(raw, off)
            if hdr is not None:
                reader = ilmine.PeReader(raw, hdr[2], row.Rva)
                body = CilMethodBody(reader)
                for insn in body.instructions:
                    op = insn.opcode.name
                    if op in ("call", "callvirt") and insn.operand is not None:
                        tok = insn.operand
                        try:
                            if tok.table is not None and tok.table.name == "MemberRef":
                                entry["calls"].append(memberref_name.get(tok.rid, f"mref:{tok.rid}"))
                            elif tok.table is not None and tok.table.name == "MethodDef":
                                entry["calls"].append(f"def:{methoddef_name.get(tok.rid, tok.rid)}")
                        except Exception:
                            pass
                    elif op.startswith("ldc.i4") and insn.operand is not None:
                        try:
                            v = insn.operand if isinstance(insn.operand, int) else int(insn.operand.value)
                            if v not in (0, 1) and abs(v) < 0x10000000:
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
        except Exception:
            pass
        print(json.dumps(entry, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main(int(sys.argv[1]), int(sys.argv[2]))
