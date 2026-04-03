"""
~~ written by claude; apologies in advance for anyone trying to make sense of this ~~

Macintosh C/C++ ABI Demangler

Parses C++ symbols mangled according to the Macintosh C/C++ ABI Standard
Specification (Rev 1.3, Dec 1996, Section 3.4.1) and converts them into
the intermediate representation used by mangle.py, enabling re-mangling
into either Macintosh or Itanium ABI.

The golden reference for the mangling format is what ``mangle.py``'s
``macintosh_mangle()`` produces; the specification PDF is supplementary.

Architecture
~~~~~~~~~~~~
The Macintosh mangling grammar is mostly context-free, except for one
construct: *length-prefixed names* (``<int> <id>``), where the integer
tells the parser how many characters to consume next.  A pure CFG cannot
express this, so we split the work:

  * A **lark grammar** describes the context-free skeleton of a mangled
    symbol (type encodings, qualifiers, function signatures, etc.).  The
    grammar treats length-prefixed names and qualified-name blobs as
    opaque terminal tokens captured by regex.

  * A **lark Transformer** processes the parse tree bottom-up, using a
    small stream-based reader to crack open the length-prefixed portions
    and build the mangle.py IR objects (``SyntaxToken``,
    ``OperatorNamespace``, ``OperatorTemplateArgs``, etc.).

Usage::

    from mac_demangle import mac_demangle, mac_to_itanium, mac_roundtrip

    mac_to_itanium("foo__Fv")            # -> "_Z3foov"
    mac_roundtrip("foo__Fv")             # -> "foo__Fv"  (parse then re-mangle)
    mac_demangle("foo__Fv")              # -> "void foo ( void )"
"""

from __future__ import annotations


from util.dol_c_kit.mangle import (
    # Builtin types
    BuiltinVoid, BuiltinBool, BuiltinChar, BuiltinShort, BuiltinInt,
    BuiltinFloat, BuiltinDouble, BuiltinEllipses,
    # Decorators
    OperatorUnsigned, OperatorSigned, OperatorLong,
    OperatorConst, OperatorPointer, OperatorReference,
    # Complex classes
    OperatorNamespace, OperatorTemplateArgs, OperatorFunctionArgs,
    # Special tokens
    SpecialTokenCtor, SpecialTokenDtor,
    SpecialTokenVTable, SpecialTokenRTTI,
    SyntaxToken,
    # Special operators
    SpecialOperatorNew, SpecialOperatorNewArray,
    SpecialOperatorDelete, SpecialOperatorDeleteArray,
    SpecialOperatorMultiply, SpecialOperatorDivide, SpecialOperatorModulo,
    SpecialOperatorBitwiseXOR, SpecialOperatorAssignDivide,
    SpecialOperatorBitwiseAND, SpecialOperatorBitwiseOR,
    SpecialOperatorBitwiseNOT, SpecialOperatorLogicalNOT,
    SpecialOperatorAssign,
    SpecialOperatorLesserThan, SpecialOperatorGreaterThan,
    SpecialOperatorAssignAdd, SpecialOperatorAssignSubtract,
    SpecialOperatorAssignMultiply, SpecialOperatorAssignModulo,
    SpecialOperatorAssignBitwiseXOR, SpecialOperatorAssignBitwiseAND,
    SpecialOperatorAssignBitwiseOR,
    SpecialOperatorLeftShift, SpecialOperatorRightShift,
    SpecialOperatorAssignRightShift, SpecialOperatorAssignLeftShift,
    SpecialOperatorEqual, SpecialOperatorNotEqual,
    SpecialOperatorLesserThanOrEqual, SpecialOperatorGreaterThanOrEqual,
    SpecialOperatorLogicalAND, SpecialOperatorLogicalOR,
    SpecialOperatorIncrement, SpecialOperatorDecrement,
    SpecialOperatorArraySubscript,
    ExternC,
    # Mangling infrastructure
    Signature, ItaniumSymbolDictionary, MangleError,
)


