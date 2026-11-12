#!/usr/bin/env python3
# Gate check: u64ctrl's network recovery trusts the committed Mozilla root list.

"""Verify the root list u64ctrl's recovery trusts, no device.

The recovery opens https:// servers with `esp_crt_bundle_attach`, which trusts
whatever bundle the build embeds. ESP-IDF 5.3.1's own list still holds three
roots Mozilla has removed (the emSign roots G3, C1 and C3, after the eMudhra
key compromise), so u64ctrl embeds `software/u64ctrl/ca/cacert.pem`, curl's
copy of Mozilla's list, instead. Three things have to stay true, and each is a
quiet regression: the sdkconfig drops back to ESP-IDF's list (a regenerated
sdkconfig does), the file is replaced by an older one, or the file is refreshed
and CI hands back the cached binary built from the old one.

Checks the file and the sdkconfig lines that select it. After a firmware
build it also checks what the build embedded: pass the build directory
(default software/u64ctrl/build), and the generated x509_crt_bundle, or failing
that u64ctrl.bin, must hold exactly the roots of cacert.pem.
"""

import base64
import datetime
import hashlib
import importlib.util
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROJECT = os.path.join(ROOT, "software", "u64ctrl")
SDKCONFIG = os.path.join(PROJECT, "sdkconfig")
SOFTWARE_DIR = os.path.join(ROOT, "software")

# Certificate sha256 fingerprints.
REQUIRED = {  # the roots both built-in recovery servers chain to
    "ISRG Root X1": "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",
    "ISRG Root X2": "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470",
}
REMOVED = {  # removed from Mozilla's list in NSS 3.131.1 (2026-10-20); G3 is bug 2084101, C1 and C3 bug 2083709
    "emSign ECC Root CA - G3": "86a1ecba089c4a8d3bbe2734c612ba341d813e043cf9e8a862cd5c57a36bbe6b",
    "emSign Root CA - C1": "125609aa301da0a249b97a8239cb6a34216f44dcac9f3954b14292f2e8c8608f",
    "emSign ECC Root CA - C3": "bc4d809b15189d78db3e1d8cf4f9726a795da1643ca5f1358e1ddb0edc0d7eb3",
}
# sha256 of the whole file, as curl publishes it next to cacert.pem: the file is curl's copy, unmodified.
# A refresh changes this, the date in ca/README.md and the lists above together.
CACERT_SHA256 = "796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe"
MAX_AGE_DAYS = 365  # the list is refreshed at least yearly; offline, so CI needs no network
MIN_ROOTS = 100  # Mozilla's list has about 120; an empty or truncated file is not it

