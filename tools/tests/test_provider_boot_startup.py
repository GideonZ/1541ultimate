"""Device-free boot identity, RF lifecycle and updater version regressions."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "software/u64ctrl/main"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class StartupTests(unittest.TestCase):
    def compile_run(self, code, *sources, args=(), defines=()):
        with tempfile.TemporaryDirectory(prefix="provider-boot-") as folder:
            work = Path(folder)
            (work / "test.c").write_text(code)
            (work / "cmd_buffer.h").write_text("#include <stdint.h>\ntypedef void command_buf_context_t;\n")
            binary = work / "test"
            command = [os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra",
                       "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                       "-I", str(work), "-I", str(MAIN), "-I",
                       str(ROOT / "software/network/tests/command_stubs"),
                       *defines, str(work / "test.c"), *map(str, sources), "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def lifecycle(self, zero=False):
        modem = (MAIN / "wifi_modem.c").read_text()
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "provider_boot.h"
#include "provider_identity.h"
typedef int esp_err_t;
#define ESP_OK 0
static int rf, fail_start, fills, zero_entropy, policy_ready;
static unsigned sar_reset = 0, sar_power = 7;
/* Model the side effects reported for IDF 5.3.1. Any call is a regression. */
void bootloader_random_enable(void) { sar_power = 3; }
void bootloader_random_disable(void) { sar_reset = 1; sar_power = 0; }
void esp_fill_random(void *bytes, size_t length) {
    assert(rf && length == PROVIDER_ID_SIZE);
    memset(bytes, zero_entropy ? 0 : 0x5a, length); ++fills;
}
static int esp_wifi_start(void) { if (fail_start) return -1; rf = 1; return ESP_OK; }
static int esp_wifi_stop(void) { rf = 0; return ESP_OK; }
static void provider_sources_wifi_stopping(void) { policy_ready = 0; }
static void provider_sources_wifi_started(int ok) { policy_ready = ok; }
'''
        code += function(modem, "static esp_err_t provider_wifi_start(void)")
        code += function(modem, "static esp_err_t provider_wifi_stop(void)")
        code += r'''
int main(int argc, char **argv) {
    (void)argv; zero_entropy = argc > 1;
    assert(!provider_boot_identity());
    fail_start = 1;
    assert(provider_wifi_start() == -1 && fills == 0 && !policy_ready);
    assert(!provider_boot_identity() && !sar_reset && sar_power == 7);
    fail_start = 0;
    assert(provider_wifi_start() == ESP_OK && policy_ready && fills == 1);
    const unsigned char *id = provider_boot_identity();
    assert(zero_entropy ? id == NULL : id != NULL);
    if (id) { for (unsigned i = 0; i < PROVIDER_ID_SIZE; ++i) assert(id[i] == 0x5a); }
    assert(!sar_reset && sar_power == 7);
    assert(provider_wifi_stop() == ESP_OK && !policy_ready && !rf);
    assert(provider_boot_identity() == id);
    fail_start = 1;
    assert(provider_wifi_start() == -1 && !policy_ready && fills == 1);
    fail_start = 0;
    assert(provider_wifi_start() == ESP_OK && policy_ready && fills == 1);
    assert(provider_boot_identity() == id && !sar_reset && sar_power == 7);
    return 0;
}
'''
        self.compile_run(code, MAIN / "provider_boot.c", args=("zero",) if zero else ())

    def test_rf_start_failure_and_restart_preserve_identity_and_adc(self):
        self.lifecycle()

    def test_zero_identity_remains_unavailable(self):
        self.lifecycle(zero=True)

    def test_no_identity_before_adc_and_rf_startup(self):
        control = function((MAIN / "control_main.c").read_text(), "void app_main(void)")
        self.assertNotIn("provider_boot_init", control)
        self.assertLess(control.index("configure_adc();"), control.index("setup_modem();"))
        modem = function((MAIN / "wifi_modem.c").read_text(), "void setup_modem()")
        self.assertLess(modem.index("wifi_init();"), modem.index("start_connector();"))
        self.assertLess(modem.index("wifi_init();"), modem.index("start_dispatch("))

    def test_module_versions_and_strings(self):
        for enabled, minor in [(False, 22), (True, 23)]:
            code = '''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define RECOVERYAPP 1
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#include "rpc_dispatch.h"
int main(void) {
    char expected[80];
    snprintf(expected, sizeof(expected), "ESP32 WiFi Bridge V%d.%d", IDENT_MAJOR, IDENT_MINOR);
    assert(!strcmp(expected, IDENT_STRING));
    assert(IDENT_MAJOR == 1 && IDENT_MINOR == EXPECTED_MINOR);
    /* The ordinary updater installs only strictly newer module versions. */
    assert(IDENT_MINOR > 21);
    return 0;
}
'''
            self.compile_run(code, defines=("-DEXPECTED_MINOR=" + str(minor),) +
                             (("-DCONFIG_ULTIMATE_PROVIDER_CONTROL=1",) if enabled else ()))


if __name__ == "__main__":
    unittest.main()