# ===================================================================
# Lark grammar
# ===================================================================
#
# The grammar describes the top-level structure of a Macintosh mangled
# symbol.  Length-prefixed names are captured as a single opaque token
# (QUAL_BLOB) and cracked open in the transformer.
#
# Top-level shape (from mangle.py output):
#
#   Free function:       <name> "__" "F" <params>
#   Member function:     <name> "__" <class> "F" <params>
#   Const member func:   <name> "__" <class> "C" "F" <params>
#   Ctor:                "__ct" "__" <class> "F" <params>
#   Dtor:                "__dt" "__" <class> "F" <params>
#   Vtable:              "__vt" "__" <class>
#   RTTI:                "__RTTI" "__" <class>
#   Operator (member):   "__" <op> "__" <class> "F" <params>
#   Operator (free):     "__" <op> "__" "F" <params>
#
# The "__" between entity and the rest is always present.  We handle
# the split in a pre-processing step before handing to lark, because
# the entity names themselves contain underscores.

MAC_GRAMMAR = r'''
start: func_sig
     | data_sig

// A function signature: <class>? "C"? "F" <params>
func_sig: class_name CONST_QUAL "F" param_list   -> const_method
        | class_name "F" param_list               -> method
        | "F" param_list                          -> free_func

// A data/special signature: just <class>
data_sig: class_name

CONST_QUAL: "C"

param_list: type+

// Class name: either Q-qualified or a single l_name blob
class_name: QUAL_BLOB

// Types
type: sign_mod INT_BASE      -> int_type
    | INT_BASE               -> int_type_plain
    | OTHER_BASE             -> other_base_type
    | cv_prefix type         -> cv_type
    | "P" type               -> pointer_type
    | "R" type               -> reference_type
    | "M" type type          -> member_pointer_type
    | class_name             -> class_type

cv_prefix: "CV" | "C" | "V"

sign_mod: "S" -> signed
        | "U" -> unsigned

INT_BASE: /[bcsilxw]/
OTHER_BASE: /[fdrve]/

// QUAL_BLOB: captures a qualified name starting with Q<digits>... or a
// plain l_name starting with digits.  The regex is greedy enough to eat
// the entire qualified name including nested template angle-bracket
// contents.  We crack it open in the transformer.
//
// This matches: Q followed by digits and the rest of the blob, OR
// a plain digit-prefixed name blob.
QUAL_BLOB: /Q[0-9][^\x00]*/ | /[0-9][^\x00]*/

%declare INDENT DEDENT
'''

# ===================================================================
# Stream helper for length-prefixed name parsing
# ===================================================================

class DemangleError(Exception):
    pass


class _Stream:
    """Cursor over a string for cracking length-prefixed names."""
    __slots__ = ('data', 'pos')

    def __init__(self, data: str):
        self.data = data
        self.pos = 0

    def peek(self, n=1) -> str:
        return self.data[self.pos:self.pos + n]

    def read(self, n=1) -> str:
        s = self.data[self.pos:self.pos + n]
        if len(s) < n:
            raise DemangleError(
                f"Unexpected end at pos {self.pos}: "
                f"wanted {n} chars, have {len(s)} in {self.data!r}"
            )
        self.pos += n
        return s

    def remaining(self) -> int:
        return len(self.data) - self.pos

    def at_end(self) -> bool:
        return self.pos >= len(self.data)


# ===================================================================
# Name-blob parser  (the only context-sensitive bit)
# ===================================================================

def _read_int(s: _Stream) -> int:
    digits = ""
    while not s.at_end() and s.peek().isdigit():
        digits += s.read()
    if not digits:
        raise DemangleError(f"Expected integer at pos {s.pos} in {s.data!r}")
    return int(digits)


def _read_l_name(s: _Stream) -> str:
    """Read <int> <chars-of-that-length>."""
    length = _read_int(s)
    return s.read(length)


def _l_name_to_node(raw: str):
    """Convert a raw l_name string to a mangle.py IR node.

    If the name contains ``<...>`` it is a template instantiation; we
    split out the template name and recursively parse the encoded
    parameters inside the angle brackets.
    """
    lt = raw.find('<')
    if lt < 0:
        return SyntaxToken(raw)

    template_name = raw[:lt]
    if not raw.endswith('>'):
        raise DemangleError(f"Template name missing '>': {raw!r}")
    params_str = raw[lt + 1:-1]
    params = _parse_template_params(params_str)
    return _mk_tmpl(SyntaxToken(template_name), params)


