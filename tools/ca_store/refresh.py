#!/usr/bin/env python3
"""Refreshes the Mozilla root store the ESP32 bridge trusts (software/wifi/ca_store).

    refresh.py [--source URL] [--summary FILE] [--ident-minor N]

Fetches curl's cacert.pem and its published sum, and checks the file against a copy made again
from Mozilla's own certdata.txt on GitHub. If Mozilla's data changed, it writes cacert.pem and
CA_PINS and, when the bundle's certificates changed, moves IDENT_MINOR of every project that
attaches the bundle (ca_store.PROJECTS) to --ident-minor N. Without it such a change is refused:
the updater reflashes a module only for a new version, and on a development line the next number
may already belong to another image, so the number is allocated by a person, not taken.

curl.se publishes the file and its sum from the same server, so the sum proves only that the
download is whole. The second copy is made from certdata.txt on Firefox's release branch (the
revision whose sha256 curl's header names) with curl's own mk-ca-bundle.pl, pinned by commit and
sha256, and curl's file is taken only if the two are the same byte for byte. That revision must
also be the newest certdata.txt on the branch (see GRACE), and a descendant of the committed CA_CERTDATA_REF, so a server serving an older conversion under
a new date is refused too. Changing the store then takes both curl.se and GitHub.

Needs Perl for mk-ca-bundle.pl, and the network; nothing else beyond the standard library.
"""

import argparse
import calendar
import datetime
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import ca_store
from ca_store import (CLOCK_SLACK, REPO_ROOT, StoreError, attaching, bump_ident,
                      bundle_set_sha256, certificates, iso, max_certs, mozilla_date,
                      parse_date, parse_iso, projects, read_pins, roots,
                      store_paths, utc_now, write_atomically, write_pins)

# The second origin: Mozilla's certdata.txt as Firefox's release branch carries it, and the
# script curl converts it with.
CERTDATA_REPO = "mozilla-firefox/firefox"
CERTDATA_BRANCH = "release"
CERTDATA_PATH = "security/nss/lib/ckfw/builtins/certdata.txt"
CERTDATA_COMMITS = 10
# How old (by commit date) the newest certdata.txt on `release` may be for curl's file to match an older one.
# Newer revisions arrive older than this, so only the newest is ever taken: no rollback, at the cost of a
# red run, healed the next week, when `release` moves between curl's daily run and ours.
GRACE = datetime.timedelta(days=14)
MK_CA_BUNDLE_COMMIT = "d48f2f4ec867783e611beeb2ad53fbabcb94aa6f"
MK_CA_BUNDLE_SHA256 = "8e278f4982210b37e4f9a048bd6ed23df41e4a054085bd73f8242628f03d09ca"
MK_CA_BUNDLE_URL = "https://raw.githubusercontent.com/curl/curl/%s/scripts/mk-ca-bundle.pl"
RAW_URL = "https://raw.githubusercontent.com/%s/%s/%s"
API_URL = "https://api.github.com/repos/%s/commits?sha=%s&path=%s&per_page=%d"
COMPARE_URL = "https://api.github.com/repos/%s/compare/%s...%s"
CERTDATA_PREFIX = "## SHA256: "
FETCH_TIMEOUT = 60


def changes(old_text, new_text):
    """(added, removed, rekeyed) as sorted lists: fingerprints for the first two, and
    (label, old fingerprint, new fingerprint) for a subject that stays with a different key or
    certificate, which is the case to look at by hand because ESP-IDF looks roots up by
    subject."""
    old, new = roots(old_text), roots(new_text)
    by_subject = {subject: fp for fp, (_, subject, _) in old.items()}
    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    rekeyed = []
    for fp in list(added):
        old_fp = by_subject.get(new[fp][1])
        if old_fp in removed:
            rekeyed.append((new[fp][0], old_fp, fp))
            added.remove(fp)
            removed.remove(old_fp)
    return added, removed, sorted(rekeyed)


def plain(label):
    """A label safe to paste into a pull request description."""
    return re.sub(r"[^A-Za-z0-9 .,()&'/:_+-]", "?", label)[:100]


