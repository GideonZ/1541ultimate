import os

import bootstrap

# The repository root.
ROOT = os.path.dirname(bootstrap.TESTS)

CATEGORIES = ("e2e", "perf", "soak")
MODES = ("overlay", "freeze", "telnet")
DEFAULT_MODE = "overlay"
