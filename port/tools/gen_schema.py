#!/usr/bin/env python3
"""Generate port_type tables (port/src/hsd_endian/schema.h) from C headers via libclang.

Usage:
  gen_schema.py --header H [--header H2 ...] --annotations A.yml --roots R.yml
                [--types T1,T2] [--clang-arg X ...] [--quiet] OUT|-

Every struct reachable from the requested types and from the roots gets a
`const port_type port_T_<Name>`; `port_roots[]` maps public-symbol prefixes to
types. Offsets are computed for wasm32 (the target the walker runs on); the
game's LINT layout asserts prove those equal the GameCube offsets.

annotations.yml (per struct name):
  __opaque__: true                         whole struct left big-endian
  <field>:
    opaque: true                           field left big-endian
    ptr: T                                 pointer target when the header says void*/forward
    ptr_array: T                           pointer to an array of T ...
      len_field: n | len_const: 4 | null_term: true   ... with this length rule
    elem_ptr: T                            inline array of pointers to T
    word: true | word: { ptr: T }          4-byte slot: pointer if relocated, else u32
    union_on: <sibling>  cases: { 0: TypeA, 1: TypeB, 2: ~ }   discriminated union
    ptr_array/elem_ptr targets may also be the scalar helpers u16, u32, u64, f32, f64, ptr, word,
    or "T*" for an array of pointers to T;
    union_on may add mask: 0x30 to compare only those bits of the discriminator;
  roots.yml entries: { prefix|suffix, type, array: null_term | { term_value: N },
                      field_types: { <field>: T } to aim a pointer field at T for this root }
    term_value: 0x83D60                    pointer array ends at the element whose first word is this
    reloc_run: true                        inline array runs while each element's pointers are relocated
  __union__: { disc_offset: 4, cases: {...} }   the struct itself is a union chosen by a word inside it
"""
import argparse
import shutil
import subprocess
import sys

import yaml
from clang import cindex

K = cindex.CursorKind
T = cindex.TypeKind

INT_KINDS = {T.CHAR_U, T.UCHAR, T.CHAR_S, T.SCHAR, T.USHORT, T.SHORT, T.UINT, T.INT, T.ULONG, T.LONG,
             T.ULONGLONG, T.LONGLONG, T.BOOL, T.ENUM, T.CHAR16, T.CHAR32, T.WCHAR}
SCALAR_BY_SIZE = {1: "F_U8", 2: "F_U16", 4: "F_U32", 8: "F_U64"}
HELPER_TYPES = {  # name -> (size, field)
    "__u16": (2, "{F_U16, 0}"), "__u32": (4, "{F_U32, 0}"), "__u64": (8, "{F_U64, 0}"),
    "__f32": (4, "{F_F32, 0}"), "__f64": (8, "{F_F64, 0}"), "__ptr": (4, "{F_PTR, 0, NULL}"),
    "__word": (4, "{F_WORD, 0, NULL}"),
}


def named(field, name):
    """Attach a field name (last member of port_field) to a positional initializer."""
    return field[:-1] + f', .name = "{name}"}}' if name else field


def scalar_kind(t):
    if t.kind == T.FLOAT:
        return "F_F32"
    if t.kind in (T.DOUBLE, T.LONGDOUBLE):
        return "F_F64"
    if t.kind in INT_KINDS:
        return SCALAR_BY_SIZE[t.get_size()]
    return None


def is_anon(decl):
    s = decl.spelling
    return not s or "(" in s or decl.is_anonymous()


