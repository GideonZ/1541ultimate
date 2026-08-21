"""A miniature repository the parser and document tests run against."""

import contextlib
import pathlib
import tempfile
from unittest import mock

import schemas

DEMO_SOURCE = """\
#include "routes.h"

API_DOC(GET, demo, none,
    TAG("Demo")
    SUMMARY("List demos")
    DESCRIPTION("Lists every demo.")
    PATH("/v1/demo", "listDemos", "")
    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")
)
API_CALL(GET, demo, none, NULL, ARRAY( { }))
{
}

#if BIG
API_DOC(PUT, demo, poke,
    TAG("Demo")
    SUMMARY("Poke a slot")
    DESCRIPTION("Writes one byte into a slot.")
    PATH("/v1/demo/{slot}:poke", "pokeDemo", "")
    PATH_PARAM("slot", "string", "Which slot.", "a")
    PATH_PARAM_ENUM("slot", "a,b")
    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")
    RESPONSE("200", "application/json", "DemoResponse", "The byte was written.", "")
    RESPONSE_ERROR("400", "Invalid slot", "")
)
API_CALL(PUT, demo, poke, NULL, ARRAY( { { "value", P_REQUIRED } }))
{
}
#endif
"""

EXTRA_SOURCE = """\
#include "routes.h"

API_DOC(POST, extra, upload,
    TAG("Demo")
    SUMMARY("Upload something")
    DESCRIPTION("Takes a body.")
    PATH("/v1/extra:upload", "uploadExtra", "")
    BODY("application/octet-stream", "", "The payload.")
    RESPONSE("200", "application/json", "DemoResponse", "Accepted.", "")
)
API_CALL(POST, extra, upload, &attachment_writer, ARRAY( { }))
{
}
"""

SMALL_MAKEFILE = "SRCS_CC = route_demo.cc \\\n\troutes.cc\n"
LARGE_MAKEFILE = "SRCS_CC = route_demo.cc \\\n\troute_extra.cc\n"

SCHEMAS = {
    "ErrorResponse": {
        "type": "object",
        "properties": {"errors": {"type": "array", "items": {"type": "string"}}},
    },
    "DemoResponse": {
        "type": "object",
        "properties": {"demos": {"type": "array", "items": {"type": "string"}}},
    }
}

TAGS = [("Demo", "The demo route.")]

PROFILES = {
    "small": {
        "defines": {},
        "targets": ("target/small/Makefile",),
        "title": "Small API",
        "products": "the small product",
        "default_host": "small",
    },
    "large": {
        "defines": {"BIG": 1},
        "targets": ("target/large/Makefile",),
        "title": "Large API",
        "products": "the large product",
        "default_host": "large",
    },
}


def write(root, sources=None, makefiles=None):
    files = {"software/api/route_demo.cc": DEMO_SOURCE, "software/api/route_extra.cc": EXTRA_SOURCE}
    files.update({"software/api/%s" % name: text for name, text in (sources or {}).items()})
    files["target/small/Makefile"] = SMALL_MAKEFILE
    files["target/large/Makefile"] = LARGE_MAKEFILE
    files.update(makefiles or {})
    for relative, text in files.items():
        target = pathlib.Path(root) / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
    return pathlib.Path(root)


@contextlib.contextmanager
def repository(sources=None, makefiles=None, profiles=None):
    """A temporary tree, with schemas.py replaced by the fixture's own definitions."""
    with tempfile.TemporaryDirectory() as root:
        with mock.patch.multiple(
            schemas, PROFILES=profiles or PROFILES, SCHEMAS=SCHEMAS, TAGS=TAGS
        ):
            yield write(root, sources, makefiles)
