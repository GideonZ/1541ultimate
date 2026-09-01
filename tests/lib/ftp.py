"""FTP against the device, for suites that use it for fixtures.

The credentials are the device's defaults: pass whatever `-p/--password` the
suite was given, and the default fills in when that is empty.

Directory listings are the one thing to be careful with. The device truncates a
name it reports (see `tests/e2e/network/ftp_server_test.py`), so a name taken
from a listing is not always the name the file was stored under. Delete by the
name you stored when you have it.
"""

from __future__ import annotations

import ftplib
import io
import re
import time
from contextlib import contextmanager
from typing import Callable, Iterable, Iterator, List, Optional

import interactions
import targets
from report import Failure

FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"
DEFAULT_TIMEOUT = 15.0


class RecordedFTP(ftplib.FTP):
    """An FTP client that writes every command and its reply to the log.

    Subclassed rather than wrapped because `ftplib` builds its own commands
    from a dozen methods, and hooking the two the protocol actually passes
    through is the only way to record all of them without a copy of each. See
    tests/lib/interactions.py.
    """

    _sent = None

    def putcmd(self, line):
        self._sent = (line, time.monotonic())
        super().putcmd(line)

    def getmultiline(self):
        reply = super().getmultiline()
        sent, self._sent = self._sent, None
        if sent is None:
            interactions.record("ftp", "reply", reply=reply.splitlines()[0]
                                if reply else "")
            return reply
        command, started = sent
        verb, _, argument = command.partition(" ")
        interactions.record(
            "ftp", verb.upper() or "reply",
            # A password is never written down, whether or not this run
            # registered it as a secret: the argument of PASS is one whatever
            # it is.
            argument="***" if verb.upper() == "PASS" else argument,
            reply=reply.splitlines()[0] if reply else "",
            ms=round((time.monotonic() - started) * 1000.0, 1))
        return reply

    def ntransfercmd(self, cmd, rest=None):
        """Open a data connection, and say so.

        FTP moves its payload on a second connection, and nothing about that
        connection reaches `putcmd` or `getmultiline`: those two carry the
        command that asked for it and the reply that opened it, and then the
        bytes go somewhere the log cannot see. A record of a `RETR` with no
        record of what came back is a control channel transcript rather than
        an account of the interaction, and a truncated listing or a short
        transfer is invisible in it.

        Every transfer in `ftplib` funnels through here - `retrbinary`,
        `storbinary`, `retrlines`, `storlines`, `nlst`, `dir` and `mlsd` all
        reach a data connection by this method - so one hook covers them the
        way `putcmd` and `getmultiline` cover the control channel. The socket
        is handed back wrapped in a counter, so the bytes are recorded as they
        move rather than being asked for afterwards from a caller that has
        already consumed them.
        """
        started = time.monotonic()
        try:
            conn, size = super().ntransfercmd(cmd, rest)
        except Exception as exc:  # noqa: BLE001 - recorded, then re-raised
            interactions.record(
                "ftp", f"data {cmd.split(' ')[0].upper()}",
                argument=cmd.partition(" ")[2], connection="new",
                fault=interactions.fault_of(exc), error=str(exc),
                ms=round((time.monotonic() - started) * 1000.0, 1))
            raise
        return _CountedData(conn, cmd, size, started), size


def _byte_length(chunk) -> int:
    """How many bytes this is, whether it arrived as bytes or as text.

    `retrlines` opens the data connection in text mode, so its lines are
    already decoded and their character count is not their byte count on any
    line holding a character outside ASCII. The field says bytes on every
    other transport, so it says bytes here.
    """
    if isinstance(chunk, str):
        return len(chunk.encode("utf-8", "replace"))
    return len(chunk)


class _CountedData:
    """A data connection that records how much crossed it, once it closes.

    Everything but the methods below is the socket's own, so `ftplib` treats
    this exactly as it treats the connection it asked for, and a method this
    does not know about cannot break because of it. The context-manager pair
    is spelled out rather than delegated because Python looks a dunder up on
    the type and never on the instance, and `retrbinary`, `storbinary` and
    `retrlines` all take the connection with `with`.
    """

    def __init__(self, sock, command: str, size, started: float) -> None:
        self._sock = sock
        self._command = command
        self._size = size
        self._started = started
        self._sent = 0
        self._received = 0
        self._recorded = False

    def __getattr__(self, name):
        if name.startswith("_"):
            # Reached only while the instance is half built, and delegating it
            # would look `_sock` up through this same method for ever.
            raise AttributeError(name)
        return getattr(self._sock, name)

    def __enter__(self):
        return self

    def __exit__(self, *_exception):
        self.close()

    def recv(self, *args, **kwargs):
        data = self._sock.recv(*args, **kwargs)
        self._received += _byte_length(data)
        return data

    def sendall(self, data, *args, **kwargs):
        result = self._sock.sendall(data, *args, **kwargs)
        self._sent += _byte_length(data)
        return result

    def send(self, data, *args, **kwargs):
        count = self._sock.send(data, *args, **kwargs)
        self._sent += count
        return count

    def makefile(self, *args, **kwargs):
        # `retrlines` and `storlines` read and write through a file object
        # rather than the socket, so the counting has to follow it there or
        # every line-oriented transfer records zero bytes.
        return _CountedFile(self._sock.makefile(*args, **kwargs), self)

    def close(self):
        try:
            self._sock.close()
        finally:
            self._finish()

    def _finish(self) -> None:
        if self._recorded:
            return
        self._recorded = True
        verb, _, argument = self._command.partition(" ")
        interactions.record(
            "ftp", f"data {verb.upper()}", argument=argument,
            connection="new", declared=self._size,
            sent=self._sent or None, received=self._received or None,
            ms=round((time.monotonic() - self._started) * 1000.0, 1))


