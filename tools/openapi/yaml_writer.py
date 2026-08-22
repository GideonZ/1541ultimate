"""A deterministic block-style YAML writer, so generating a document needs nothing installed."""

import re

from errors import OpenApiError

_PLAIN = re.compile(r"^[A-Za-z_/$][A-Za-z0-9_ ./,()$+=-]*$")
_NUMBER = re.compile(r"^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$")
_RESERVED = {"true", "false", "null", "yes", "no", "on", "off", "~"}

# A line feed and a tab are the only control characters this writer can place in
# a scalar: the first as a block scalar, the second inside quotes. Every other
# one is refused rather than written, because a quoted scalar carrying it is not
# a document any reader will accept.
_WRITABLE_IN_SCALAR = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")
_WRITABLE_IN_KEY = re.compile(r"[\x00-\x1f\x7f]")

INDENT = 2


def dump(node):
    return _render(node, 0)


def _quoted(text):
    return "'%s'" % text.replace("'", "''")


def _needs_quotes(text):
    return (
        text == ""
        or text != text.strip()
        or text.lower() in _RESERVED
        or bool(_NUMBER.match(text))
        or not _PLAIN.match(text)
    )


def _scalar(value, indent):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    found = _WRITABLE_IN_SCALAR.search(text)
    if found:
        raise OpenApiError(
            "cannot write the control character %r in the value %r"
            % (found.group(0), text[:60])
        )
    if "\n" in text:
        padding = " " * (indent + INDENT)
        lines = [line.rstrip() for line in text.rstrip("\n").split("\n")]
        block = "\n".join(padding + line if line else "" for line in lines)
        # The explicit indentation indicator keeps a first line that starts with a
        # space from being read as extra indentation.
        return ("|%d\n" % INDENT if text.endswith("\n") else "|%d-\n" % INDENT) + block
    return _quoted(text) if _needs_quotes(text) else text


def _key(name):
    text = str(name)
    found = _WRITABLE_IN_KEY.search(text)
    if found:
        raise OpenApiError(
            "cannot write the control character %r in the key %r"
            % (found.group(0), text[:60])
        )
    return _quoted(text) if _needs_quotes(text) else text


def _empty(value):
    return "{}" if isinstance(value, dict) else "[]"


def _render(node, indent):
    padding = " " * indent
    if isinstance(node, dict):
        if not node:
            return padding + "{}\n"
        out = []
        for key, value in node.items():
            if isinstance(value, (dict, list)):
                if value:
                    out.append("%s%s:\n%s" % (padding, _key(key), _render(value, indent + INDENT)))
                else:
                    out.append("%s%s: %s\n" % (padding, _key(key), _empty(value)))
            else:
                out.append("%s%s: %s\n" % (padding, _key(key), _scalar(value, indent)))
        return "".join(out)
    if isinstance(node, list):
        out = []
        for item in node:
            if isinstance(item, (dict, list)):
                if item:
                    rendered = _render(item, indent + INDENT)
                    out.append(padding + "- " + rendered[indent + INDENT:])
                else:
                    out.append("%s- %s\n" % (padding, _empty(item)))
            else:
                out.append("%s- %s\n" % (padding, _scalar(item, indent)))
        return "".join(out)
    return padding + _scalar(node, indent) + "\n"
