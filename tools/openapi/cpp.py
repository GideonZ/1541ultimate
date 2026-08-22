"""Reads just enough C++ to find macro invocations and the #if regions around them."""

import re

from errors import OpenApiError

_TOKEN = re.compile(r"\s*(0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|&&|\|\||[<>=!]=|.)")
_DIRECTIVE = re.compile(r"^#\s*(\w+)\s*(.*)$")
_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
_INTEGER = re.compile(r"^(0[xX][0-9a-fA-F]+|\d+)$")
_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')

# The single-character escapes C defines. \x and the octal forms are decoded
# from their digits; anything else is refused rather than silently dropped.
_ESCAPES = {
    "a": "\a", "b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t", "v": "\v",
    "\\": "\\", "'": "'", '"': '"', "?": "?",
}
_HEX_ESCAPE = re.compile(r"x([0-9a-fA-F]+)")
_OCTAL_ESCAPE = re.compile(r"([0-7]{1,3})")

_PRECEDENCE = (("||",), ("&&",), ("==", "!="), ("<", ">", "<=", ">="), ("+", "-"))
_APPLY = {
    "||": lambda a, b: int(bool(a) or bool(b)),
    "&&": lambda a, b: int(bool(a) and bool(b)),
    "==": lambda a, b: int(a == b),
    "!=": lambda a, b: int(a != b),
    "<": lambda a, b: int(a < b),
    ">": lambda a, b: int(a > b),
    "<=": lambda a, b: int(a <= b),
    ">=": lambda a, b: int(a >= b),
    "+": lambda a, b: a + b,
    "-": lambda a, b: a - b,
}


def _skip_literal(text, start):
    quote = text[start]
    i = start + 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == quote:
            return i + 1
        i += 1
    raise OpenApiError("unterminated %s literal" % quote)


def without_comments(text):
    out = []
    i = 0
    while i < len(text):
        if text[i] in "\"'":
            end = _skip_literal(text, i)
            out.append(text[i:end])
            i = end
        elif text.startswith("//", i):
            while i < len(text) and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise OpenApiError("unterminated block comment")
            out.append("\n" * text.count("\n", i, end))
            i = end + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


class _Expression:
    """The subset of the #if grammar the route sources use. Anything else raises."""

    def __init__(self, text, defines):
        self.tokens = [t for t in _TOKEN.findall(text) if t.strip()]
        self.defines = defines
        self.position = 0

    def evaluate(self):
        value = self._binary(0)
        if self._peek() is not None:
            raise OpenApiError("trailing %r in #if expression" % self._peek())
        return value

    def _peek(self):
        return self.tokens[self.position] if self.position < len(self.tokens) else None

    def _take(self):
        token = self._peek()
        self.position += 1
        return token

    def _expect(self, token):
        if self._take() != token:
            raise OpenApiError("expected %r in #if expression" % token)

    def _binary(self, level):
        if level == len(_PRECEDENCE):
            return self._unary()
        value = self._binary(level + 1)
        while self._peek() in _PRECEDENCE[level]:
            operator = self._take()
            value = _APPLY[operator](value, self._binary(level + 1))
        return value

    def _unary(self):
        token = self._take()
        if token == "!":
            return int(not self._unary())
        if token == "-":
            return -self._unary()
        if token == "(":
            value = self._binary(0)
            self._expect(")")
            return value
        if token == "defined":
            bracketed = self._peek() == "("
            if bracketed:
                self._take()
            name = self._take()
            if bracketed:
                self._expect(")")
            if not name or not _IDENTIFIER.match(name):
                raise OpenApiError("defined() needs an identifier")
            return int(name in self.defines)
        if token is None:
            raise OpenApiError("#if expression ended early")
        if _INTEGER.match(token):
            return int(token, 0)
        if _IDENTIFIER.match(token):
            value = self.defines.get(token, 0)
            if not isinstance(value, int):
                raise OpenApiError(
                    "%s is defined as %r, which this evaluator cannot compare" % (token, value)
                )
            return value
        raise OpenApiError("unsupported token %r in #if expression" % token)


class _Region:
    def __init__(self, taken, enclosing_active):
        self.enclosing_active = enclosing_active
        self.matched = taken
        self.active = enclosing_active and taken

    def switch_to(self, taken):
        self.active = self.enclosing_active and taken and not self.matched
        self.matched = self.matched or taken