class _CountedFile:
    """The file `ftplib` reads a listing through, counting what passes."""

    def __init__(self, handle, owner: "_CountedData") -> None:
        self._handle = handle
        self._owner = owner

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return getattr(self._handle, name)

    def __iter__(self):
        for line in self._handle:
            self._owner._received += _byte_length(line)
            yield line

    def readline(self, *args, **kwargs):
        line = self._handle.readline(*args, **kwargs)
        self._owner._received += _byte_length(line)
        return line

    def read(self, *args, **kwargs):
        data = self._handle.read(*args, **kwargs)
        self._owner._received += _byte_length(data)
        return data

    def write(self, data, *args, **kwargs):
        result = self._handle.write(data, *args, **kwargs)
        self._owner._sent += _byte_length(data)
        return result

    def close(self):
        return self._handle.close()

    def __enter__(self):
        self._handle.__enter__()
        return self

    def __exit__(self, *exception):
        return self._handle.__exit__(*exception)


def connect(host: str, password: Optional[str] = None,
            timeout: float = DEFAULT_TIMEOUT,
            directory: Optional[str] = None,
            user: str = FTP_USER) -> ftplib.FTP:
    """Open and log in an FTP session, optionally changing directory.

    `host` may be a target: the FTP server under test is the device's, so a
    cartridge target connects to the cartridge. See tests/lib/targets.py.
    """
    started = time.monotonic()
    try:
        client = RecordedFTP(timeout=timeout)
        target = targets.resolve(host)
        try:
            client.connect(target.device, target.ftp_port)
        except Exception as exc:  # noqa: BLE001 - recorded, then re-raised
            interactions.record(
                "ftp", f"connect {target.device}:{target.ftp_port}",
                ms=round((time.monotonic() - started) * 1000.0, 1),
                fault=interactions.fault_of(exc), error=str(exc),
                connection="new")
            raise
        interactions.record(
            "ftp", f"connect {target.device}:{target.ftp_port}",
            ms=round((time.monotonic() - started) * 1000.0, 1),
            connection="new")
        client.login(user, password or FTP_DEFAULT_PASSWORD)
        if directory:
            client.cwd(directory)
        return client
    except ftplib.all_errors as exc:
        raise Failure(f"FTP connect to {host} failed: {exc}") from exc


@contextmanager
def session(host: str, password: Optional[str] = None,
            timeout: float = DEFAULT_TIMEOUT,
            directory: Optional[str] = None,
            passive: bool = True,
            user: str = FTP_USER) -> Iterator[ftplib.FTP]:
    """A logged-in session that is always closed, however the body ends."""
    client = connect(host, password, timeout, directory, user)
    if passive:
        client.set_pasv(True)
    try:
        yield client
    finally:
        close(client)


def retrieve(client: ftplib.FTP, path: str) -> bytes:
    """Download `path` and return its contents."""
    buffer = io.BytesIO()
    try:
        client.retrbinary(f"RETR {path}", buffer.write)
    except ftplib.all_errors as exc:
        raise Failure(f"FTP retrieve of {path} failed: {exc}") from exc
    return buffer.getvalue()


def listing(client: ftplib.FTP, directory: Optional[str] = None) -> List[str]:
    """Raw LIST lines, for a caller that needs the size or date columns.

    Changes to `directory` first, like `names`, so every name this module
    returns is relative to the session's current directory and can be passed
    straight back to `delete_quietly` or `retrieve`. Listing a directory
    without moving into it returns names the session cannot then address.

    Use `names` when only the entry names matter.
    """
    try:
        if directory:
            client.cwd(directory)
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return []
        raise Failure(f"FTP cd to {directory} failed: {exc}") from exc
    lines: List[str] = []
    try:
        client.retrlines("LIST", lines.append)
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return []
        raise Failure(f"FTP LIST of {directory or '.'} failed: {exc}") from exc
    return lines