def _parse_template_params(blob: str) -> list:
    """Parse comma-separated type encodings between angle brackets.

    Types inside use the same mangling, and may themselves contain
    angle brackets (nested templates), so we split on commas only at
    nesting depth 0.
    """
    parts: list[str] = []
    depth = 0
    current = ""
    for ch in blob:
        if ch == '<':
            depth += 1; current += ch
        elif ch == '>':
            depth -= 1; current += ch
        elif ch == ',' and depth == 0:
            parts.append(current); current = ""
        else:
            current += ch
    if current:
        parts.append(current)

    from util.dol_c_kit.mac_demangle import _parse_type_from_string  # avoid circular at module level
    return [_parse_type_from_string(p) for p in parts]


def _parse_qual_blob(blob: str):
    """Crack open a QUAL_BLOB token into mangle.py IR nodes.

    A QUAL_BLOB is either:
      * ``Q<count><l_name1><l_name2>...``  (qualified / nested name)
      * ``<l_name>``                        (single name)

    The tricky part: the count after Q is an integer whose digits run
    into the length-prefix of the first name.  For example
    ``Q24name4Test`` is Q, count=2, names=[``4name``, ``4Test``].
    We resolve the ambiguity by trying every possible split point for
    the count and checking which one results in a valid parse that
    consumes the entire blob.
    """
    s = _Stream(blob)

    if not s.at_end() and s.peek() == 'Q':
        s.read()  # consume Q
        # Read all leading digits (these are count + start of first l_name length)
        all_digits = ""
        while not s.at_end() and s.peek().isdigit():
            all_digits += s.read()

        if not all_digits:
            raise DemangleError(f"Expected digits after Q in {blob!r}")

        # Try every split: count = all_digits[:i], first l_name length starts at all_digits[i:]
        rest_after_digits = s.data[s.pos:]
        for i in range(1, len(all_digits) + 1):
            count = int(all_digits[:i])
            if count < 2:
                continue  # qualified names have at least 2 components
            leftover_digits = all_digits[i:]
            candidate = leftover_digits + rest_after_digits
            result = _try_parse_n_lnames(candidate, count)
            if result is not None:
                names, _ = result
                nodes = [_l_name_to_node(n) for n in names]
                r = nodes[0]
                for j in range(1, len(nodes)):
                    r = _mk_ns(r, nodes[j])
                return r

        raise DemangleError(f"Cannot parse qualified name blob: {blob!r}")
    else:
        raw = _read_l_name(s)
        if not s.at_end():
            raise DemangleError(
                f"Trailing data after l_name in blob: {blob!r} "
                f"(parsed {raw!r}, leftover {s.data[s.pos:]!r})"
            )
        return _l_name_to_node(raw)


def _try_parse_n_lnames(data: str, n: int, must_consume_all: bool = True) -> tuple[list[str], int] | None:
    """Try to parse exactly *n* length-prefixed names from *data*.

    Returns ``(list_of_names, chars_consumed)`` if successful, or
    ``None`` on failure.  When *must_consume_all* is True the entire
    input must be used up; when False, trailing data is fine.
    """
    s = _Stream(data)
    names: list[str] = []
    try:
        for _ in range(n):
            name = _read_l_name(s)
            names.append(name)
    except DemangleError:
        return None
    if must_consume_all and not s.at_end():
        return None
    return (names, s.pos)


# ===================================================================
# Factory helpers  (bypass MutableString-based constructors)
# ===================================================================

def _mk_sig(items):
    sig = list.__new__(Signature)
    list.__init__(sig, items)
    return sig

def _mk_tmpl(name_token, params):
    obj = list.__new__(OperatorTemplateArgs)
    list.__init__(obj, params)
    obj.lhand = name_token
    return obj

def _mk_fargs(params):
    obj = list.__new__(OperatorFunctionArgs)
    list.__init__(obj, params)
    return obj

def _mk_ns(lhand, rhand):
    obj = OperatorNamespace()
    obj.lhand = lhand
    obj.rhand = rhand
    return obj


# ===================================================================
# Operator code -> mangle.py class mapping
# ===================================================================

