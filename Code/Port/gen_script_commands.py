"""Rewrite the ScriptCommands function-pointer members that use default arguments (VC6-only)
into one-pointer wrapper structs that keep the defaults. Size and layout are unchanged, so the
engine <-> scripts.dll interface stays binary compatible.

    py -3 Code/Port/gen_script_commands.py <original scriptcommands.h> <output scriptcommands.h>

Run for Combat/scriptcommands.h and Scripts/scriptcommands.h (from the unmodified originals).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="latin1").read()

DECL = re.compile(r"(?m)^(?P<indent>[ \t]*)(?P<ret>[^;{}()\n]*?)\(\s*\*\s*(?P<name>\w+)\s*\)\s*\((?P<params>[^;]*)\)\s*;")


def split_params(params):
    out, depth, cur = [], 0, ""
    for ch in params:
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += ch in "(<"
        depth -= ch in ")>"
        cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


count = 0


def rewrite(m):
    global count
    params = split_params(m.group("params"))
    if not any("=" in p for p in params):
        return m.group(0)
    count += 1
    indent, ret, name = m.group("indent"), m.group("ret").strip(), m.group("name")
    plain, with_defaults, args = [], [], []
    for i, p in enumerate(params):
        decl, _, default = p.partition("=")
        decl = decl.strip()
        pname = re.search(r"(\w+)\s*$", decl).group(1)
        plain.append(decl)
        with_defaults.append(p)
        args.append(pname)
    fn = name + "_Fn"
    i2 = indent + "\t"
    lines = [
        "%sstruct %s {\t// PORT: one function pointer that keeps VC6's default arguments" % (indent, fn),
        "%stypedef %s (*Ptr)( %s );" % (i2, ret, ", ".join(plain)),
        "%sPtr fn;" % i2,
        "%s%s operator()( %s ) const { return fn( %s ); }" % (i2, ret, ", ".join(with_defaults), ", ".join(args)),
        "%s%s & operator= (Ptr p) { fn = p; return *this; }" % (i2, fn),
        "%soperator Ptr () const { return fn; }" % i2,
        "%s} %s;" % (indent, name),
    ]
    return "\n".join(lines)


out = DECL.sub(rewrite, text)
note = ("// PORT: members that had default arguments (allowed by VC6 on function pointers, not by\n"
        "// modern C++) are wrapper structs holding the same single function pointer; see\n"
        "// Port/gen_script_commands.py. Layout and the scripts.dll interface are unchanged.\n")
out = out.replace("#ifndef", note + "#ifndef", 1)
open(dst, "w", encoding="latin1", newline="").write(out)
print("%s: %d members wrapped" % (dst, count))
