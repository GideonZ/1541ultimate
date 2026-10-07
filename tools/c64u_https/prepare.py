#!/usr/bin/env python3
"""Export the retail C64 Ultimate baseline plus the shared HTTP/HTTPS sources.

Offline preparation only: no device, network, install or flash operations.
The destination must be new. The source checkout is never changed.
"""
import argparse
import hashlib
import io
import json
import subprocess
import tarfile
from pathlib import Path

from httpd_patch import apply as apply_httpd_patch

BASELINE = "7b628eb166872965ea59d66f90ffd9bcf7d71d8a"
HTTPD = "ad73d3217bb0e1b3f37e0dbba595309b942aea17"
LWIP = "26a22151f4b7ebb2925523192edc83fa7f31cba9"
ROOT = Path(__file__).resolve().parents[2]

# Explicit allowlist: never copy another project's overlay or installer.
SHARED = (
    "software/network/http_connection.h", "software/network/http_request.h",
    "software/network/http_request.cc", "software/api/json.h", "software/api/json.cc",
    "software/components/mystring.h", "software/components/mystring.cc",
    "software/io/stream/stream_ramfile.h",
    "software/io/command_interface/command_target.h",
    "software/io/command_interface/command_intf.h",
    "software/io/command_interface/command_intf.cc",
    "software/io/command_interface/http_target.h",
    "software/io/command_interface/http_target.cc",
    "software/io/command_interface/test_http_target.cc",
    "software/io/uart/dma_uart.h", "software/io/uart/dma_uart.cc",
    "software/io/uart/tests/check_rx_rearm.py", "software/io/uart/tests/rx_rearm_fixture.cc",
    "software/network/sntp_time.cc",
    "software/u64ctrl/main/my_uart.h", "software/u64ctrl/main/my_uart.c",
    "software/u64ctrl/main/tls_uart.h", "software/u64ctrl/main/tls_uart.c",
    "software/u64ctrl/components/https_tls/CMakeLists.txt",
    "target/pc/linux/test_http_target/Makefile",
    "tools/c64u_https/check_menu_state.py",
    "tools/c64u_https/menu_state_fixture.cc",
    "tools/c64u_https/check_menu_pages.py",
    "tools/c64u_https/menu_pages_fixture.cc",
    "tools/c64u_https/check_menu_windows.py",
    "tools/c64u_https/menu_windows_fixture.cc",
)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected one integration anchor: {old[:100]!r}")
    return text.replace(old, new, 1)


def edit(root, path, old, new):
    p = root / path
    p.write_text(replace_once(p.read_text(), old, new))


def export(repo, revision, dest, *paths):
    data = git(repo, "archive", revision, *paths)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for item in archive:
            p = dest / item.name
            if not p.resolve().is_relative_to(dest.resolve()):
                raise ValueError("Archive path escaped destination")
            if item.isdir():
                p.mkdir(parents=True, exist_ok=True)
            elif item.isfile():
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(archive.extractfile(item).read())
                p.chmod(item.mode)
            else:
                raise ValueError(f"Unsupported archive member: {item.name}")


