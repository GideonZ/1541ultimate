"""What the API explorer page is allowed to load, and from where.

html/api.html is a static page the device serves next to the document. It loads
Swagger UI from unpkg.com rather than carrying it, because the bundle is larger
than the flash disk of the smallest product. A script from another origin runs
with the page's privileges, so this module records exactly which release the page
may load and the digest of each file, and test_explorer.py fails the build when
the page stops agreeing with it.

Moving to a newer Swagger UI means changing VERSION and both digests here and in
the page. The digests are what `openssl dgst -sha384 -binary | openssl base64 -A`
prints for the file at the pinned URL.
"""

import base64
import hashlib
import pathlib
import re

PAGE = "html/api.html"

VERSION = "5.32.14"
ORIGIN = "https://unpkg.com/swagger-ui-dist@%s/" % VERSION

# Digest of each file at the pinned URL, as the page's integrity attribute states it.
SUBRESOURCES = {
    ORIGIN + "swagger-ui.css":
        "sha384-fgyWYkUAamzuI8mJFu/xpRP0JWCJRwkwUwsYDoOYVHUJ8NQE5cENn8ib3ppwFFSX",
    ORIGIN + "swagger-ui-bundle.js":
        "sha384-Dt83RhU85ZmX7werw9uTFCzmauXUoSyx3pdzTQMABtsnFmooJy4Vz9/ACh7n5m1A",
}

# Directives the page must carry, whatever else it adds. `connect-src 'self'` is
# the one that matters most: it is what stops anything the page loads from
# sending the device's answers, or a password typed into Try it out, anywhere
# else. `script-src` names no origin but the pinned one, and no inline script but
# the page's own, which is admitted by its digest rather than by 'unsafe-inline'.
REQUIRED_POLICY = {
    "default-src": ["'none'"],
    "connect-src": ["'self'"],
    "base-uri": ["'none'"],
    "form-action": ["'none'"],
}

_TAG = re.compile(r"<(script|link)\b([^>]*)>", re.IGNORECASE)
_ATTRIBUTE = re.compile(r'(\w[\w-]*)\s*=\s*"([^"]*)"')
_INLINE_SCRIPT = re.compile(r"<script(?![^>]*\bsrc=)[^>]*>(.*?)</script>", re.DOTALL | re.IGNORECASE)
_POLICY = re.compile(
    r'<meta\s+http-equiv="Content-Security-Policy"\s+content="([^"]*)"', re.IGNORECASE
)


def page_text(repo_root):
    return (pathlib.Path(repo_root) / PAGE).read_text()


def external_references(text):
    """Every URL the page pulls from another origin, with the attributes beside it."""
    out = []
    for kind, attributes in _TAG.findall(text):
        found = dict(_ATTRIBUTE.findall(attributes))
        url = found.get("src") or found.get("href", "")
        if url.startswith(("http://", "https://", "//")):
            out.append((kind.lower(), url, found))
    return out


def inline_scripts(text):
    return _INLINE_SCRIPT.findall(text)


def digest(text):
    """The CSP source expression that admits exactly this inline script."""
    return "sha256-" + base64.b64encode(hashlib.sha256(text.encode()).digest()).decode()


def policy(text):
    """The page's Content-Security-Policy, as {directive: [source, ...]}."""
    found = _POLICY.search(text)
    if not found:
        return None
    out = {}
    for clause in found.group(1).split(";"):
        parts = clause.split()
        if parts:
            out[parts[0]] = parts[1:]
    return out