def name_key(subject):
    """A subject name with its string types and case set aside, to catch a look-alike: the same
    name spelled with UTF8String where the real one uses PrintableString is a different subject
    to ESP-IDF, and to the SAME SUBJECT section, but not to a reader."""
    tlv = ca_store._tlv
    _, start, end = tlv(subject, 0)
    parts = []
    while start < end:                          # SET OF AttributeTypeAndValue
        _, set_start, set_end = tlv(subject, start)
        while set_start < set_end:
            _, seq_start, seq_end = tlv(subject, set_start)
            _, oid_start, oid_end = tlv(subject, seq_start)
            _, value_start, value_end = tlv(subject, oid_end)
            value = subject[value_start:value_end].decode("utf-8", "replace").strip().lower()
            parts.append((subject[oid_start:oid_end], value))
            set_start = seq_end
        start = set_end
    return tuple(sorted(parts))


def certdata_sha256(text):
    for line in text.split("\n"):
        if line.startswith(CERTDATA_PREFIX) and re.fullmatch(r"[0-9a-f]{64}",
                                                             line[len(CERTDATA_PREFIX):]):
            return line[len(CERTDATA_PREFIX):]
    raise StoreError("no '%s' line naming the certdata.txt it came from"
                     % CERTDATA_PREFIX.strip("# :"))


def validate(data, expected_sha256):
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected_sha256:
        raise StoreError("sha256 mismatch: the file is %s, the published sum is %s"
                         % (actual, expected_sha256))
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise StoreError("not UTF-8")
    date = mozilla_date(text)
    parse_date(date)
    return text, date, certificates(text)


# The second origin.


def fetch(url, token=False):
    request = urllib.request.Request(url)
    # A token only raises the API's rate limit; nothing here needs its permissions.
    if token and os.environ.get("GITHUB_TOKEN"):
        request.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
    try:
        with urllib.request.urlopen(request, timeout=FETCH_TIMEOUT) as response:
            return response.read()
    except (urllib.error.URLError, OSError) as error:
        raise StoreError("cannot fetch %s: %s" % (url, error))


def fetch_json(url):
    try:
        return json.loads(fetch(url, token=True))
    except ValueError:
        raise StoreError("%s did not return JSON" % url)


def find_certdata(wanted, now):
    """(CA_CERTDATA_REF, contents, commit date) of the certdata.txt revision on Firefox's
    release branch whose sha256 is the one curl's file names. It must be the newest revision,
    or the newest must have been committed less than GRACE ago."""
    url = API_URL % (CERTDATA_REPO, CERTDATA_BRANCH, CERTDATA_PATH, CERTDATA_COMMITS)
    try:
        commits = [(c["sha"], c["commit"]["committer"]["date"]) for c in fetch_json(url)]
    except (KeyError, TypeError):
        raise StoreError("%s did not return a list of commits" % url)
    for index, (commit, date) in enumerate(commits):
        data = fetch(RAW_URL % (CERTDATA_REPO, commit, CERTDATA_PATH))
        if hashlib.sha256(data).hexdigest() != wanted:
            continue
        if index:
            newest, newest_date = commits[0]
            since = now - datetime.datetime.strptime(newest_date, ca_store.ISO_FORMAT)
            if since > GRACE:
                raise StoreError(
                    "curl's file is certdata.txt from %s (committed %s), but the release branch "
                    "now carries a newer one, %s@%s, committed %s (%d days ago). Either the "
                    "branch moved after curl's last daily run, which the next run heals once "
                    "curl publishes it, or this is an old file served as new"
                    % (commit[:12], date, CERTDATA_REPO, newest[:12], newest_date, since.days))
        return "%s@%s" % (CERTDATA_REPO, commit), data, date
    raise StoreError("none of the last %d revisions of %s in %s is the certdata.txt curl's file "
                     "names (sha256 %s)" % (len(commits), CERTDATA_PATH, CERTDATA_REPO, wanted))


def descends(old_ref, new_ref):
    """Whether new_ref is old_ref or a later commit on top of it."""
    if old_ref == new_ref:
        return True
    old_repo, old = old_ref.split("@", 1)
    new_repo, new = new_ref.split("@", 1)
    if old_repo != new_repo:
        return False
    result = fetch_json(COMPARE_URL % (new_repo, old, new))
    if not isinstance(result, dict):
        raise StoreError("GitHub's compare of %s...%s did not return an object" % (old, new))
    return result.get("status") == "ahead" and result.get("behind_by") == 0