def make_dir(client: ftplib.FTP, path: str) -> bool:
    """Create `path`, treating "already there" as success."""
    try:
        client.mkd(path)
        return True
    except ftplib.error_perm:
        return path.rsplit("/", 1)[-1] in names(client, path.rsplit("/", 1)[0] or "/")


def names(client: ftplib.FTP, directory: Optional[str] = None,
          prefix: str = "") -> List[str]:
    """Entry names in `directory`, or an empty list when it does not exist."""
    try:
        if directory:
            client.cwd(directory)
        listed = client.nlst()
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return []
        raise Failure(f"FTP list of {directory or '.'} failed: {exc}") from exc
    return [name for name in listed if name.startswith(prefix)]


def delete_quietly(client: ftplib.FTP, name: str) -> bool:
    """Remove one entry, returning whether it went away.

    Tries `DELE` and then `RMD`, because a caller working from a listing does
    not always know which one an entry is.
    """
    for remove in (client.delete, client.rmd):
        try:
            remove(name)
            return True
        except ftplib.all_errors:
            continue
    return False


def quietly(action: Callable[[], object]) -> bool:
    """Run an FTP call whose failure is not itself a result.

    For cleanup: a fixture that is already gone, a directory that was never
    created. Returns whether the call succeeded, so a caller that does care can
    still tell.
    """
    try:
        action()
        return True
    except ftplib.all_errors:
        return False


def store(client: ftplib.FTP, path: str, payload: bytes) -> str:
    """Upload `payload` as `path`, returning the server's closing reply."""
    try:
        return client.storbinary("STOR " + path, io.BytesIO(payload))
    except ftplib.all_errors as exc:
        raise Failure(f"FTP store of {path} failed: {exc}") from exc


def remove_tree(client: ftplib.FTP, directory: str) -> bool:
    """Remove `directory` and everything in it, returning whether it went away.

    Entries are removed before the directory itself, and a subdirectory is
    recursed into, because the device refuses RMD on a directory that still has
    contents.
    """
    entries = names(client, directory)
    for name in entries:
        target = f"{directory}/{name}"
        if not delete_quietly(client, target):
            remove_tree(client, target)
    return delete_quietly(client, directory)


def close(client: ftplib.FTP) -> None:
    """End a session, falling back to dropping the socket."""
    try:
        client.quit()
    except ftplib.all_errors:
        try:
            client.close()
        except ftplib.all_errors:
            pass


def file_names(client: ftplib.FTP, directory: Optional[str] = None) -> List[str]:
    """Names of the files in `directory`, excluding subdirectories.

    Parsed from LIST rather than NLST, because only LIST distinguishes a file
    from a directory.
    """
    found: List[str] = []
    for line in listing(client, directory):
        parts = line.split(maxsplit=8)
        if len(parts) < 9 or parts[0].startswith("d"):
            continue
        name = parts[8]
        if name not in (".", ".."):
            found.append(name)
    return sorted(found)


def purge_directory(client: ftplib.FTP, directory: str,
                    keep: Iterable[str] = ()) -> int:
    """Remove every file in `directory` except `keep`, returning how many went.

    A directory that does not exist is not a failure: there is nothing to
    purge. A delete the device refuses is, and it names the entry and the
    reply, because a caller purging in order to assert the directory is empty
    would otherwise see that assertion fail later with no mention of a delete.
    """
    kept = set(keep)
    try:
        client.cwd(directory)
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return 0
        raise Failure(f"FTP purge of {directory} failed: {exc}") from exc

    removed = 0
    refused: List[str] = []
    for name in file_names(client):
        if name in kept:
            continue
        try:
            client.delete(name)
            removed += 1
        except ftplib.all_errors as exc:
            refused.append(f"{name} ({exc})")
    if refused:
        raise Failure(f"FTP could not delete in {directory}: {'; '.join(refused)}")
    return removed


def usb_volumes(client: ftplib.FTP) -> List[str]:
    """The physical USB volumes the device serves, as paths, lowest port first.

    The device names a volume after the USB port its medium is in, so the stick
    a suite is meant to write to is /USB0 on one machine and /USB2 on the next.
    A suite that assumes /USB0 finds nothing on the second machine and skips
    itself: that is how tests/e2e/network/ftp_usb_integrity_test.py reported OK
    on a C64 Ultimate that reproduces GideonZ/1541ultimate#803 on every run.

    Sorted by port number rather than by name, so /USB10 does not come between
    /USB1 and /USB2.
    """
    found = []
    for entry in names(client, "/"):
        name = entry.rsplit("/", 1)[-1]
        match = re.fullmatch(r"USB(\d+)", name)
        if match:
            found.append((int(match.group(1)), f"/{name}"))
    return [path for _, path in sorted(found)]