def prepare(source, dest):
    source, dest = source.resolve(), dest.resolve()
    if dest.exists():
        raise ValueError("Destination already exists; choose a new empty build directory")
    # Verify all git inputs before making the destination.
    for repo, revision in ((source, BASELINE), (source / "software/httpd", HTTPD),
                           (source / "software/lwip", LWIP)):
        if git(repo, "rev-parse", revision + "^{commit}").decode().strip() != revision:
            raise ValueError(f"Missing pinned source: {revision}")
    dest.mkdir(parents=True)
    export(source, BASELINE, dest, "software", "target", "tools", "roms", "html", "external")
    export(source / "software/httpd", HTTPD, dest / "software/httpd")
    response_patch = source / "tools/c64u_https/httpd-response.patch"
    apply_httpd_patch(dest / "software/httpd", response_patch)
    export(source / "software/lwip", LWIP, dest / "software/lwip")

    inputs = {"tools/c64u_https/httpd-response.patch": sha(response_patch.read_bytes())}
    heap_source = source / "tools/c64u_https/retail_heap.cc"
    inputs["tools/c64u_https/retail_heap.cc"] = sha(heap_source.read_bytes())
    route = dest / "software/api/route_machine.cc"
    route.write_text(route.read_text() + "\n" + heap_source.read_text())
    paths = list(SHARED)
    # Only source/test text; no generated executables, caches or certificates.
    for p in sorted((source / "software/network/https").rglob("*")):
        if p.is_file() and (p.suffix in (".c", ".h", ".cc", ".py", ".md") or p.name == "Makefile"):
            paths.append(p.relative_to(source).as_posix())
    for path in paths:
        data = (source / path).read_bytes()
        inputs[path] = sha(data)
        p = dest / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    # Backport only the existing upstream state-ownership repair. Configuration
    # roots remain borrowed; retail menu actions, cached pages and UI stay intact.
    edit(dest, "software/userinterface/tree_browser.h",
         "    virtual ~TreeBrowser();",
         "    virtual ~TreeBrowser();\n    void replace_root_state(TreeBrowserState *s);")
    edit(dest, "software/userinterface/tree_browser.cc",
         "void TreeBrowser :: init() // call on root!",
         """void TreeBrowser :: replace_root_state(TreeBrowserState *s)
{
    if (state) {
        delete state;
    }
    state = s;
    state_root = s;
}

void TreeBrowser :: init() // call on root!""")
    edit(dest, "software/userinterface/config_menu.cc",
         "    state = new ConfigBrowserState(root, this, level);",
         "    replace_root_state(new ConfigBrowserState(root, this, level));")

    # Retail actions allocate their own page; ordinary ConfigBrowser callers
    # still borrow static/cached roots. Keep that ownership distinction explicit.
    owned_header = source / "tools/c64u_https/owned_config_browser.h"
    inputs["tools/c64u_https/owned_config_browser.h"] = sha(owned_header.read_bytes())
    (dest / "software/userinterface/owned_config_browser.h").write_bytes(owned_header.read_bytes())
    menu = "software/userinterface/commodore_menu.cc"
    edit(dest, menu, '#include "config_menu.h"',
         '#include "config_menu.h"\n#include "owned_config_browser.h"')
    page_ctor = "new ConfigBrowser(menu->user_interface, configPage, 1)"
    path = dest / menu
    if path.read_text().count(page_ctor) != 2:
        raise ValueError("Expected exactly two retail owned page/group constructors")
    path.write_text(path.read_text().replace(page_ctor,
                    "new OwnedConfigBrowser(menu->user_interface, configPage, 1)"))
    edit(dest, menu, "new ConfigBrowser(menu->user_interface, configRoot)",
         "new OwnedConfigBrowser(menu->user_interface, configRoot)")
    edit(dest, "software/userinterface/config_menu.h",
         "    ~BrowsableConfigRoot()\n    {\n    }",
         """    ~BrowsableConfigRoot()
    {
        for (int i = 0; i < children.get_elements(); i++) {
            delete children[i];
        }
    }""")

    # Release windows when a persistent menu is hidden, before init replaces
    # its pointer. Preserve retail border restoration and cover direct teardown.
    edit(dest, "software/userinterface/context_menu.cc",
         "        window->reset_border();",
         "        window->reset_border();\n        delete window;\n        window = NULL;")
    edit(dest, "software/userinterface/context_menu.cc",
         "ContextMenu :: ~ContextMenu(void)\n{",
         "ContextMenu :: ~ContextMenu(void)\n{\n    delete window;\n    window = NULL;")
    edit(dest, "software/userinterface/context_menu.cc",
         "void ContextMenu :: draw()\n{",
         "void ContextMenu :: draw()\n{\n    if (!window) {\n        return;\n    }")

    # Keep retail IndexedList::remove's boolean return contract for old callers.
    # New JSON needs an index, so find it explicitly instead of replacing the
    # global container API underneath the retail file browser and drive code.
    edit(dest, "software/api/json.h", "int idx = values.remove(el);", """int idx = -1;
        for (int i = 0; i < values.get_elements(); ++i) {
            if (values[i] == el) { idx = i; break; }
        }
        if (idx >= 0) values.remove_idx(idx);""")

    # HTTP form encoding only; preserve retail filename conversion behavior.
    pattern = (source / "software/components/pattern.cc").read_text()
    marker = "void url_encode(const char *src, mstring &dest)"
    if pattern.count(marker) != 1:
        raise ValueError("Cannot locate upstream form encoder")
    path = dest / "software/components/pattern.cc"
    path.write_text(path.read_text() + "\n" + pattern[pattern.index(marker):])
    inputs["software/components/pattern.cc"] = sha((source / "software/components/pattern.cc").read_bytes())
    edit(dest, "software/components/pattern.h", "#define PATTERN_H",
         '#define PATTERN_H\n#include "mystring.h"\nvoid url_encode(const char *, mstring &);')

    # Retail CommoServe predates the shared HTTP client. Keep its behavior and
    # body layout, but do not export colliding helper names into the new client.
    assembly = dest / "software/network/assembly.cc"
    text = assembly.read_text()
    start = text.index("void url_encode(")
    end = text.index("void attachment_to_buffer(")
    text = text[:start] + text[end:]
    for symbol in ("attachment_to_buffer", "collect_in_buffer"):
        text = replace_once(text, "void " + symbol + "(", "static void " + symbol + "(")
    assembly.write_text(text.replace("t_BufferedBody", "AssemblyBufferedBody"))
    header = dest / "software/network/assembly.h"
    header.write_text(header.read_text().replace("t_BufferedBody", "AssemblyBufferedBody"))
    search = dest / "software/userinterface/assembly_search.cc"
    search.write_text(search.read_text().replace("t_BufferedBody", "AssemblyBufferedBody"))

    # Apply only HTTPS hooks to retail Wi-Fi/power behavior.
    wifi = "software/io/wifi/wifi_cmd.cc"
    edit(dest, wifi, '#include "dump_hex.h"', '#include "dump_hex.h"\n#if defined(ULTIMATE_HTTPS)\n#include "https_management.h"\n#endif')
    for signature, extra in (
        ("BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)",
         "    if(https_management_rx(context,buf,w))return pdTRUE;"),
        ("void wifi_command_init(void)", "    https_management_init();"),
        ("BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)",
         "    https_management_version(0,0);"),
    ):
        anchor = signature + "\n{"
        edit(dest, wifi, anchor, anchor + "\n#if defined(ULTIMATE_HTTPS)\n" + extra + "\n#endif")
    edit(dest, wifi, "        *minor = result->minor;", "        *minor = result->minor;\n#if defined(ULTIMATE_HTTPS)\n        https_management_version(*major,*minor);\n#endif")

    dispatch = "software/u64ctrl/main/rpc_dispatch.c"
    edit(dest, dispatch, '#include "sntp.h"', '#include "sntp.h"\n#include "tls_uart.h"')
    edit(dest, dispatch, "        switch(hdr->command) {", "        switch(hdr->command) {\n        case CMD_TLS_STREAM:\n            tls_uart_receive(pbuffer);\n            break;")
    edit(dest, dispatch, "void start_dispatch(QueueHandle_t queue)\n{", "void start_dispatch(QueueHandle_t queue)\n{\n    tls_uart_start();")
    edit(dest, "software/u64ctrl/main/rpc_calls.h", "#define EVENT_CONNECTED",
         "/* Experimental TLS stream v2 and read-only metrics; not upstream allocations. */\n#define CMD_TLS_STREAM 0x30\n#define CMD_TLS_METRICS 0x31\n\n#define EVENT_CONNECTED")
    edit(dest, "software/u64ctrl/main/rpc_dispatch.h", "ESP32 WiFi Bridge V1.11", "ESP32 WiFi Bridge V1.18 C64U HTTPS")
    edit(dest, "software/u64ctrl/main/rpc_dispatch.h", "#define IDENT_MINOR   11", "#define IDENT_MINOR   18")
    edit(dest, "software/u64ctrl/main/CMakeLists.txt", '"sntp.c"', '"sntp.c" "tls_uart.c"')
    edit(dest, "software/u64ctrl/sdkconfig", "# CONFIG_MBEDTLS_HAVE_TIME_DATE is not set", "CONFIG_MBEDTLS_HAVE_TIME_DATE=y")

    management = "target/u64ii/riscv/ultimate/Makefile"
    edit(dest, management, "SRCS_ASM =", "VPATH += $(PATH_SW)/network/https\nSRCS_C += https_client.c tls_wire.c tls_epoch.c\nSRCS_CC += http_request.cc http_target.cc https_management.cc\n\nSRCS_ASM =")
    edit(dest, management, "COPTIONS =", "OPTIONS += -DULTIMATE_HTTPS\nCOPTIONS =")
    # Do not claim the dirty feature tree is a clean retail release in System Info.
    rules = dest / "target/common/rules.mk"
    content = rules.read_text()
    start, end = content.index("gitinfo::"), content.index("\nmem:")
    content = content[:start] + "gitinfo:: | $(OUTPUT)\n\t@cp $(TOOLS)/c64u-https-gitinfo.h $(OUTPUT)/gitinfo.h\n" + content[end:]
    rules.write_text(content)
    inputs_digest = sha(json.dumps(inputs, sort_keys=True).encode())
    (dest / "tools/c64u-https-gitinfo.h").write_text(
        '#define APP_VERSION_TAG "C64U HTTPS TEST"\n'
        '#define APP_VERSION_BRANCH "retail-1.1.0-https"\n'
        '#define APP_VERSION_DATE "2026-03-16 15:05:47 +0100"\n'
        f'#define APP_VERSION_HASH "{BASELINE[:8]}+{inputs_digest[:8]}"\n'
        '#define APP_BUILD_DATE __DATE__ " " __TIME__\n'
        '#define APP_BUILD_MACHINE "c64u-https-build"\n')
    # Changes to common headers can affect retail callers; record every changed
    # exported file so that the complete adaptation is reviewable.
    manifest = {"format": 1, "product": "Commodore 64 Ultimate", "baseline": BASELINE,
                "feature_head": git(source, "rev-parse", "HEAD").decode().strip(),
                "httpd": HTTPD, "lwip": LWIP, "shared_inputs": inputs,
                "shared_inputs_sha256": inputs_digest,
                "recipe_sha256": sha(Path(__file__).read_bytes()),
                "hardware_tested": False, "install_performed": False,
                "files": {p.relative_to(dest).as_posix(): sha(p.read_bytes())
                          for p in sorted(dest.rglob("*")) if p.is_file()
                          and p.relative_to(dest).as_posix() != "tools/64tass/64tass"}}
    (dest / "c64u-https-source.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = prepare(args.source, args.output)
    print(f"Prepared C64 Ultimate retail {manifest['baseline'][:8]} + shared HTTPS in {args.output}")
    print("Offline source snapshot only; no device operations.")


if __name__ == "__main__":
    main()