PEM_BLOCK = re.compile(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", re.DOTALL)


def sdkconfig_settings(text):
    """CONFIG_NAME -> value for every set option; `# CONFIG_X is not set` is absent."""
    return dict(re.findall(r'^(CONFIG_\w+)=(.*)$', text, re.MULTILINE))


def fingerprints(pem_text):
    return {hashlib.sha256(base64.b64decode("".join(block.split()))).hexdigest()
            for block in PEM_BLOCK.findall(pem_text)}


def bundle_path(settings):
    """The file the sdkconfig selects, or the reason it selects none.

    The path is relative to the project directory, as ESP-IDF reads it.
    """
    for name in ("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE",
                 "CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE"):
        if settings.get(name) != "y":
            raise Failure(f"u64ctrl/sdkconfig does not set {name}=y, so the recovery trusts "
                          "ESP-IDF's own root list, which still holds the emSign roots")
    path = settings.get("CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH", "").strip('"')
    if not path or not os.path.isfile(os.path.join(PROJECT, path)):
        raise Failure(f"CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH={path!r} is not a file "
                      "under software/u64ctrl")
    return os.path.join(PROJECT, path)


def list_problems(pem_text, required=None, removed=None):
    """What is wrong with a root list: the roots it lacks, the removed ones it holds."""
    required = REQUIRED if required is None else required
    removed = REMOVED if removed is None else removed
    seen = fingerprints(pem_text)
    problems = []
    if len(seen) < MIN_ROOTS:
        problems.append(f"only {len(seen)} certificates (expected at least {MIN_ROOTS})")
    problems += [f"lacks {name}" for name, fp in required.items() if fp not in seen]
    problems += [f"holds {name}, which Mozilla removed" for name, fp in removed.items() if fp in seen]
    return problems


def der_element(der, pos):
    """(start of content, end) of the DER element at pos."""
    length = der[pos + 1]
    pos += 2
    if length & 0x80:
        count = length & 0x7F
        length = int.from_bytes(der[pos:pos + count], "big")
        pos += count
    return pos, pos + length


def cert_fields(der):
    """(serial, signature algorithm, issuer, validity, subject, SubjectPublicKeyInfo) DER of a certificate."""
    pos = der_element(der, 0)[0]            # Certificate
    pos = der_element(der, pos)[0]          # tbsCertificate
    if der[pos] == 0xA0:                    # [0] version, absent in a v1 certificate
        pos = der_element(der, pos)[1]
    fields = []
    for _ in range(6):
        end = der_element(der, pos)[1]
        fields.append(der[pos:end])
        pos = end
    return tuple(fields)


def subject_and_key(der):
    """(subject, SubjectPublicKeyInfo) DER of a certificate: what a bundle stores of it."""
    fields = cert_fields(der)
    return fields[4], fields[5]


def embedded_problems(embedded, pem_text, whole_bundle):
    """What is wrong with the bytes a build embedded, against the roots of cacert.pem.

    Every root's subject and key must be in it. A whole x509_crt_bundle must also have
    exactly their size (4 bytes of header each, plus 2 or 4 per root for the count or
    the offset table of ESP-IDF 5 or 6), so no root of ESP-IDF's own list is in it.
    The removed roots are looked for by name, which a binary holds too.
    """
    pairs = [subject_and_key(base64.b64decode("".join(block.split()))) for block in PEM_BLOCK.findall(pem_text)]
    problems = [f"lacks {len(pairs) - sum(n + k in embedded for n, k in pairs)} of the {len(pairs)} roots of cacert.pem"
                ] if not all(n + k in embedded for n, k in pairs) else []
    if whole_bundle:
        body = sum(4 + len(n) + len(k) for n, k in pairs)
        if len(embedded) not in (body + 2, body + 4 * len(pairs)):
            problems.append(f"is {len(embedded)} bytes, not the {body + 2} or {body + 4 * len(pairs)} "
                            "of a bundle made from cacert.pem")
    problems += [f"holds {name}" for name in REMOVED if name.encode() in embedded]
    return problems


def age_problem(pem_text, today):
    """Why a list is too old, from the 'Mozilla as of' date in its header; None when it is not."""
    match = re.search(r"Certificate data from Mozilla as of: \w+ (\w+ +\d+) [\d:]+ (\d{4}) GMT", pem_text)
    if not match:
        return "has no 'Certificate data from Mozilla as of' header"
    date = datetime.datetime.strptime(f"{match.group(1)} {match.group(2)}", "%b %d %Y").date()
    if (today - date).days > MAX_AGE_DAYS:
        return (f"is Mozilla data of {date}, more than {MAX_AGE_DAYS} days old: "
                "run software/u64ctrl/ca/refresh.py")
    return None


def run_selection_check():
    with check("the sdkconfig selects the committed root list"):
        with open(SDKCONFIG, encoding="utf-8") as handle:
            path = bundle_path(sdkconfig_settings(handle.read()))
        detail(f"bundle: {os.path.relpath(path, ROOT)}")
        return path


def run_contents_check(path):
    with check("the list holds ISRG Root X1 and X2 and none of the removed emSign roots"):
        with open(path, "rb") as handle:
            raw = handle.read()
        text = raw.decode("utf-8")
        problems = list_problems(text)
        if hashlib.sha256(raw).hexdigest() != CACERT_SHA256:
            problems.insert(0, "is not the file CACERT_SHA256 pins (curl's published cacert.pem.sha256); "
                               "a refresh changes both together")
        if problems:
            raise Failure("; ".join(problems))
        detail(f"{len(fingerprints(text))} roots")
    with check("the list is less than a year old, and a list that is older is caught"):
        problem = age_problem(text, datetime.date.today())
        if problem:
            raise Failure("cacert.pem " + problem)
        if not age_problem(text, datetime.date.today() + datetime.timedelta(days=2 * MAX_AGE_DAYS)):
            raise Failure("a list two years old was accepted")
    with check("a list that lacks X2 or holds a removed root is caught"):
        no_x2 = PEM_BLOCK.sub(
            lambda block: "" if hashlib.sha256(base64.b64decode("".join(block.group(1).split()))).hexdigest()
            == REQUIRED["ISRG Root X2"] else block.group(0), text)
        if not any("lacks ISRG Root X2" in p for p in list_problems(no_x2)):
            raise Failure("a list without ISRG Root X2 was accepted")
        # any root can stand in for a removed one once it is named as removed
        stand_in = {"stand-in": REQUIRED["ISRG Root X1"]}
        if not any("holds stand-in" in p for p in list_problems(text, removed=stand_in)):
            raise Failure("a list holding a removed root was accepted")


def run_embedded_check_mutants(text):
    """The check of a build's output accepts a bundle made from the list and refuses the others."""
    with check("the embedded-bundle check accepts the list's bundle and refuses an extra or a missing root"):
        pairs = [subject_and_key(base64.b64decode("".join(block.split()))) for block in PEM_BLOCK.findall(text)]
        entries = [len(n).to_bytes(2, "big") + len(k).to_bytes(2, "big") + n + k for n, k in pairs]
        bundle = len(pairs).to_bytes(2, "big") + b"".join(entries)       # ESP-IDF 5 layout
        if embedded_problems(bundle, text, True):
            raise Failure("a bundle made from the list was refused: " + "; ".join(embedded_problems(bundle, text, True)))
        extra = pairs[0][0] + b"x"
        longer = bundle + b"\0\0\0\0" + extra + pairs[0][1]
        if not embedded_problems(longer, text, True):
            raise Failure("a bundle with an extra root was accepted")
        if not embedded_problems(len(pairs).to_bytes(2, "big") + b"".join(entries[1:]), text, True):
            raise Failure("a bundle without a root was accepted")
        if not any("holds emSign ECC Root CA - G3" in p for p in embedded_problems(
                bundle + b"emSign ECC Root CA - G3", text, False)):
            raise Failure("an embedded removed root was not noticed")


def load_refresh(source=None):
    """refresh.py as a module; `source` is a (mutated) copy of its text."""
    path = os.path.join(PROJECT, "ca", "refresh.py")
    spec = importlib.util.spec_from_file_location("u64ctrl_ca_refresh", path)
    module = importlib.util.module_from_spec(spec)
    if source is None:
        spec.loader.exec_module(module)
    else:
        exec(compile(source, path, "exec"), module.__dict__)
    return module


def pem_of(der):
    return "-----BEGIN CERTIFICATE-----\n" + base64.b64encode(der).decode() + "\n-----END CERTIFICATE-----\n"


def run_refresh_checks(path):
    """refresh.py against a fake curl.se and a temporary copy of the tree, so no network is needed.

    Everything is derived from the committed list (its date, its roots), so a real refresh does not break this.
    """
    with check("refresh.py changes nothing unless every check passes, and then updates the three places"):
        refresh = load_refresh()
        with open(path, encoding="utf-8") as handle:
            committed = handle.read()
        # a newer list that has lost one root (the first that is not required), with a later date
        date = refresh.header_date(committed)
        later = date + datetime.timedelta(days=40)
        names = refresh.root_names(committed, sys.modules[__name__])
        victim = next(fp for fp in names if fp not in REQUIRED.values())
        name = names[victim]
        block = re.search(r"^" + re.escape(name) + r"\n=+\n(?:#[^\n]*\n)*-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n",
                          committed, re.MULTILINE | re.DOTALL)
        if block is None:
            raise Failure(f"could not find the block of {name} in cacert.pem; this check needs updating")
        newer = committed.replace(block.group(0), "").replace(
            committed[committed.index("as of: ") + 7:committed.index(" GMT")],
            later.strftime("%a %b %d 13:32:40 %Y"))
        if newer == committed or refresh.header_date(newer) != later:
            raise Failure("could not build the newer list; this check needs updating")
        roots = len(names)

        def served(data, listed=None):
            data = data.encode() if isinstance(data, str) else data
            digest = listed or hashlib.sha256(data).hexdigest()
            return lambda url: data if url.endswith(".pem") else (digest + "  cacert.pem\n").encode()

        def run(get, verify=lambda host, pem, ca: 4, pem=None):
            tree = tempfile.mkdtemp()
            try:
                for relative in ("software/u64ctrl/ca/cacert.pem", "software/u64ctrl/ca/README.md",
                                 "software/u64ctrl/main/recovery_trust.c", "tests/lib/u64ctrl_ca_test.py"):
                    os.makedirs(os.path.join(tree, os.path.dirname(relative)), exist_ok=True)
                    shutil.copy(os.path.join(ROOT, relative), os.path.join(tree, relative))
                if pem is not None:
                    Path(tree, "software/u64ctrl/ca/cacert.pem").write_text(pem)
                files = [os.path.join(tree, r) for r in ("software/u64ctrl/ca/cacert.pem", "software/u64ctrl/ca/README.md",
                                                         "tests/lib/u64ctrl_ca_test.py")]
                before = [Path(f).read_bytes() for f in files]
                out = io.StringIO()
                try:
                    result = refresh.refresh(tree, get, verify, out)
                    error = None
                except refresh.RefreshError as exc:
                    result, error = None, str(exc)
                stray = [f for f in os.listdir(os.path.dirname(files[0])) if f.endswith(".new")]
                return result, error, out.getvalue(), before, [Path(f).read_bytes() for f in files], stray
            finally:
                shutil.rmtree(tree)

        def untouched(outcome, what):
            if outcome[3] != outcome[4] or outcome[5]:
                raise Failure(f"{what} changed the tree")

        wrong = run(served(newer, "0" * 64))
        if not wrong[1] or "does not match" not in wrong[1]:
            raise Failure("a download that does not match the published sha256 was accepted")
        untouched(wrong, "a hash mismatch")
        few = run(served("".join(committed.splitlines(True)[:1200])))
        if not few[1]:
            raise Failure("a list with too few roots was accepted")
        untouched(few, "a truncated list")
        no_x2 = PEM_BLOCK.sub(lambda m: "" if hashlib.sha256(base64.b64decode("".join(m.group(1).split()))).hexdigest()
                              == REQUIRED["ISRG Root X2"] else m.group(0), newer)
        missing = run(served(no_x2))
        if not missing[1] or "ISRG Root X2" not in missing[1]:
            raise Failure("a list without ISRG Root X2 was accepted")
        untouched(missing, "a list without ISRG Root X2")
        older = run(served(committed.replace(committed[committed.index("as of: ") + 7:committed.index(" GMT")],
                                             (date - datetime.timedelta(days=30)).strftime("%a %b %d 13:32:40 %Y"))))
        if not older[1] or "older" not in older[1]:
            raise Failure("a list older than the committed one was accepted")
        untouched(older, "an older list")

        def refuse(host, pem, ca):
            raise refresh.RefreshError(f"{host}: the chain does not verify against the new list")
        server = run(served(newer), refuse)
        if not server[1]:
            raise Failure("a list that a recovery server does not verify against was accepted")
        untouched(server, "a failing server check")

        done = run(served(newer))
        if done[0] is not True or done[1]:
            raise Failure(f"a good new list was refused: {done[1]}")
        new_hash = hashlib.sha256(newer.encode()).hexdigest()
        if done[4][0] != newer.encode() or f'CACERT_SHA256 = "{new_hash}"'.encode() not in done[4][2] \
                or f"of {later} ({roots - 1} roots)".encode() not in done[4][1] or done[5]:
            raise Failure("the new list, its pin and the README date were not all updated")
        if f"removed: {name}" not in done[2] or "added:" in done[2]:
            raise Failure("the output does not list exactly the removed root:\n" + done[2])
        # the refreshed tree passes the checks that read it: the pin and the date the test now holds
        if re.search(r'^CACERT_SHA256 = "([0-9a-f]{64})"', done[4][2].decode(), re.MULTILINE).group(1) != new_hash:
            raise Failure("the pin in the test does not match the new list")
        same = run(served(committed))
        if same[0] is not False or "no change" not in same[2]:
            raise Failure("the committed list was not reported as no change")
        untouched(same, "an unchanged list")
        # upstream unchanged for longer than MAX_AGE_DAYS: say so, since refresh.py cannot clear the age check
        stale = committed.replace(committed[committed.index("as of: ") + 7:committed.index(" GMT")],
                                  (datetime.date.today() - datetime.timedelta(days=MAX_AGE_DAYS + 30)).strftime(
                                      "%a %b %d 13:32:40 %Y"))
        aged = run(served(stale), pem=stale)
        if aged[0] is not False or "age limit needs a maintainer decision" not in aged[2]:
            raise Failure("an unchanged list past MAX_AGE_DAYS was not reported")
        untouched(aged, "an unchanged old list")
        for url in ("http://curl.se/ca/cacert.pem", "ftp://curl.se/ca/cacert.pem"):
            try:
                refresh.fetch(url)
            except refresh.RefreshError:
                pass
            else:
                raise Failure(f"{url} was fetched")
        # a redirect to anything but https is refused, and fetch() uses the handler that does so
        handler = refresh.HttpsOnly()
        request = refresh.urllib.request.Request("https://curl.se/ca/cacert.pem")
        for target in ("http://example.org/cacert.pem", "ftp://example.org/cacert.pem"):
            try:
                handler.redirect_request(request, None, 302, "Found", {}, target)
            except refresh.RefreshError:
                continue
            raise Failure(f"a redirect to {target} was followed")
        if handler.redirect_request(request, None, 302, "Found", {}, "https://example.org/cacert.pem") is None:
            raise Failure("a redirect to https was refused")
        used = []

        class Spy:
            def open(self, *args, **kwargs):
                raise OSError("no network in this test")

        real = refresh.urllib.request.build_opener
        refresh.urllib.request.build_opener = lambda *handlers: used.extend(handlers) or Spy()
        try:
            refresh.fetch("https://curl.se/ca/cacert.pem")
        except refresh.RefreshError:
            pass
        finally:
            refresh.urllib.request.build_opener = real
        if refresh.HttpsOnly not in used:
            raise Failure("fetch() does not use the https-only redirect handler")


def chain_cases(refresh, committed):
    """What judge_chain must say about what `openssl s_client -showcerts` printed (synthesised here).
    Raises Failure if it says otherwise."""
    ca = sys.modules[__name__]
    ders = [base64.b64decode("".join(block.split())) for block in PEM_BLOCK.findall(committed)]
    x1 = next(d for d in ders if hashlib.sha256(d).hexdigest() == REQUIRED["ISRG Root X1"])
    reduced = "".join(pem_of(d) for d in ders if d != x1)       # a list that lacks the issuer of X1
    ok = "Certificate chain\n" + pem_of(x1) + "---\n    Verify return code: 0 (ok)\n"
    spoof = ("Certificate chain\n 0 s:CN = Verify return code: 0 (ok)\n   i:CN = x\n" + pem_of(x1)
             + "---\n    Verify return code: 18 (self-signed certificate)\n")

    def verdict(output, code, roots):
        try:
            refresh.judge_chain("host", output, code, roots, ca)
        except refresh.RefreshError as exc:
            return str(exc)
        return None

    if verdict(ok, 0, committed):
        raise Failure("a verified chain ending in a root of the list was refused")
    if not verdict(ok, 1, committed):
        raise Failure("a non-zero openssl exit code was ignored")
    if not verdict(spoof, 1, committed) or not verdict(spoof, 0, committed):
        raise Failure("a server name that spells the verify line was believed")
    if not verdict("Certificate chain\n" + pem_of(x1), 0, committed):
        raise Failure("a chain without the verify line was accepted")
    if not verdict("Verify return code: 0 (ok)\n", 0, committed):
        raise Failure("a chain without certificates was accepted")
    if "not issued by a root" not in (verdict(ok, 0, reduced) or ""):
        raise Failure("a chain ending in a certificate whose issuer is not in the list was accepted (mbedTLS would refuse it)")
    # a certificate of an unknown CA followed by an unrelated one whose issuer IS in the list: mbedTLS finds no
    # parent for the first, so the module refuses it, whatever follows
    unknown = next(d for d in ders if d != x1 and hashlib.sha256(d).hexdigest() not in REQUIRED.values())
    without = "".join(pem_of(d) for d in ders if d != unknown)
    trailing = "Certificate chain\n" + pem_of(unknown) + pem_of(x1) + "---\n    Verify return code: 0 (ok)\n"
    if "not issued by a root" not in (verdict(trailing, 0, without) or ""):
        raise Failure("an unrelated trailing certificate hid a chain that mbedTLS would refuse")
    if verdict(trailing, 0, committed):
        raise Failure("a chain of a root of the list followed by another root was refused")


def run_judge_checks(path):
    with check("the server check refuses a spoofed result, a failed exit code and a chain top the list does not issue"):
        refresh = load_refresh()
        with open(path, encoding="utf-8") as handle:
            committed = handle.read()
        chain_cases(refresh, committed)
        with open(os.path.join(PROJECT, "ca", "refresh.py"), encoding="utf-8") as handle:
            source = handle.read()
        # each guard in turn removed must make the cases fail
        for guard, off in (("    if returncode != 0:\n", "    if False:\n"),
                           ("    if not VERIFIED.search(output):\n", "    if False:\n"),
                           ("    if fields[top][2] not in subjects:\n", "    if False:\n"),
                           ("    if fields[top][2] not in subjects:\n",
                            "    top = len(fields) - 1\n    if fields[top][2] not in subjects:\n")):
            if source.count(guard) != 1:
                raise Failure(f"could not find {guard.strip()!r} in refresh.py; this check needs updating")
            try:
                chain_cases(load_refresh(source.replace(guard, off)), committed)
            except Failure:
                continue
            raise Failure(f"the cases still pass without {guard.strip()!r}")


def run_openssl_checks(path):
    """check_server against a real `openssl s_server` with a throwaway CA, when this openssl can do it."""
    refresh = load_refresh()
    problem = refresh.openssl_problem()
    if problem:
        detail("openssl s_client was not run against a local server: " + problem)
        return
    with check("check_server against a local openssl s_server: verified, unknown CA, spoofed name"):
        import socket
        import time
        ca_module = sys.modules[__name__]
        tmp = tempfile.mkdtemp()

        def openssl(*args):
            subprocess.run(["openssl", *args], cwd=tmp, check=True, capture_output=True, timeout=60)

        def serve(cn, chain):
            """Starts s_server for a leaf with this CN, sending the leaf and `chain`; returns (process, port)."""
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            openssl("req", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes", "-keyout", "leaf.key",
                    "-out", "leaf.csr", "-subj", f"/CN={cn}", "-addext", "subjectAltName=DNS:localhost")
            openssl("x509", "-req", "-in", "leaf.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
                    "-out", "leaf.pem", "-days", "2", "-copy_extensions", "copy")
            server = subprocess.Popen(["openssl", "s_server", "-accept", f"127.0.0.1:{port}", "-cert", "leaf.pem", "-key",
                                       "leaf.key", *(["-cert_chain", chain] if chain else []), "-quiet"], cwd=tmp,
                                      stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(50):
                try:
                    socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                    break
                except OSError:
                    time.sleep(0.1)
            return server, port

        try:
            openssl("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes", "-keyout",
                    "ca.key", "-out", "ca.pem", "-subj", "/CN=Test Root", "-days", "2",
                    "-addext", "basicConstraints=critical,CA:TRUE")
            shutil.copy(path, os.path.join(tmp, "committed.pem"))
            outcomes = {}
            for label, cn, chain, roots in (("ok", "localhost", "ca.pem", "ca.pem"),
                                            ("unknown CA", "localhost", "ca.pem", "committed.pem"),
                                            ("spoofed name", "Verify return code: 0 (ok)", "ca.pem", "committed.pem")):
                server, port = serve(cn, chain)
                try:
                    try:
                        refresh.check_server("localhost", os.path.join(tmp, roots), ca_module, port=port)
                        outcomes[label] = None
                    except refresh.RefreshError as exc:
                        outcomes[label] = str(exc)
                finally:
                    server.kill()
                    server.wait()
            if outcomes["ok"]:
                raise Failure("a server whose chain the list verifies was refused: " + outcomes["ok"])
            for label in ("unknown CA", "spoofed name"):
                if not outcomes[label]:
                    raise Failure(f"check_server accepted a server with an {label}")
        finally:
            shutil.rmtree(tmp)


def run_cache_key_check(path):
    """A refreshed list must change the ESP32 cache key, or CI hands back the old binary."""
    with check("the ESP32 cache key hashes the list"):
        result = subprocess.run([sys.executable, "esp_depends.py"], cwd=SOFTWARE_DIR,
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise Failure(f"esp_depends.py failed: {result.stderr.strip() or result.returncode}")
        relative = os.path.relpath(path, SOFTWARE_DIR)
        if not any(line.startswith(relative + ":") for line in result.stdout.splitlines()):
            raise Failure(f"software/esp_depends.py does not hash {relative}, so replacing it "
                          "would not rebuild u64ctrl in CI")


def run_build_check(path, build_dir):
    """What the build embedded, when there is a build output to look at."""
    bundle = os.path.join(build_dir, "esp-idf", "mbedtls", "x509_crt_bundle")
    binary = os.path.join(build_dir, "u64ctrl.bin")
    source = bundle if os.path.isfile(bundle) else binary if os.path.isfile(binary) else None
    if source is None:
        detail(f"no build output in {os.path.relpath(build_dir, ROOT)}: the embedded bundle was not checked")
        return
    with check("the bundle the build embedded was made from the list"):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        with open(source, "rb") as handle:
            embedded = handle.read()
        problems = embedded_problems(embedded, text, source == bundle)
        if problems:
            raise Failure(f"{os.path.relpath(source, ROOT)} " + "; ".join(problems))
        detail(os.path.relpath(source, ROOT))


def main():
    if not os.path.isfile(SDKCONFIG):
        suite_fail("u64ctrl_ca_test", f"missing {SDKCONFIG}")
        return 1
    try:
        path = run_selection_check()
        run_contents_check(path)
        with open(path, encoding="utf-8") as handle:
            run_embedded_check_mutants(handle.read())
        run_refresh_checks(path)
        run_judge_checks(path)
        run_openssl_checks(path)
        run_cache_key_check(path)
        run_build_check(path, sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJECT, "build"))
    except Failure as exc:
        suite_fail("u64ctrl_ca_test", str(exc))
        return 1

    detail("u64ctrl's recovery trusts the committed Mozilla list, without the removed emSign roots")
    suite_ok("u64ctrl_ca_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
