#!/usr/bin/env python3
"""Checks the Mozilla root store the ESP32 bridge trusts for TLS, before every ESP32 build.

    ca_store.py check      fail if the store, its pins and the sdkconfigs disagree; warn when
                           the store is older than CA_MAX_AGE_DAYS, and fail instead with
                           --release or RELEASE (not 0) in the environment

The bridge's build runs the check at its configure step and again at every build, with
--project and --sdkconfig-json naming the configuration that build actually uses (which
`idf.py -D SDKCONFIG=...` can point away from the project's own sdkconfig).

The store is the directory software/wifi/ca_store: cacert.pem, the bundle curl extracts from
Mozilla's certdata.txt (the same source ESP-IDF's own cacrt_all.pem comes from), and CA_PINS.
Each project in PROJECTS selects CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE and points
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH at cacert.pem, so ESP-IDF's gen_crt_bundle.py builds
the device's bundle from it alone, and again whenever it changes. ESP-IDF refreshes its own copy
only on its release branches, and not always then: v6.0.3 ships Mozilla's data of 2025-12-02.

There are no hand-kept anchors. ESP-IDF 6 verifies cross-signed chains
(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY, which the bridge pins to y and its engine
refuses to build without), so a chain a server sends through a cross-sign verifies against the
newer root Mozilla still trusts. `check` refuses anchors.pem, or any other certificate file, in
the store.

The Ultimate's updater reflashes a module only when the version the module reports differs from
the one it carries. CA_BUNDLE_SET_SHA256 pins the set of certificates the bundle is built from,
so `check` fails when that set changes without the pin, and refresh.py moves the pin together
with IDENT_MINOR of every project whose own sources attach the bundle.

Only the standard library, so it runs in the build image and on a stock runner alike.
"""

import argparse
import base64
import datetime
import hashlib
import json
import os
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE_DIR = "software/wifi/ca_store"
PEM_NAME = "cacert.pem"
PINS_NAME = "CA_PINS"
# A file that must not be in the store: ESP-IDF 6 verifies cross-signed chains itself, so no hand-kept roots are needed.
ANCHORS_NAME = "anchors.pem"
PIN_KEYS = ("CA_SOURCE_URL", "CA_MOZILLA_DATE", "CA_SHA256", "CA_MAX_AGE_DAYS", "CA_CERTDATA_REF",
            "CA_BUNDLE_SET_SHA256")
# The ESP-IDF 6 projects whose bundle comes from the store. raw_c3 and u64ctrl are built with
# ESP-IDF 5.3's bundle, which looks up only the issuer of the topmost certificate a server
# sends, so they would need hand-kept anchor roots; they keep ESP-IDF's own bundle.
PROJECTS = ("software/wifi/raw_u64",)
# Source directories a project builds that are not under its own main/, relative to the project. The TLS
# engine, which attaches the bundle, is shared by the module firmware that carry it (software/wifi/tls).
SHARED_SOURCES = {"software/wifi/raw_u64": ("../tls",)}

DATE_PREFIX = "## Certificate data from Mozilla as of: "
BEGIN = "-----BEGIN CERTIFICATE-----"
END = "-----END CERTIFICATE-----"
ISO_FORMAT = "%Y-%m-%dT%H:%M:%SZ"
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
# How far ahead of this host's clock a Mozilla date may be. A later one would make every real
# update look like a rollback, and the age gate would never fire again.
CLOCK_SLACK = datetime.timedelta(days=1)
# What a project's sources call when they verify servers against the bundle. A project that
# never calls it has no bundle in its image.
ATTACH = "esp_crt_bundle_attach"


class StoreError(Exception):
    pass


def store_paths(repo_root):
    store = pathlib.Path(repo_root) / STORE_DIR
    return store / PEM_NAME, store / PINS_NAME


def write_atomically(path, data):
    """Writes through a new file renamed into place, so a build never reads half a file."""
    path = pathlib.Path(path)
    temporary = path.with_name(path.name + ".new")
    temporary.write_bytes(data)
    os.replace(str(temporary), str(path))


# CA_PINS: KEY=VALUE lines and # comments, parsed like tests/crypto/PINS and shell-sourceable,
# so no value has quotes or spaces.