_OP_TABLE: dict[str, type] = {
    "nw":  SpecialOperatorNew,
    "nwa": SpecialOperatorNewArray,
    "dl":  SpecialOperatorDelete,
    "dla": SpecialOperatorDeleteArray,
    "ml":  SpecialOperatorMultiply,
    "dv":  SpecialOperatorDivide,
    "md":  SpecialOperatorModulo,
    "er":  SpecialOperatorBitwiseXOR,
    "adv": SpecialOperatorAssignDivide,
    "ad":  SpecialOperatorBitwiseAND,
    "or":  SpecialOperatorBitwiseOR,
    "co":  SpecialOperatorBitwiseNOT,
    "nt":  SpecialOperatorLogicalNOT,
    "as":  SpecialOperatorAssign,
    "lt":  SpecialOperatorLesserThan,
    "gt":  SpecialOperatorGreaterThan,
    "apl": SpecialOperatorAssignAdd,
    "ami": SpecialOperatorAssignSubtract,
    "amu": SpecialOperatorAssignMultiply,
    "amd": SpecialOperatorAssignModulo,
    "aer": SpecialOperatorAssignBitwiseXOR,
    "aad": SpecialOperatorAssignBitwiseAND,
    "aor": SpecialOperatorAssignBitwiseOR,
    "ls":  SpecialOperatorLeftShift,
    "rs":  SpecialOperatorRightShift,
    "ars": SpecialOperatorAssignRightShift,
    "als": SpecialOperatorAssignLeftShift,
    "eq":  SpecialOperatorEqual,
    "ne":  SpecialOperatorNotEqual,
    "le":  SpecialOperatorLesserThanOrEqual,
    "ge":  SpecialOperatorGreaterThanOrEqual,
    "aa":  SpecialOperatorLogicalAND,
    "oo":  SpecialOperatorLogicalOR,
    "pp":  SpecialOperatorIncrement,
    "mm":  SpecialOperatorDecrement,
    "vc":  SpecialOperatorArraySubscript,
}


# ===================================================================
# Pre-processing: split entity from the rest
# ===================================================================

def _split_entity(mangled: str) -> tuple[str, str]:
    """Split a mangled symbol into ``(entity_name, rest)``.

    ``rest`` is everything after the ``__`` separator and is what gets
    fed into the lark grammar.
    """
    # Special prefixed entities — order matters (longest prefix first)
    _PREFIXES = [
        ("__RTTI__", "__RTTI"),
        ("__vt__",   "__vt"),
        ("__ct__",   "__ct"),
        ("__dt__",   "__dt"),
    ]
    for prefix, entity in _PREFIXES:
        if mangled.startswith(prefix):
            return (entity, mangled[len(prefix):])

    # Operator entities: __<op>__<rest>  or  __<op>F<rest>  (free function)
    if mangled.startswith("__"):
        op_codes = sorted(_OP_TABLE.keys(), key=len, reverse=True)
        for op in op_codes:
            # Member operator: __<op>__<class>F<params>
            p = "__" + op + "__"
            if mangled.startswith(p):
                return ("__" + op, mangled[len(p):])
        # Free operator function: __<op>F...  (no class)
        for op in op_codes:
            p = "__" + op
            if mangled.startswith(p) and len(mangled) > len(p) and mangled[len(p)] == 'F':
                return ("__" + op, mangled[len(p):])

    # Normal entity: scan for "__" followed by a digit, Q, F, or C
    i = 0
    while i < len(mangled) - 1:
        if mangled[i] == '_' and mangled[i + 1] == '_':
            rest = mangled[i + 2:]
            if rest and (rest[0].isdigit() or rest[0] in 'QFC'):
                return (mangled[:i], rest)
        i += 1

    raise DemangleError(f"Cannot find '__' separator in {mangled!r}")


def _entity_to_node(name: str):
    """Map an entity-name string to a mangle.py IR node."""
    if name == "__ct": return SpecialTokenCtor()
    if name == "__dt": return SpecialTokenDtor()
    if name == "__vt": return SpecialTokenVTable()
    if name == "__RTTI": return SpecialTokenRTTI()
    if name.startswith("__") and name[2:] in _OP_TABLE:
        return _OP_TABLE[name[2:]]()
    return SyntaxToken(name)


# ===================================================================
# Type-string parser  (used inside template parameters and by lark
# transformer for individual type tokens)
# ===================================================================

