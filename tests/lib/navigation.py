"""What the on-device menu does with a typed letter, which is a setting.

"User Interface Settings" / "Navigation Style" has two values, and it changes
what every menu key handler sees:

    Quick Search    a letter reaches the handler as itself
    WASD Cursors    'w', 'a', 's' and 'd' become KEY_UP, KEY_LEFT, KEY_DOWN
                    and KEY_RIGHT, and 'A' to 'Z' are folded to lowercase

`UserInterface::keymapper` (software/userinterface/userinterface.cc) applies
that, and the two rules are one if/else: an uppercase letter is folded and
never reaches the WASD switch. So under WASD Cursors, the way to type a
literal letter is to send it shifted, which is what the firmware's own help
text tells a person at the keyboard to do.

Measured on a C64 Ultimate 1.2.0 (Navigation Style "WASD Cursors"), in the
root file browser with the cursor on "Temp":

    's' moved the cursor down one row       'S' jumped the cursor to "SD"
    'a' left the browser for the launcher   'A' did nothing, because the
    'w' moved the cursor up one row         listing has no entry starting
                                            with 'a' or 'w'

The keymapper is applied by the file browser, the context and task menus, the
text viewer, popups and the choice picker. It is not applied by the monitor
(`e_keymap_monitor` is excluded) and not by the string editor
`UIStringEdit::poll`, which is what a suite types a filename into. So this
transform belongs at the call sites that type a *command* into the menu, and
must not be applied to a suite typing text into a field.

Which machine's setting counts is the machine whose firmware draws the menu.
For a cartridge target the keys are injected into the computer, but they are
read by the cartridge's own UI, so it is the cartridge's setting.

The setting is not a property of the model: a C64 Ultimate defaults to WASD
Cursors and an Ultimate 64 and an Ultimate II+ default to Quick Search, but
all three offer both values and a person can change any of them. That is why
this is asked of the device instead of derived from the product name the way
tests/lib/machine.py derives a capability.
"""

from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Callable

CONFIG_CATEGORY = "User Interface Settings"
CONFIG_ITEM = "Navigation Style"

QUICK_SEARCH = "Quick Search"
WASD_CURSORS = "WASD Cursors"

# The four letters the menu reads as cursor keys under WASD Cursors.
CURSOR_LETTERS = "wasd"


@dataclass(frozen=True)
class Navigation:
    """How this device's menu reads the letters a suite types at it."""

    style: str

    @property
    def wasd(self) -> bool:
        return self.style == WASD_CURSORS

    def menu_char(self, character: str) -> str:
        """The character to send so the menu's key handler receives `character`.

        Under WASD Cursors a letter is sent shifted, because the keymapper
        folds 'A' to 'Z' back to lowercase before it looks for a cursor
        letter. The menu therefore receives the lowercase form of whatever
        letter is asked for, which is what its two consumers need: the quick
        seek matches case-insensitively, and the popup button keys are
        lowercase ('o', 'y', 'n', 'a', 'c' in `UserInterface::popup`).

        Everything that is not an ASCII letter is sent unchanged, because the
        keymapper only touches 'A' to 'Z' and the four cursor letters.
        """
        if self.wasd and character.isascii() and character.isalpha():
            return character.upper()
        return character

    def menu_text(self, text: str) -> str:
        """`menu_char` over a whole string."""
        return "".join(self.menu_char(character) for character in text)

    def receives_uppercase(self) -> bool:
        """Whether the menu can be made to read an uppercase letter at all.

        False under WASD Cursors: every letter arrives folded to lowercase, so
        a caller that needs the menu to tell 'A' from 'a' cannot get it. No
        menu key handler does; this says so rather than leaving the limit to
        be rediscovered.
        """
        return not self.wasd


def classify(style: str) -> Navigation:
    """The navigation for a reported setting value.

    An unknown or empty value is Quick Search, which is what a device that
    does not serve the setting behaves like.
    """
    return Navigation(style=style if style == WASD_CURSORS else QUICK_SEARCH)


_cache: dict[str, Navigation] = {}


def identify(host: str, fetch_style: Callable[[], str]) -> Navigation:
    """The navigation `host` is set to, reading the setting at most once.

    `fetch_style` is supplied by the caller for the same reason
    tests/lib/machine.py takes one: this module needs no REST client, and a
    host test can drive it without a device. It returns the value of
    "Navigation Style", or "" when the device does not serve the item.
    """
    cached = _cache.get(host)
    if cached is None:
        cached = classify(fetch_style())
        _cache[host] = cached
    return cached


def forget(host: str | None = None) -> None:
    """Drop what was learnt, for a caller that changed the setting."""
    if host is None:
        _cache.clear()
    else:
        _cache.pop(host, None)