def read_pins(path):
    pins = {}
    for line in pathlib.Path(path).read_text().split("\n"):
        line = line.split("#", 1)[0].strip()
        if line:
            if "=" not in line:
                raise StoreError("%s: %r is not KEY=VALUE" % (path, line))
            key, value = line.split("=", 1)
            pins[key] = value
    missing = [key for key in PIN_KEYS if key not in pins]
    if missing:
        raise StoreError("%s: missing %s" % (path, ", ".join(missing)))
    if not pins["CA_MAX_AGE_DAYS"].isdigit():
        raise StoreError("%s: CA_MAX_AGE_DAYS=%s is not a number of days"
                         % (path, pins["CA_MAX_AGE_DAYS"]))
    return pins


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)


def write_pins(path, updates):
    """Rewrites the given keys in place, keeping the order and the comments."""
    out = []
    for line in pathlib.Path(path).read_text().split("\n"):
        key = line.split("=", 1)[0]
        if "=" in line and not line.startswith("#") and key in updates:
            line = "%s=%s" % (key, updates[key])
        out.append(line)
    write_atomically(path, "\n".join(out).encode())


# The PEM.


def mozilla_date(text):
    for line in text.split("\n"):
        if line.startswith(DATE_PREFIX):
            return line[len(DATE_PREFIX):].strip()
    raise StoreError("no '%s' line" % DATE_PREFIX.strip("# "))


def parse_date(stamp):
    # "Fri Sep 25 03:12:01 2026 GMT", parsed by hand so the host's locale cannot change it.
    match = re.fullmatch(r"\w{3} (\w{3}) +(\d{1,2}) (\d\d):(\d\d):(\d\d) (\d{4}) GMT", stamp)
    if not match or match.group(1) not in MONTHS:
        raise StoreError("unrecognised Mozilla date %r" % stamp)
    month, day, hour, minute, second, year = match.groups()
    return datetime.datetime(int(year), MONTHS.index(month) + 1, int(day),
                             int(hour), int(minute), int(second))


def iso(stamp):
    """The header's date as CA_PINS holds it: 2026-09-25T03:12:01Z."""
    return parse_date(stamp).strftime(ISO_FORMAT)


def parse_iso(value):
    try:
        return datetime.datetime.strptime(value, ISO_FORMAT)
    except ValueError:
        raise StoreError("CA_MOZILLA_DATE=%s is not of the form 2026-09-25T03:12:01Z" % value)


def certificates(text):
    """The DER of each certificate, after checking the file is whole."""
    ders = []
    inside = None
    for line in text.split("\n"):
        if line == BEGIN:
            if inside is not None:
                raise StoreError("certificate %d is not closed" % (len(ders) + 1))
            inside = []
        elif line == END:
            if inside is None:
                raise StoreError("END without BEGIN after certificate %d" % len(ders))
            try:
                der = base64.b64decode("".join(inside), validate=True)
            except ValueError:
                raise StoreError("certificate %d is not valid base64" % (len(ders) + 1))
            if len(der) < 4 or der[0] != 0x30:
                raise StoreError("certificate %d is not DER" % (len(ders) + 1))
            ders.append(der)
            inside = None
        elif inside is not None:
            inside.append(line)
    if inside is not None:
        raise StoreError("the file ends inside certificate %d (truncated?)" % (len(ders) + 1))
    if not ders:
        raise StoreError("no certificates")
    if not text.endswith(END + "\n"):
        raise StoreError("the file does not end with a whole certificate (truncated?)")
    return ders


def names(text):
    """The label curl writes above each certificate, in file order."""
    lines = text.split("\n")
    return [lines[i - 2] for i, line in enumerate(lines)
            if line == BEGIN and i >= 2 and lines[i - 1].startswith("=")]


def _tlv(der, pos):
    """(tag, start of the value, end of the element) of the DER element at pos."""
    if pos + 2 > len(der):
        raise StoreError("DER ends early")
    tag, length = der[pos], der[pos + 1]
    pos += 2
    if length & 0x80:
        count = length & 0x7F
        if count == 0 or count > 4 or pos + count > len(der):
            raise StoreError("bad DER length")
        length = int.from_bytes(der[pos:pos + count], "big")
        pos += count
    if pos + length > len(der):
        raise StoreError("DER element runs past its end")
    return tag, pos, pos + length