def _parse_type_from_string(s_str: str):
    """Parse a single Mac-ABI encoded type from a plain string.

    This is the entry point used by template-parameter parsing (which
    already has the parameter as a substring) and by the lark
    transformer when it needs to interpret a type token.
    """
    s = _Stream(s_str)
    result = _parse_type(s)
    if not s.at_end():
        raise DemangleError(
            f"Trailing data after type parse: {s_str!r} "
            f"(leftover: {s.data[s.pos:]!r})"
        )
    return result


def _parse_type(s: _Stream):
    """Recursive-descent type parser operating on a _Stream."""
    if s.at_end():
        raise DemangleError("Unexpected end of input while parsing type")

    ch = s.peek()

    # CV qualifiers: C (const), V (volatile), CV (both)
    # These appear before the base type or before further qualifiers.
    is_const = False
    if ch == 'C' and s.remaining() >= 2:
        nxt = s.peek(2)[1]
        if nxt == 'V':
            s.read(2); is_const = True  # CV prefix
        elif nxt in 'PRMbcsilxwfdrveUSQ' or nxt.isdigit():
            s.read(); is_const = True   # C prefix before type
    elif ch == 'V' and s.remaining() >= 2:
        nxt = s.peek(2)[1]
        if nxt in 'PRMbcsilxwfdrveUSCQ' or nxt.isdigit():
            s.read()  # volatile — not modeled in IR

    ch = s.peek() if not s.at_end() else ''

    # Type qualifiers
    if ch == 'P':
        s.read()
        inner = _parse_type(s)
        w = OperatorPointer(); w.lhand = inner
        return _wrap_const(w) if is_const else w
    if ch == 'R':
        s.read()
        inner = _parse_type(s)
        w = OperatorReference(); w.lhand = inner
        return _wrap_const(w) if is_const else w
    if ch == 'M':
        s.read()
        _parse_type(s)  # member class type (consumed, simplified to pointer)
        inner = _parse_type(s)
        w = OperatorPointer(); w.lhand = inner
        return _wrap_const(w) if is_const else w

    # Sign modifiers
    if ch in 'SU' and s.remaining() >= 2 and s.peek(2)[1] in 'bcsilxw':
        sign = s.read()
        base = _parse_int_base(s)
        if sign == 'U':
            w = OperatorUnsigned(); w.rhand = base; result = w
        else:
            w = OperatorSigned(); w.rhand = base; result = w
        return _wrap_const(result) if is_const else result

    # Integer base types
    if ch in 'bcsilxw':
        result = _parse_int_base(s)
        return _wrap_const(result) if is_const else result

    # Other base types
    _other = {'f': BuiltinFloat, 'd': BuiltinDouble, 'v': BuiltinVoid, 'e': BuiltinEllipses}
    if ch in _other:
        s.read()
        result = _other[ch]()
        return _wrap_const(result) if is_const else result
    if ch == 'r':
        s.read()
        op = OperatorLong(); op.rhand = BuiltinDouble()
        return _wrap_const(op) if is_const else op

    # Array type
    if ch == 'A' and s.remaining() >= 2 and s.peek(2)[1].isdigit():
        while not s.at_end() and s.peek() == 'A':
            s.read()
            _read_int(s)
            if not s.at_end() and s.peek() == '_':
                s.read()
        result = _parse_type(s)
        return _wrap_const(result) if is_const else result

    # Function type
    if ch == 'F':
        s.read()
        params = []
        while not s.at_end() and s.peek() != '_':
            if not _can_start_type(s):
                break
            params.append(_parse_type(s))
        if not s.at_end() and s.peek() == '_':
            s.read()
            _parse_type(s)  # return type, consumed
        result = _mk_fargs(params)
        return _wrap_const(result) if is_const else result

    # Class name (qualified or simple l_name)
    if ch == 'Q' or ch.isdigit():
        result = _parse_qual_name(s)
        return _wrap_const(result) if is_const else result

    raise DemangleError(f"Cannot parse type at pos {s.pos} in {s.data!r} (char={ch!r})")


