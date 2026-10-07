import os

import bootstrap

# The repository root, which run-tests also derives from its own location.
ROOT = os.path.dirname(bootstrap.TESTS)

CATEGORIES = ("e2e", "perf", "soak")
MODES = ("overlay", "freeze", "telnet")
DEFAULT_MODE = "overlay"