class Gen:
    def __init__(self, ann, roots, quiet):
        self.ann, self.roots, self.quiet = ann, roots, quiet
        self.by_name = {}   # struct tag / typedef name -> record cursor
        self.defs = {}      # record USR -> definition cursor (forward declarations share the USR)
        self.emitted = {}   # usr -> C identifier
        self.out = []       # field arrays and helper tables
        self.order = []     # port_type definitions
        self.helpers = set()
        self.variants = {}
        self.warned = set()

    # --- collection ------------------------------------------------------
    def collect(self, tu):
        errors = 0
        for d in tu.diagnostics:
            if d.severity >= cindex.Diagnostic.Error:
                sys.stderr.write(f"gen_schema: {d}\n")
                errors += 1
        if errors:
            raise SystemExit(f"gen_schema: {errors} parse error(s); offsets would be wrong")
        for c in tu.cursor.walk_preorder():
            if c.kind in (K.STRUCT_DECL, K.UNION_DECL) and c.is_definition():
                self.defs.setdefault(c.get_usr(), c)
                if not is_anon(c):
                    self.by_name.setdefault(c.spelling, c)
            elif c.kind == K.TYPEDEF_DECL:
                u = c.underlying_typedef_type.get_canonical()
                if u.kind == T.RECORD:
                    d = self.defs.get(u.get_declaration().get_usr())
                    if d is not None:
                        self.by_name.setdefault(c.spelling, d)

    def definition(self, t):
        """Definition cursor for a canonical RECORD type, or None when only declared."""
        d = t.get_declaration()
        return self.defs.get(d.get_usr()) if d.get_usr() else (d if d.is_definition() else None)

    def warn(self, msg):
        if not self.quiet and msg not in self.warned:
            self.warned.add(msg)
            sys.stderr.write(f"gen_schema: {msg}\n")

    # --- naming ----------------------------------------------------------
    def names_for(self, decl):
        """Every header name (struct tag, typedef) of a record definition."""
        usr = decl.get_usr()
        return [name for name, d in self.by_name.items() if d.get_usr() == usr]

    def record_name(self, decl, hint):
        """C identifier for a record: its typedef name if it has one (HSD_TObjDesc rather
        than the tag _HSD_TObjDesc), else its tag, else a synthesized one."""
        names = self.names_for(decl)
        if not names:
            return hint
        return sorted(names, key=lambda n: (n.startswith("_"), len(n)))[0]

    def annotations_for(self, decl, name):
        """Annotations may be keyed by any of the record's names."""
        for n in [name] + self.names_for(decl):
            a = self.ann.get(n)
            if a:
                return a
        return {}

    def helper(self, name):
        if name not in self.helpers:
            self.helpers.add(name)
            size, field = HELPER_TYPES[name]
            self.out.append(f"static const port_field fields_{name}[] = {{ {field} }};")
            self.order.append(f'const port_type port_T_{name} = {{ "{name}", {size}, fields_{name}, 1 }};')
        return f"&port_T_{name}"

    def ptr_elem_helper(self, target):
        """Element type for an inline array of T*: a 4-byte record holding one followed pointer."""
        name = f"__ptr_{target}"
        if name not in self.helpers:
            self.helpers.add(name)
            self.out.append(f"static const port_field fields_{name}[] = {{ {{F_PTR, 0, &port_T_{target}}} }};")
            self.order.append(f'const port_type port_T_{name} = {{ "{target}*", 4, fields_{name}, 1 }};')
        return f"&port_T_{name}"

    def need_name(self, name):
        """Reference a type by header name (from annotations / roots / --types), or a
        scalar helper: u16, u32, u64, f32, f64, ptr, word."""
        if name == "opaque":  # nothing to convert (raw blobs, pointer-only tables)
            if "__opaque" not in self.helpers:
                self.helpers.add("__opaque")
                self.order.append('const port_type port_T___opaque = { "opaque", 0, NULL, 0 };')
            return "&port_T___opaque"
        if "__" + name in HELPER_TYPES:
            return self.helper("__" + name)
        if name.endswith("*"):
            target = self.need_name(name[:-1].strip())[len("&port_T_"):]
            return self.ptr_elem_helper(target)
        if name not in self.by_name:
            raise SystemExit(f"gen_schema: unknown type '{name}' (not defined in the given headers)")
        return self.need(self.by_name[name], name)

    def need(self, decl, hint):
        usr = decl.get_usr()
        if usr not in self.emitted:
            name = self.record_name(decl, hint)
            self.emitted[usr] = name
            self.emit(decl, name)
        return f"&port_T_{self.emitted[usr]}"

    def variant(self, base, overrides):
        """A copy of `base` with some pointer fields aimed at different types.
        Root symbols that share a struct but not its payload (every fighter's
        ftData, whose ext_attr is that character's own attribute block) need
        this."""
        key = base + "__" + "_".join(f"{f}_{t}" for f, t in sorted(overrides.items()))
        if key in self.variants:
            return self.variants[key]
        decl = self.by_name.get(base)
        if decl is None:
            raise SystemExit(f"gen_schema: unknown type '{base}' in a root override")
        merged = dict(self.annotations_for(decl, base))
        for field, target in overrides.items():
            merged[field] = dict(merged.get(field) or {}, ptr=target)
        self.ann[key] = merged
        self.variants[key] = f"&port_T_{key}"
        self.emit(decl, key)
        return self.variants[key]

    # --- emission --------------------------------------------------------
    def emit(self, decl, name):
        size = decl.type.get_size()
        a = self.annotations_for(decl, name)
        if a.get("__opaque__"):
            self.order.append(f'const port_type port_T_{name} = {{ "{name}", {size}, NULL, 0 }};')
            return
        # get_fields() rather than get_children(): a C11 anonymous struct or
        # union member appears only in the former, as an unnamed field.
        kids = list(decl.type.get_fields())
        if "__union__" in a:
            u = a["__union__"]
            fields = [self.union_desc(name, 0, int(u["disc_offset"]), int(u.get("disc_size", 4)), 1, u.get("mask", 0), u["cases"])]
        elif decl.kind == K.UNION_DECL:
            fields = [self.union_field(decl, name, kids, a)]
        else:
            fields = self.struct_fields(decl, name, kids, a)
        self.out.append(f"static const port_field fields_{name}[] = {{ {', '.join(fields) or '{F_OPAQUE, 0}'} }};")
        self.order.append(f'const port_type port_T_{name} = {{ "{name}", {size}, fields_{name}, {len(fields)} }};')

    def union_desc(self, name, off, disc, size, be, mask, cases):
        vals = ", ".join(f"{int(v)}u" for v in cases)
        tys = ", ".join(self.need_name(t) if t else "NULL" for t in cases.values())
        self.out.append(f"static const uint32_t disc_{name}_{off}[] = {{ {vals} }};")
        self.out.append(f"static const port_type* const disc_t_{name}_{off}[] = {{ {tys} }};")
        return (f"{{F_UNION, {off}, NULL, 0, 0, 0, NULL, 0, {disc}, disc_{name}_{off}, disc_t_{name}_{off}, {len(cases)}, "
                f"{size}, {be}, {int(mask)}u}}")

    def union_field(self, decl, name, kids, a):
        """An unannotated C union: all members must agree on one representation."""
        reps = set()
        target = None
        for f in kids:
            t = f.type.get_canonical()
            if t.kind == T.POINTER:
                reps.add("word")
                pt = t.get_pointee().get_canonical()
                pd = self.definition(pt) if pt.kind == T.RECORD else None
                if pd is not None and target is None:
                    target = self.need(pd, f"{name}__{f.spelling}")
            elif t.kind == T.RECORD:
                reps.add("record")
            else:
                sk = scalar_kind(t)
                reps.add({"F_U8": "u8", "F_U16": "u16", "F_U32": "word", "F_F32": "word",
                          "F_U64": "u64", "F_F64": "u64", None: "other"}[sk])
        if reps <= {"word"}:
            return f"{{F_WORD, 0, {target or 'NULL'}}}"
        if reps == {"u16"}:
            return "{F_U16, 0}"
        if reps == {"u64"}:
            return "{F_U64, 0}"
        if reps == {"u8"}:
            return "{F_U8, 0}"
        self.warn(f"union {name}: members disagree ({', '.join(sorted(reps))}); left opaque — annotate it")
        return "{F_OPAQUE, 0}"

    def struct_fields(self, decl, name, kids, a):
        fields, bits_defs = [], []
        i = 0
        while i < len(kids):
            f = kids[i]
            off = f.get_field_offsetof() // 8
            fa = a.get(f.spelling, {}) or {}
            if f.is_bitfield():
                storage = f.type.get_size()
                unit = (f.get_field_offsetof() // 8) // storage * storage
                widths = []
                while (i < len(kids) and kids[i].is_bitfield()
                       and (kids[i].get_field_offsetof() // 8) // storage * storage == unit):
                    widths.append(kids[i].get_bitfield_width())
                    i += 1
                bits_defs.append(f"static const uint8_t bits_{name}_{unit}[] = {{ {', '.join(map(str, widths))} }};")
                fields.append(named(f"{{F_BITS, {unit}, NULL, 0, 0, {storage}, bits_{name}_{unit}, {len(widths)}}}", f.spelling))
                continue
            i += 1
            # a C11 anonymous member: no name of its own (a *named* field whose
            # type happens to be an anonymous struct is not one)
            anon = not f.spelling or '(anonymous' in f.spelling
            fld = self.field(decl, name, kids, f, off, {} if anon else fa, anon)
            if fld:
                fields.append(fld if anon else named(fld, f.spelling))
        self.out.extend(bits_defs)
        return fields

    def field(self, decl, name, kids, f, off, fa, anon=False):
        member = ('anon%d' % off) if anon else (f.spelling or 'anon')
        if fa.get("opaque"):
            return f"{{F_OPAQUE, {off}}}"
        if "union_on" in fa:
            dk = next(k for k in kids if k.spelling == fa["union_on"])
            disc = dk.get_field_offsetof() // 8
            if disc >= off:
                raise SystemExit(f"gen_schema: {name}.{f.spelling}: union_on field must precede the union")
            return self.union_desc(name, off, disc, dk.type.get_size(), 0, fa.get("mask", 0), fa["cases"])
        if "word" in fa:
            w = fa["word"]
            target = self.need_name(w["ptr"]) if isinstance(w, dict) and "ptr" in w else "NULL"
            return f"{{F_WORD, {off}, {target}}}"
        if "ptr_array" in fa:
            lk, lv = self.len_spec(fa, kids)
            return f"{{F_PTR_ARRAY, {off}, {self.need_name(fa['ptr_array'])}, {lk}, {lv}}}"
        t = f.type.get_canonical()
        if t.kind == T.POINTER:
            if "ptr" in fa:
                return f"{{F_PTR, {off}, {self.need_name(fa['ptr'])}}}"
            pt = t.get_pointee().get_canonical()
            pd = self.definition(pt) if pt.kind == T.RECORD else None
            if pd is not None:
                return f"{{F_PTR, {off}, {self.need(pd, f'{name}__{member}')}}}"
            if pt.kind not in (T.VOID, T.FUNCTIONPROTO, T.FUNCTIONNOPROTO, T.RECORD) and scalar_kind(pt) not in (None, "F_U8"):
                self.warn(f"{name}.{f.spelling}: pointer to {pt.spelling} with unknown length; target left big-endian (add ptr_array)")
            if pt.kind == T.RECORD:
                self.warn(f"{name}.{f.spelling}: pointer to incomplete {pt.spelling}; not followed (add ptr)")
            return f"{{F_PTR, {off}, NULL}}"
        if t.kind in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
            n = 1
            et = t
            while et.kind == T.CONSTANTARRAY:
                n *= et.element_count
                et = et.element_type.get_canonical()
            if t.kind == T.INCOMPLETEARRAY:
                return None  # flexible array member: length unknown, needs an annotation on the parent
            # an inline array may run to a terminator instead of its declared length (list mirrors)
            lk, lv = "LEN_CONST", n
            if fa.get("null_term"):
                lk, lv = "LEN_NULL_TERM", 0
            elif fa.get("reloc_run"):
                lk, lv = "LEN_RELOC_RUN", 0
            elif "term_value" in fa:
                lk, lv = "LEN_TERM_VALUE", f"{int(fa['term_value'])}u"
            if et.kind == T.RECORD:
                return f"{{F_ARRAY, {off}, {self.need(self.definition(et), f'{name}__{member}')}, {lk}, {lv}}}"
            if et.kind == T.POINTER:
                if "elem_ptr" in fa:
                    target = self.need_name(fa["elem_ptr"])[len("&port_T_"):]
                    return f"{{F_ARRAY, {off}, {self.ptr_elem_helper(target)}, {lk}, {lv}}}"
                pt = et.get_pointee().get_canonical()
                pd = self.definition(pt) if pt.kind == T.RECORD else None
                if pd is not None:
                    target = self.need(pd, f"{name}__{f.spelling}")[len("&port_T_"):]
                    return f"{{F_ARRAY, {off}, {self.ptr_elem_helper(target)}, {lk}, {lv}}}"
                return f"{{F_ARRAY, {off}, {self.helper('__ptr')}, {lk}, {lv}}}"
            sk = scalar_kind(et)
            if sk is None:
                raise SystemExit(f"gen_schema: {name}.{f.spelling}: unsupported array element {et.spelling}")
            if sk == "F_U8":
                return None
            return f"{{F_ARRAY, {off}, {self.helper('__' + sk[2:].lower())}, {lk}, {lv}}}"
        if t.kind == T.RECORD:
            d = self.definition(t)
            return f"{{F_STRUCT, {off}, {self.need(d, f'{name}__{member}')}}}"
        sk = scalar_kind(t)
        if sk is None:
            raise SystemExit(f"gen_schema: {name}.{f.spelling}: unsupported type {t.spelling}; add an annotation")
        return None if sk == "F_U8" else f"{{{sk}, {off}}}"

    def len_spec(self, fa, kids):
        if "len_const" in fa:
            return "LEN_CONST", int(fa["len_const"])
        if fa.get("null_term"):
            return "LEN_NULL_TERM", 0
        if "term_value" in fa:
            return "LEN_TERM_VALUE", f"{int(fa['term_value'])}u"
        if fa.get("reloc_run"):
            return "LEN_RELOC_RUN", 0
        k = next(k for k in kids if k.spelling == fa["len_field"])
        return {4: "LEN_FIELD_U32", 2: "LEN_FIELD_U16", 1: "LEN_FIELD_U8"}[k.type.get_size()], k.get_field_offsetof() // 8


def resource_dir_args():
    """libclang does not find its builtin headers (stdint.h, stddef.h) for a
    cross target on its own; point it at the matching clang's resource dir."""
    clang = shutil.which("clang")
    if clang:
        r = subprocess.run([clang, "-print-resource-dir"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            return ["-resource-dir", r.stdout.strip()]
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", action="append", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--roots", required=True)
    ap.add_argument("--types", default="")
    ap.add_argument("--clang-arg", action="append", default=[])
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("out")
    a = ap.parse_args()
    ann = yaml.safe_load(open(a.annotations)) or {}
    roots = yaml.safe_load(open(a.roots)) or []
    g = Gen(ann, roots, a.quiet)
    idx = cindex.Index.create()
    args = ["-xc", "-std=gnu99", "-DTARGET_PC", "-DLINT", "--target=wasm32-unknown-emscripten"] + resource_dir_args() + a.clang_arg
    for h in a.header:
        g.collect(idx.parse(h, args=args, options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES))
    for key in ann:
        if key not in g.by_name and not key.startswith("port_"):
            g.warn(f"annotations.yml: '{key}' is not a type in the headers")
    for t in filter(None, a.types.split(",")):
        g.need_name(t)
    root_refs = []
    for r in roots:
        if "prefix" not in r and "suffix" not in r:
            raise SystemExit(f"gen_schema: root {r} needs a prefix and/or suffix")
        t = g.need_name(r["type"])
        if "field_types" in r:
            t = g.variant(r["type"], r["field_types"])
        if "array" in r:  # the symbol is an inline list of `type`: null_term or term_value:N
            elem = t[len("&port_T_"):]
            spec = r["array"]
            lk, lv = ("LEN_NULL_TERM", "0") if spec == "null_term" else ("LEN_TERM_VALUE", f"{int(spec['term_value'])}u")
            wname = f"__list_{elem}_{lk.lower()}"
            if wname not in g.helpers:
                g.helpers.add(wname)
                g.out.append(f"static const port_field fields_{wname}[] = {{ {{F_ARRAY, 0, {t}, {lk}, {lv}}} }};")
                g.order.append(f'const port_type port_T_{wname} = {{ "{elem}[]", 0, fields_{wname}, 1 }};')
            t = f"&port_T_{wname}"
        root_refs.append((r.get("prefix"), r.get("suffix"), t))
    fwd = [f"extern const port_type port_T_{n};"
           for n in list(g.emitted.values()) + sorted(g.helpers) + sorted(g.variants)]
    body = ["#include <stddef.h>", "#include \"hsd_endian/schema.h\"", "/* GENERATED by port/tools/gen_schema.py; do not edit */"] + fwd + g.out + g.order
    cstr = lambda x: f'"{x}"' if x is not None else "NULL"
    body.append("const port_root port_roots[] = { " + "".join(f"{{ {cstr(p)}, {cstr(sfx)}, {t} }}, " for p, sfx, t in root_refs)
                + "{ NULL, NULL, NULL } };")
    text = "\n".join(body) + "\n"
    if a.out == "-":
        sys.stdout.write(text)
    else:
        with open(a.out, "w") as fh:
            fh.write(text)


if __name__ == "__main__":
    main()
