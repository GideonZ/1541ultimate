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
from contextlib import contextmanager
from typing import Callable, Iterable, Iterator, List, Optional

import targets
from report import Failure

FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"
DEFAULT_TIMEOUT = 15.0


def connect(host: str, password: Optional[str] = None,
            timeout: float = DEFAULT_TIMEOUT,
            directory: Optional[str] = None,
            user: str = FTP_USER) -> ftplib.FTP:
    """Open and log in an FTP session, optionally changing directory.

    `host` may be a target: the FTP server under test is the device's, so a
    cartridge target connects to the cartridge. See tests/lib/targets.py.
    """
    try:
        client = ftplib.FTP(timeout=timeout)
        client.connect(targets.device_of(host), 21)
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