def active_lines(text, defines):
    """Blanks the lines the preprocessor would skip, leaving the line numbering intact."""
    regions = []
    out = []
    for number, line in enumerate(text.split("\n"), start=1):
        directive = _DIRECTIVE.match(line.strip())
        if not directive:
            out.append(line if all(r.active for r in regions) else "")
            continue
        name, rest = directive.group(1), directive.group(2).strip()
        try:
            if name in ("if", "ifdef", "ifndef"):
                regions.append(_Region(_opens(name, rest, defines), all(r.active for r in regions)))
            elif name == "elif":
                _current(regions, name).switch_to(bool(_Expression(rest, defines).evaluate()))
            elif name == "else":
                _current(regions, name).switch_to(True)
            elif name == "endif":
                _current(regions, name)
                regions.pop()
        except OpenApiError as error:
            raise OpenApiError("line %d: %s" % (number, error))
        out.append("")
    if regions:
        raise OpenApiError("unterminated #if")
    return "\n".join(out)


def _opens(name, rest, defines):
    if name == "ifdef":
        return rest in defines
    if name == "ifndef":
        return rest not in defines
    return bool(_Expression(rest, defines).evaluate())


def _current(regions, name):
    if not regions:
        raise OpenApiError("#%s without #if" % name)
    return regions[-1]


def closing_parenthesis(text, start):
    depth = 1
    i = start
    while i < len(text):
        if text[i] in "\"'":
            i = _skip_literal(text, i)
            continue
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise OpenApiError("unbalanced parentheses")


def _scan(text, opening):
    """Walks `text`, yielding what `opening` matches outside literals and nested calls."""
    i = 0
    while i < len(text):
        if text[i] in "\"'":
            i = _skip_literal(text, i)
            continue
        match = opening.match(text, i)
        if match and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            end = closing_parenthesis(text, match.end())
            yield text.count("\n", 0, i) + 1, match, text[match.end():end]
            i = end + 1
            continue
        i += 1


def invocations(text, macro):
    """Yields (line number, argument text) for every call of `macro`."""
    opening = re.compile(re.escape(macro) + r"\s*\(")
    for line, _, arguments in _scan(text, opening):
        yield line, arguments


def calls(text):
    """Yields (line number, name, argument text) for every call that is not nested in another."""
    opening = re.compile(r"([A-Za-z_]\w*)\s*\(")
    for line, match, arguments in _scan(text, opening):
        yield line, match.group(1), arguments


def split_arguments(text):
    """Splits on commas that are neither nested nor inside a literal."""
    parts = []
    current = 0
    depth = 0
    i = 0
    while i < len(text):
        character = text[i]
        if character in "\"'":
            i = _skip_literal(text, i)
            continue
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
        elif character == "," and depth == 0:
            parts.append(text[current:i])
            current = i + 1
        i += 1
    parts.append(text[current:])
    return [part.strip() for part in parts]


def _byte(value, literal, sequence):
    if value > 0xFF:
        raise OpenApiError(
            "escape sequence %r in %r denotes %#x, which is not one byte"
            % ("\\" + sequence, literal, value)
        )
    return chr(value)


def _escape(literal, i):
    """Decodes the escape sequence starting at the backslash at `i`."""
    rest = literal[i + 1:]
    hexadecimal = _HEX_ESCAPE.match(rest)
    if hexadecimal:
        # C reads every hexadecimal digit that follows \x, so the digits are not
        # split into a byte and some trailing text.
        return _byte(int(hexadecimal.group(1), 16), literal, hexadecimal.group(0)), 1 + hexadecimal.end()
    octal = _OCTAL_ESCAPE.match(rest)
    if octal:
        return _byte(int(octal.group(1), 8), literal, octal.group(0)), 1 + octal.end()
    if rest[:1] in _ESCAPES:
        return _ESCAPES[rest[0]], 2
    raise OpenApiError("unsupported escape sequence %r in %r" % ("\\" + rest[:1], literal))


def string_literal(text):
    """Resolves one or more adjacent C string literals into the string they denote."""
    literals = _STRING.findall(text)
    if not literals or _STRING.sub("", text).strip():
        raise OpenApiError("expected only string literals, found %r" % text.strip())
    out = []
    for literal in literals:
        i = 0
        while i < len(literal):
            if literal[i] == "\\":
                decoded, width = _escape(literal, i)
                out.append(decoded)
                i += width
            else:
                out.append(literal[i])
                i += 1
    return "".join(out)