def regenerate(text, now):
    """(CA_CERTDATA_REF, bytes, commit date): curl's file made again from Mozilla's
    certdata.txt on GitHub with curl's own pinned mk-ca-bundle.pl. Its "as of" line comes from
    the input's modification time, which is set to curl's date, so an honest file comes out
    identical."""
    ref, certdata, date = find_certdata(certdata_sha256(text), now)
    script = fetch(MK_CA_BUNDLE_URL % MK_CA_BUNDLE_COMMIT)
    if hashlib.sha256(script).hexdigest() != MK_CA_BUNDLE_SHA256:
        raise StoreError("mk-ca-bundle.pl at %s is not the pinned file" % MK_CA_BUNDLE_COMMIT)
    stamp = calendar.timegm(parse_date(mozilla_date(text)).timetuple())
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        (work / "mk-ca-bundle.pl").write_bytes(script)
        (work / "certdata.txt").write_bytes(certdata)
        os.utime(work / "certdata.txt", (stamp, stamp))
        try:
            run = subprocess.run(["perl", "mk-ca-bundle.pl", "-n", "-q", "cacert.pem"], cwd=work,
                                 capture_output=True, check=False)
        except OSError as error:
            raise StoreError("cannot run perl: %s" % error)
        if run.returncode != 0:
            raise StoreError("mk-ca-bundle.pl failed: %s"
                             % run.stderr.decode("utf-8", "replace").strip())
        return ref, (work / "cacert.pem").read_bytes(), date


def published_sha256(data, url):
    match = re.match(r"([0-9a-f]{64})\s", data.decode("ascii", "replace") + "\n")
    if not match:
        raise StoreError("%s is not a sha256 sum" % url)
    return match.group(1)


# The refresh.


def refresh(repo_root, source=None, summary=None, now=None, regenerate=regenerate,
            descends=descends, ident_minor=None):
    """Returns the paths written, empty when Mozilla's data has not changed."""
    pem_path, pins_path = store_paths(repo_root)
    pins = read_pins(pins_path)
    now = now or utc_now()
    url = source or pins["CA_SOURCE_URL"]
    data = fetch(url)
    text, date, ders = validate(data, published_sha256(fetch(url + ".sha256"), url + ".sha256"))

    when, old_date = parse_date(date), pins["CA_MOZILLA_DATE"]
    if when > now + CLOCK_SLACK:
        raise StoreError("%s is dated %s, in the future" % (url, iso(date)))
    if when < parse_iso(old_date):
        raise StoreError("%s is older than the committed store (%s, committed %s)"
                         % (url, iso(date), old_date))

    # Every run, changed or not, so a weekly job keeps checking curl.se against Mozilla.
    ref, again, ref_date = regenerate(text, now)
    if again != data:
        try:
            added, removed, rekeyed = changes(again.decode("utf-8"), text)
            detail = ("; in curl's file only: %s; in Mozilla's only: %s"
                      % (", ".join(added + [new for _, _, new in rekeyed]) or "none",
                         ", ".join(removed + [old for _, old, _ in rekeyed]) or "none"))
        except (StoreError, UnicodeDecodeError):
            detail = ""
        raise StoreError("%s is not what %s gives when converted with mk-ca-bundle.pl %s%s"
                         % (url, ref, MK_CA_BUNDLE_COMMIT, detail))
    if not descends(pins["CA_CERTDATA_REF"], ref):
        raise StoreError("%s comes from %s, which is not the committed %s or a later commit on "
                         "top of it: an older certdata.txt under a newer date, or the branch's "
                         "history was rewritten (then move CA_CERTDATA_REF by hand after a look)"
                         % (url, ref, pins["CA_CERTDATA_REF"]))

    old_text = pem_path.read_text() if pem_path.exists() else ""
    if iso(date) == old_date:
        if old_text and set(ders) != set(certificates(old_text)):
            raise StoreError("the certificates changed but the Mozilla date did not (%s)" % date)
        return []

    using = projects(repo_root)
    for project, sdkconfig in using:
        limit = max_certs(sdkconfig)
        if limit is not None and len(ders) > limit:
            raise StoreError("%d certificates, but %s/sdkconfig allows %d "
                             "(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS)"
                             % (len(ders), project, limit))

    # The bundle is built from the certificates alone, so only a change to them needs the
    # modules that carry it reflashed. Every header is checked before anything is written.
    new_set = bundle_set_sha256(ders)
    day = when.date().isoformat()
    carrying = ([project for project, _ in using if attaching(project)]
                if new_set != pins["CA_BUNDLE_SET_SHA256"] else [])
    if carrying and ident_minor is None:
        raise StoreError("the bundle's certificates changed, so %s needs a new IDENT_MINOR: pass "
                         "--ident-minor N with a number allocated for this image"
                         % ", ".join(os.path.relpath(p, repo_root) for p in carrying))
    bumps = [bump_ident(project, day, ident_minor) for project in carrying]

    write_atomically(pem_path, data)
    write_pins(pins_path, {"CA_MOZILLA_DATE": iso(date),
                           "CA_SHA256": hashlib.sha256(data).hexdigest(),
                           "CA_CERTDATA_REF": ref,
                           "CA_BUNDLE_SET_SHA256": new_set})
    written = [pem_path, pins_path]
    bumped = []
    for header, text_after, version in bumps:
        write_atomically(header, text_after.encode())
        written.append(header)
        bumped.append((header.parent.parent, version))

    if summary:
        pathlib.Path(summary).write_text(describe(
            old_text, text, url, ref, ref_date, iso(date), old_date, len(ders),
            new_set != pins["CA_BUNDLE_SET_SHA256"], bumped, repo_root))
    return written