def _parse_int_base(s: _Stream):
    """Parse a single int_base_type character."""
    ch = s.read()
    _map = {
        'b': BuiltinBool, 'c': BuiltinChar, 's': BuiltinShort,
        'i': BuiltinInt, 'w': lambda: SyntaxToken("wchar_t"),
    }
    if ch in _map:
        v = _map[ch]
        return v() if callable(v) else v
    if ch == 'l':
        op = OperatorLong(); op.rhand = BuiltinInt(); return op
    if ch == 'x':
        inner = OperatorLong(); inner.rhand = BuiltinInt()
        outer = OperatorLong(); outer.rhand = inner; return outer
    raise DemangleError(f"Unknown int base type: {ch!r}")


def _wrap_const(node):
    w = OperatorConst(); w.lhand = node; return w


def _can_start_type(s: _Stream) -> bool:
    if s.at_end(): return False
    return s.peek() in 'bcsilxwfdrveUSCVPRMAFQ' or s.peek().isdigit()


def _parse_qual_name(s: _Stream):
    """Parse a qualified name or simple l_name from the stream."""
    if s.peek() == 'Q':
        s.read()
        # Read all leading digits
        all_digits = ""
        while not s.at_end() and s.peek().isdigit():
            all_digits += s.read()
        if not all_digits:
            raise DemangleError(f"Expected digits after Q at pos {s.pos}")
        rest = s.data[s.pos:]
        # Try each split of all_digits into count vs first-name-length-prefix
        for i in range(1, len(all_digits) + 1):
            count = int(all_digits[:i])
            if count < 2: continue
            leftover = all_digits[i:] + rest
            result = _try_parse_n_lnames(leftover, count, must_consume_all=False)
            if result is not None:
                names, consumed = result
                # Advance stream: consumed covers leftover digits + rest chars
                advance_in_rest = consumed - len(all_digits[i:])
                s.pos += advance_in_rest
                nodes = [_l_name_to_node(n) for n in names]
                r = nodes[0]
                for j in range(1, len(nodes)):
                    r = _mk_ns(r, nodes[j])
                return r
        raise DemangleError(f"Cannot parse Q-name: Q{all_digits}{rest[:20]!r}...")
    else:
        raw = _read_l_name(s)
        return _l_name_to_node(raw)


# ===================================================================
# Top-level parse entry point
# ===================================================================

def _parse_mangled(mangled: str) -> Signature:
    """Parse a Macintosh ABI mangled name into a ``Signature`` object."""
    entity_str, rest = "",""
    try: 
        entity_str, rest = _split_entity(mangled)
    except DemangleError:
        # assumed it is not a mangled symbol if it doesn't have "__"
        # put these first two arguments because Signature expects a function 
        # symbol to have three; TBD if it actually matters here whether we
        # are talking about a function or not, because this tells
        # it to just print the symbol out directly
        return _mk_sig([None, None, ExternC(mangled)])
        
    entity_node = _entity_to_node(entity_str)

    # Special entities that take only a class name (no function type)
    if entity_str in ("__vt", "__RTTI"):
        s = _Stream(rest)
        class_node = _parse_qual_name(s)
        return _mk_sig([_mk_ns(class_node, entity_node)])

    s = _Stream(rest)

    # Optional class qualification
    class_qual = None
    if not s.at_end() and (s.peek().isdigit() or s.peek() == 'Q'):
        class_qual = _parse_qual_name(s)

    # Const method: 'C' immediately before 'F'
    is_const_method = False
    if not s.at_end() and s.peek() == 'C' and s.remaining() >= 2 and s.peek(2)[1:] == 'F':
        s.read(); is_const_method = True

    # Function parameters
    has_func = False
    params = []
    if not s.at_end() and s.peek() == 'F':
        s.read()
        has_func = True
        while not s.at_end() and _can_start_type(s):
            params.append(_parse_type(s))

    # Build name node
    name_node = _mk_ns(class_qual, entity_node) if class_qual else entity_node

    if has_func:
        if not params:
            params = [BuiltinVoid()]
        func_args = _mk_fargs(params)
        if is_const_method:
            args_node = OperatorConst(); args_node.lhand = func_args
        else:
            args_node = func_args

        if isinstance(entity_node, (SpecialTokenCtor, SpecialTokenDtor)):
            return _mk_sig([name_node, args_node])
        else:
            return _mk_sig([BuiltinVoid(), name_node, args_node])
    else:
        return _mk_sig([name_node])


# ===================================================================
# Public API
# ===================================================================