def subject_and_key(der):
    """The subject name and the SubjectPublicKeyInfo of a certificate, each as DER. These are
    the two things ESP-IDF's bundle keeps of a root."""
    _, start, _ = _tlv(der, 0)                  # Certificate
    _, pos, _ = _tlv(der, start)                # tbsCertificate
    fields = []
    while len(fields) < 7:
        tag, _, end = _tlv(der, pos)
        if not (tag == 0xA0 and not fields):    # skip the optional [0] version
            fields.append(der[pos:end])
        pos = end
    # serial, signature, issuer, validity, subject, subjectPublicKeyInfo
    return fields[4], fields[5]


def roots(text):
    """{sha256 fingerprint: (label, subject DER, key DER)} of every certificate in a store."""
    if not text:
        return {}
    ders = certificates(text)
    labels = names(text)
    if len(labels) != len(ders):
        labels = ["(no label)"] * len(ders)
    return {hashlib.sha256(der).hexdigest(): (label, *subject_and_key(der))
            for label, der in zip(labels, ders)}


def bundle_set_sha256(ders):
    """CA_BUNDLE_SET_SHA256: the sha256 over the DER encodings, sorted and concatenated. DER is
    self-delimiting, so the concatenation is unambiguous, and the order of the certificates does
    not matter, as it does not to gen_crt_bundle.py."""
    return hashlib.sha256(b"".join(sorted(set(ders)))).hexdigest()


# The ESP32 projects that build their bundle from the store.


def has_line(text, line):
    return line in text.split("\n")


def sdkconfig_from_json(path):
    """A build's config/sdkconfig.json (what it really uses) in sdkconfig's own syntax."""
    try:
        config = json.loads(pathlib.Path(path).read_text())
    except (OSError, ValueError) as error:
        raise StoreError("cannot read the build's configuration %s: %s" % (path, error))
    lines = []
    for key, value in config.items():
        if value is True:
            lines.append("CONFIG_%s=y" % key)
        elif value is False:
            lines.append("# CONFIG_%s is not set" % key)
        elif isinstance(value, str):
            lines.append('CONFIG_%s="%s"' % (key, value))
        else:
            lines.append("CONFIG_%s=%s" % (key, value))
    return "\n".join(lines) + "\n"


def projects(repo_root, effective=None):
    """(project dir, sdkconfig text) of every project in PROJECTS. effective maps a project dir
    to the sdkconfig.json of the build at hand, which then stands for its sdkconfig."""
    repo_root = pathlib.Path(repo_root)
    effective = {pathlib.Path(p).resolve(): j for p, j in (effective or {}).items()}
    for project in effective:
        if project not in [(repo_root / path).resolve() for path in PROJECTS]:
            raise StoreError("%s is not one of the projects that use the store" % project)
    found = []
    for path in PROJECTS:
        project = repo_root / path
        if project.resolve() in effective:
            found.append((project, sdkconfig_from_json(effective[project.resolve()])))
            continue
        sdkconfig = project / "sdkconfig"
        if not sdkconfig.exists():
            raise StoreError("%s: no sdkconfig, but PROJECTS names it" % path)
        found.append((project, sdkconfig.read_text()))
    return found


def attaching(project):
    """Whether a project's own sources (under main/, and its SHARED_SOURCES, not ESP-IDF's) attach
    the bundle."""
    project = pathlib.Path(project)
    roots = [project / "main"]
    for name, shared in SHARED_SOURCES.items():
        if project.resolve().as_posix().endswith("/" + name):
            roots += [project / directory for directory in shared]
    for root in roots:
        if not root.is_dir():
            raise StoreError("%s: no %s, but it is one of the project's sources" % (project, root))
        for path in sorted(root.rglob("*")):
            if (path.suffix in (".c", ".cc", ".cpp", ".h")
                    and ATTACH in path.read_text(errors="replace")):
                return True
    return False