def describe(old_text, text, url, ref, ref_date, date, old_date, count, set_changed, bumped,
             repo_root):
    """The pull request description: what changed, by SHA-256 fingerprint."""
    added, removed, rekeyed = changes(old_text, text)
    new, old = roots(text), roots(old_text)
    lines = ["Mozilla root store as of %s (was %s), %d certificates." % (date, old_date, count),
             "",
             "Fetched from %s. Regenerated from `%s` (committed %s) with curl's mk-ca-bundle.pl "
             "at %s, byte for byte the same." % (url, ref, ref_date, MK_CA_BUNDLE_COMMIT), ""]
    if rekeyed:
        lines += ["## SAME SUBJECT, DIFFERENT CERTIFICATE: check each of these by hand", "",
                  "ESP-IDF finds a root by its subject name, so a changed key under the same "
                  "subject changes what the modules trust for every server that names it.", ""]
        lines += ["- %s: `%s` became `%s`" % (plain(label), old_fp, new_fp)
                  for label, old_fp, new_fp in rekeyed]
        lines += [""]
    kept = {name_key(subject): label for fp, (label, subject, _) in old.items()
            if fp not in removed}
    kept_labels = {label for label in kept.values()}
    lookalikes = [fp for fp in added
                  if name_key(new[fp][1]) in kept or new[fp][0] in kept_labels]
    if lookalikes:
        lines += ["## ADDED UNDER THE NAME OF A ROOT ALREADY HERE: check each of these by hand",
                  "", "A new root whose name reads like one the store already has.", ""]
        lines += ["- `%s` %s" % (fp, plain(new[fp][0])) for fp in lookalikes] + [""]
    lines += ["## Added", ""] + (["- `%s` %s" % (fp, plain(new[fp][0])) for fp in added]
                                 or ["None."]) + [""]
    lines += ["## Removed", ""] + (["- `%s` %s" % (fp, plain(old[fp][0])) for fp in removed]
                                   or ["None."]) + [""]
    if bumped:
        lines += ["## Module versions", ""]
        lines += ["- %s now reports %s." % (os.path.relpath(p, repo_root), v) for p, v in bumped]
    elif set_changed:
        lines += ["No project attaches the bundle, so no module version changes."]
    else:
        lines += ["The certificates did not change, so the bundle and the module versions do "
                  "not either."]
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument("--source", help="fetch from this URL instead of CA_SOURCE_URL "
                                         "(its sum from the same URL + .sha256)")
    parser.add_argument("--summary", help="write what changed to this file")
    parser.add_argument("--ident-minor", type=int,
                        help="the IDENT_MINOR allocated for the image, needed when the "
                             "certificates change")
    arguments = parser.parse_args(argv)
    repo_root = pathlib.Path(arguments.repo_root)
    try:
        written = refresh(repo_root, arguments.source, arguments.summary,
                          ident_minor=arguments.ident_minor)
    except (StoreError, OSError) as error:
        print("refresh: %s" % error, file=sys.stderr)
        return 1
    if not written:
        print("refresh: the Mozilla root store is current, nothing to do")
    for path in written:
        print("refresh: wrote %s" % os.path.relpath(path, repo_root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