def mac_demangle(mangled: str) -> str:
    """Demangle to a human-readable string."""
    return str(_parse_mangled(mangled))


def mac_to_itanium(mangled: str) -> str:
    """Re-mangle from Macintosh ABI to Itanium ABI."""
    sig = _parse_mangled(mangled)
    return sig.itanium_mangle(ItaniumSymbolDictionary())


def mac_roundtrip(mangled: str) -> str:
    """Parse a Mac symbol and re-mangle it back to Macintosh ABI."""
    sig = _parse_mangled(mangled)
    return sig.macintosh_mangle()


def mac_demangle_signature(mangled: str) -> Signature:
    """Return the raw mangle.py ``Signature`` for programmatic use."""
    return _parse_mangled(mangled)


# ===================================================================
# Tests: round-trip only (parse Mac -> re-mangle Mac -> compare)
# ===================================================================

if __name__ == "__main__":
    from mangle import macintosh_mangle

    print("=" * 72)
    print("  Round-trip tests: Mac ABI -> parse -> re-mangle Mac ABI")
    print("=" * 72)

    # Build test cases from mangle.py's own output
    _protos = [
        # Simple free functions with various types
        "void foo()",
        "void foo(int)",
        "void foo(long int)",
        "void foo(long long int)",
        "void foo(float)",
        "void foo(double)",
        "void foo(long double)",
        "void foo(char, int, short)",
        "void foo(bool)",
        "void foo(...)",
        "void foo(int, ...)",
        # Pointer / reference
        "void foo(int *)",
        "void foo(int &)",
        "void foo(int const *)",
        "void foo(int const &)",
        # Unsigned / signed
        "void foo(unsigned int)",
        "void foo(unsigned long int)",
        "void foo(unsigned char)",
        "void foo(signed char)",
        # Member functions
        "void ActCrowd::procWallMsg(Piki*, MsgWall*)",
        "void name::Test::MethodA()",
        "void name::Test::MethodB() const",
        # Templates
        "BaseShape::recTraverseMaterials(Joint*, IDelegate2<Joint*, unsigned long int>*)",
        "void foobar(NamespaceA::TemplateClassA<int, float> arg1)",
        # Special entities
        "ClassA::$$ctor()",
        "ClassA::$$dtor()",
        "ClassA::$$vtable",
        "ClassA::$$rtti",
        "NamespaceA::ClassA::$$vtable",
        # Operators
        "void* operator new(unsigned int)",
        "void* operator new[](unsigned int)",
        "void operator delete(void *)",
        "void operator delete[](void *)",
        "void MyClass::operator ==(int)",
        "void MyClass::operator !=(int)",
        "void MyClass::operator <(int)",
        "void MyClass::operator >(int)",
        "void MyClass::operator ++()",
        "void MyClass::operator --()",
        "void MyClass::operator <<(int)",
        "void MyClass::operator >>(int)",
        "void MyClass::operator >>=(unsigned int)",
        "void MyClass::operator <<=(unsigned int)",
        # Deeply nested
        "void a::b::c::d::e::f::g::h::i::j::k::func()",
        # Pikmin 1 longest symbol
        "zen::particleGenerator::init(unsigned char*, Texture*, Texture*, "
        "Vector3f&, zen::particleMdlManager*, "
        "zen::CallBack1<zen::particleGenerator*>*, "
        "zen::CallBack2<zen::particleGenerator*, zen::particleMdl*>*)",
    ]

    pass_count = 0
    fail_count = 0
    error_count = 0

    for proto in _protos:
        try:
            expected = macintosh_mangle(proto)
        except Exception as e:
            print(f"  SKIP  {proto}")
            print(f"         mangle.py cannot mangle: {e}")
            continue

        try:
            actual = mac_roundtrip(expected)
        except Exception as e:
            print(f"  ERROR {expected}")
            print(f"         from: {proto}")
            print(f"         {e}")
            error_count += 1
            continue

        if actual == expected:
            print(f"  OK    {expected}")
            pass_count += 1
        else:
            print(f"  FAIL  {expected}")
            print(f"         got:  {actual}")
            print(f"         from: {proto}")
            fail_count += 1

    print()
    print(f"  Results: {pass_count} passed, {fail_count} failed, {error_count} errors")
    print()