def bundle_misconfigured(repo_root, effective=None):
    """Each project builds its bundle from cacert.pem and nothing else, and verifies cross-signed
    chains, which is what lets the store go without anchors."""
    repo_root = pathlib.Path(repo_root)
    pem = (repo_root / STORE_DIR / PEM_NAME).resolve()
    problems = []
    for project, text in projects(repo_root, effective):
        relative = (project / "sdkconfig").relative_to(repo_root)
        if not has_line(text, "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y"):
            problems.append("%s: CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is off, so the store is not "
                            "used" % relative)
            continue
        match = re.search(r'^CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="(.*)"$', text, re.M)
        if not match or (project / match.group(1)).resolve() != pem:
            problems.append("%s: CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH does not name "
                            "%s/%s" % (relative, STORE_DIR, PEM_NAME))
        elif not has_line(text, "CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y"):
            problems.append("%s: names the store but does not set "
                            "CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y, so the path is ignored"
                            % relative)
        if not has_line(text, "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=y"):
            problems.append("%s: also takes ESP-IDF's default bundle; select "
                            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE" % relative)
        if not has_line(text, "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y"):
            problems.append("%s: the store holds no anchors, so it needs "
                            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y" % relative)
    return problems


def max_certs(text):
    match = re.search(r"^CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=(\d+)$", text, re.M)
    return int(match.group(1)) if match else None


def bump_ident(project, date, new):
    """IDENT_MINOR and IDENT_STRING moved to `new`, which must be above the current minor, with a
    line saying why, as the header keeps them. The caller names the number: on a development line
    the next one may already belong to another image.

    Returns (header, new text, new version) and writes nothing, so a caller can check every
    project before it changes any file."""
    header = project / "main" / "rpc_dispatch.h"
    if not header.exists():
        raise StoreError("%s uses the store but has no %s" % (project, header))
    text = header.read_text()
    major = re.search(r"^#define IDENT_MAJOR\s+(\d+)$", text, re.M)
    minor = re.search(r"^#define IDENT_MINOR\s+(\d+)$", text, re.M)
    string = re.search(r'^#define IDENT_STRING "(.* V)(\d+)\.(\d+)"$', text, re.M)
    if not (major and minor and string):
        raise StoreError("%s: IDENT_MAJOR, IDENT_MINOR or IDENT_STRING not found" % header)
    if (string.group(2), string.group(3)) != (major.group(1), minor.group(1)):
        raise StoreError("%s: IDENT_STRING says %s.%s, IDENT_MAJOR/MINOR say %s.%s" % (
            header, string.group(2), string.group(3), major.group(1), minor.group(1)))
    if new <= int(minor.group(1)):
        raise StoreError("%s: IDENT_MINOR %d is not above %s" % (header, new, minor.group(1)))
    version = "%s.%d" % (major.group(1), new)
    text = (text[:minor.start()] + minor.group(0).replace(minor.group(1), str(new))
            + text[minor.end():])
    text = (text[:string.start()] + '#define IDENT_STRING "%s%s"' % (string.group(1), version)
            + text[string.end():])
    major = re.search(r"^#define IDENT_MAJOR\s+\d+\n", text, re.M)
    note = "// %s: the Mozilla root store as of %s.\n" % (version, date)
    return header, text[:major.end()] + note + text[major.end():], version


# The check.


def check_store_dir(store_dir):
    """Nothing but cacert.pem may hold certificates in the store: the anchors are retired, and a
    directory bundle path would take every .pem and .der in it."""
    errors = []
    for path in sorted(pathlib.Path(store_dir).iterdir()):
        if path.name == ANCHORS_NAME:
            errors.append("%s: the anchors are retired on ESP-IDF 6, which verifies cross-signed "
                          "chains (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY); remove "
                          "it" % ANCHORS_NAME)
        elif path.suffix in (".pem", ".der", ".crt") and path.name != PEM_NAME:
            errors.append("%s: only %s holds certificates in %s" % (path.name, PEM_NAME, STORE_DIR))
    return errors


def check(repo_root, release=False, today=None, now=None, effective=None):
    """Returns (errors, warnings). effective: see projects()."""
    pem_path, pins_path = store_paths(repo_root)
    pins = read_pins(pins_path)
    now = now or utc_now()
    errors = []
    if not re.fullmatch(r"[\w.-]+/[\w.-]+@[0-9a-f]{40}", pins["CA_CERTDATA_REF"]):
        errors.append("CA_CERTDATA_REF=%s is not owner/repo@<full commit SHA>"
                      % pins["CA_CERTDATA_REF"])
    if parse_iso(pins["CA_MOZILLA_DATE"]) > now + CLOCK_SLACK:
        errors.append("CA_MOZILLA_DATE=%s is in the future" % pins["CA_MOZILLA_DATE"])
    data = pem_path.read_bytes()
    if hashlib.sha256(data).hexdigest() != pins["CA_SHA256"]:
        errors.append("%s does not match CA_SHA256 in %s: the vendored store changed without "
                      "tools/ca_store/refresh.py" % (pem_path.name, PINS_NAME))
    text = data.decode("utf-8", "replace")
    try:
        if iso(mozilla_date(text)) != pins["CA_MOZILLA_DATE"]:
            errors.append("%s is from %s, CA_MOZILLA_DATE says %s"
                          % (pem_path.name, iso(mozilla_date(text)), pins["CA_MOZILLA_DATE"]))
        store = certificates(text)
        subjects = {}
        for der in store:
            subject = subject_and_key(der)[0]
            if subject in subjects and subjects[subject] != der:
                # ESP-IDF's lookup is a binary search that tries one root per subject, so which
                # of two would be used depends on where they sort.
                errors.append("two roots in the bundle have the same subject: %s and %s"
                              % (hashlib.sha256(subjects[subject]).hexdigest()[:16],
                                 hashlib.sha256(der).hexdigest()[:16]))
            subjects[subject] = der
    except StoreError as error:
        errors.append("%s: %s" % (pem_path.name, error))
        store = []
    errors += check_store_dir(pem_path.parent)
    if store and bundle_set_sha256(store) != pins["CA_BUNDLE_SET_SHA256"]:
        modules = [os.path.relpath(p, repo_root) for p, _ in projects(repo_root) if attaching(p)]
        errors.append("the bundle's certificates changed but CA_BUNDLE_SET_SHA256 did not: "
                      "update the store with tools/ca_store/refresh.py, which bumps IDENT_MINOR "
                      "of %s" % (", ".join(modules) or "no project (none attaches the bundle)"))
    errors += bundle_misconfigured(repo_root, effective)
    for project, sdkconfig in projects(repo_root, effective):
        limit = max_certs(sdkconfig)
        if limit is not None and len(store) > limit:
            errors.append("%s/sdkconfig: CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=%d, "
                          "the store has %d" % (os.path.relpath(project, repo_root), limit,
                                                len(store)))

    warnings = []
    today = today or now.date()
    age = (today - parse_iso(pins["CA_MOZILLA_DATE"]).date()).days
    limit = int(pins["CA_MAX_AGE_DAYS"])
    if age > limit:
        message = ("the Mozilla root store is %d days old (as of %s), over CA_MAX_AGE_DAYS=%d: "
                   "run tools/ca_store/refresh.py"
                   % (age, pins["CA_MOZILLA_DATE"], limit))
        (errors if release else warnings).append(message)
    return errors, warnings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("command", choices=("check",))
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument("--release", action="store_true",
                        help="a stale store is an error, not a warning (also RELEASE=1 in the "
                             "environment)")
    parser.add_argument("--project", help="with --sdkconfig-json: the project being built")
    parser.add_argument("--sdkconfig-json",
                        help="the build's config/sdkconfig.json, checked instead of the project's "
                             "sdkconfig")
    parser.add_argument("--today", type=datetime.date.fromisoformat, help=argparse.SUPPRESS)
    arguments = parser.parse_args(argv)
    repo_root = pathlib.Path(arguments.repo_root)
    if bool(arguments.project) != bool(arguments.sdkconfig_json):
        parser.error("--project and --sdkconfig-json go together")
    effective = {arguments.project: arguments.sdkconfig_json} if arguments.project else None
    release = arguments.release or os.environ.get("RELEASE", "") not in ("", "0")

    try:
        errors, warnings = check(repo_root, release, arguments.today, effective=effective)
    except (StoreError, OSError) as error:
        print("ca_store: %s" % error, file=sys.stderr)
        return 1

    for message in warnings:
        print("ca_store: warning: %s" % message, file=sys.stderr)
    for message in errors:
        print("ca_store: %s" % message, file=sys.stderr)
    if errors:
        return 1
    pins = read_pins(store_paths(repo_root)[1])
    print("ca_store: the Mozilla root store as of %s, sha256 %s"
          % (pins["CA_MOZILLA_DATE"], pins["CA_SHA256"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
