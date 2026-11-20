/*
 * recovery.c
 *
 * See recovery.h. The sequence follows recovery/u64ii/recover.py, which
 * revives a board over a JTAG cable: load the runtime FPGA, hold the CPU in
 * reset, write the application into memory, leave the boot magic for the
 * bootloader at 0xFFF8 and release the reset.
 */

#include <string.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "button_handler.h"
#include "jtag.h"
#include "pinout.h"
#include "recovery.h"
#include "recovery_image.h"
#include "recovery_manifest.h"
#include "recovery_url.h"
#include "recovery_verify.h"
#include "rpc_dispatch.h"

static const char *TAG = "recovery";

#define NVS_NAMESPACE   "recovery"
#define NVS_KEY_URL     "url"
#define NO_SERIAL       "NOSERIAL"  // what a board without a serial number asks as

/* Where the image may be loaded: above the bootloader and its boot magic, up to
   the end of the region the updater links into (target/u64ii/riscv/update). */
#define LOAD_MIN        0x00010000u
#define LOAD_MAX        0x04000000u
#define IMAGE_FAMILY    "U64MK2"

#define BOOT_MAGIC_ADDR 0x0000FFF8u
#define BOOT_MAGIC      0x1571BABEu
#define CPU_RESET       0x80

/* The last bytes of a bitstream carry the START command, about 1640 bytes from
   the end of every shipped bitstream; the recovery server's manifest.py
   refuses one where it is further back. Holding these back until the hash is checked means
   a bitstream damaged in transit cannot start, whatever its startup clock. It does not stop
   one crafted to carry its own START earlier: those bytes are shifted before the hash is
   known, so a server or a path that can swap the file can start a design of its choosing. */
#define FPGA_HOLDBACK   4096

/* recover.py writes memory in blocks of 16 KiB, each followed by 8 bytes of
   zeros; this does the same. */
#define BLOCK_SIZE      16384
#define BLOCK_PAD       8

#define READ_SIZE       4096
#define YIELD_EVERY     (256 * 1024)  // bytes of work between yields, for the idle task's watchdog
#define NETWORK_WAIT_MS 30000
#define TIME_WAIT_MS    10000

typedef enum {
    RECOVERY_DONE = 0,
    FAIL_NETWORK = 1,
    FAIL_NO_PACKAGE = 2,
    FAIL_MANIFEST = 3,
    FAIL_NO_FPGA = 4,
    FAIL_BITSTREAM = 5,
    FAIL_FPGA_START = 6,
    FAIL_IMAGE = 7,
    CANCELLED = 99,
} outcome_t;

extern esp_netif_t *my_sta_netif;  // wifi_modem.c
extern uint16_t connected_g;       // wifi_modem.c
extern void wifi_autoconnect(void);  // wifi_modem.c

static volatile bool running = false;
static volatile bool cancelled = false;

/* Static: the task stack holds the TLS session already. */
static char manifest_text[RECOVERY_MANIFEST_MAX_SIZE + 1];
static char manifest_url[256];
static char url[256];
static uint8_t read_buf[READ_SIZE];
static uint8_t fpga_tail[FPGA_HOLDBACK];
static uint8_t block[BLOCK_SIZE + BLOCK_PAD];

/* ------------------------------------------------------------------ LEDs */

typedef enum { LEDS_FREE, LEDS_BUSY, LEDS_DONE, LEDS_ERROR } led_mode_t;

#define LED_TICK_MS     100
#define LED_DONE_TICKS  (10000 / LED_TICK_MS)
#define LED_ERROR_TICKS (60000 / LED_TICK_MS)

static TimerHandle_t led_timer;
static volatile led_mode_t led_mode = LEDS_FREE;
static volatile int led_code;
static int led_ticks;

static void leds(int green, int red)
{
    gpio_set_level(IO_ESP_LED, !green);  // both are lit by a low level
    gpio_set_level(IO_ESP_LED_RED, !red);
}

static void led_tick(TimerHandle_t t)
{
    led_ticks++;
    switch (led_mode) {
        case LEDS_BUSY:
            leds((led_ticks / 2) & 1, 0);
            break;
        case LEDS_DONE:
            leds(1, 0);
            if (led_ticks >= LED_DONE_TICKS) {
                led_mode = LEDS_FREE;
            }
            break;
        case LEDS_ERROR: {
            // The code as that many blinks of half a second, then a pause.
            int period = 5 * led_code + 15;
            int phase = led_ticks % period;
            leds(0, (phase < 5 * led_code) && (phase % 5 < 2));
            if (led_ticks >= LED_ERROR_TICKS) {
                led_mode = LEDS_FREE;
            }
            break;
        }
        case LEDS_FREE:
            break;
    }
    if (led_mode == LEDS_FREE) {
        leds(0, 0);
        xTimerStop(t, 0);
    }
}

static void show(led_mode_t mode, int code)
{
    led_code = code;
    led_ticks = 0;
    led_mode = mode;
    if (!led_timer) {
        led_timer = xTimerCreate("recovery_led", pdMS_TO_TICKS(LED_TICK_MS), pdTRUE, NULL, led_tick);
    }
    if (led_timer) {
        xTimerStart(led_timer, 0);
    }
}

bool recovery_owns_leds(void)
{
    return running || led_mode != LEDS_FREE;
}

/* ------------------------------------------------------------------- NVS */

esp_err_t recovery_set_url(const char *new_url)
{
    if (!recovery_url_valid(new_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    char current[RECOVERY_URL_MAX + 1];
    if (recovery_get_url(current, sizeof(current)) == ESP_OK && strcmp(current, new_url) == 0) {
        return ESP_OK;  // spare the flash
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    if (new_url[0]) {
        err = nvs_set_str(handle, NVS_KEY_URL, new_url);
    } else {
        err = nvs_erase_key(handle, NVS_KEY_URL);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Recovery server set to '%s': %s", new_url, esp_err_to_name(err));
    return err;
}

esp_err_t recovery_get_url(char *out, size_t size)
{
    out[0] = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t len = size;
        err = nvs_get_str(handle, NVS_KEY_URL, out, &len);
        nvs_close(handle);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = 0;
        return ESP_OK;  // none set: not an error
    }
    if (err != ESP_OK) {
        out[0] = 0;
    }
    return err;
}

static void read_serial(char *serial, size_t size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("board", NVS_READONLY, &handle);  // written by cmd_set_serial()
    if (err == ESP_OK) {
        size_t len = size;
        err = nvs_get_str(handle, "serial", serial, &len);
        nvs_close(handle);
    }
    if (err != ESP_OK || !serial[0]) {
        strncpy(serial, NO_SERIAL, size);
        serial[size - 1] = 0;
    }
}

/* ------------------------------------------------------------------ HTTP */

/* The Location header of the response being read; see on_http_event(). */
static char location[256];

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "Location") == 0) {
        strncpy(location, evt->header_value, sizeof(location) - 1);
        location[sizeof(location) - 1] = 0;
    }
    return ESP_OK;
}

/* Opens `get_url` and follows redirects. Returns the client positioned at the
   body, with its status in `status`, or NULL when no response arrived. The URL
   the body came from goes to `final_url` when it is not NULL: the client's own
   esp_http_client_get_url() leaves out the port, so it is tracked here. */
static esp_http_client_handle_t http_open(const char *get_url, int *status, char *final_url, size_t final_size)
{
    static char current[256];
    static char next[256];
    esp_http_client_config_t cfg = {
        .url = get_url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = READ_SIZE,
        .buffer_size_tx = 1024,
        .event_handler = on_http_event,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return NULL;
    }
    strncpy(current, get_url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;
    // Following redirects by hand, as esp_https_ota does: the automatic
    // handling is only part of esp_http_client_perform().
    for (int redirects = 0; redirects <= 4; redirects++) {
        location[0] = 0;
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        int code = esp_http_client_get_status_code(client);
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            char drain[64];
            while (esp_http_client_read(client, drain, sizeof(drain)) > 0) {
            }
            // A redirect on the same host keeps the connection, and open()
            // only reconnects one it believes is closed. A server that closes
            // after each response would get the next request on a dead socket.
            esp_http_client_close(client);
            if (!location[0] || !recovery_url_resolve(next, sizeof(next), current, location) ||
                esp_http_client_set_url(client, next) != ESP_OK) {
                ESP_LOGW(TAG, "%s: cannot follow the redirect to '%s'", current, location);
                break;
            }
            strcpy(current, next);
            continue;
        }
        *status = code;
        if (final_url) {
            strncpy(final_url, current, final_size - 1);
            final_url[final_size - 1] = 0;
        }
        return client;
    }
    ESP_LOGW(TAG, "No response from %s", current);
    esp_http_client_cleanup(client);
    return NULL;
}

static void close_client(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

/* What streaming a file feeds: `len` more bytes at `offset`. False aborts. */
typedef bool (*sink_fn)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);

/* Streams the file at `get_url` into `sink`, and checks its size and hash. */
static bool stream_file(const char *get_url, const recovery_file_t *file, sink_fn sink, void *ctx)
{
    int status = 0;
    esp_http_client_handle_t client = http_open(get_url, &status, NULL, 0);
    if (!client) {
        return false;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "%s: HTTP %d", get_url, status);
        close_client(client);
        return false;
    }

    recovery_hash_t sha;
    if (recovery_hash_start(&sha) != RECOVERY_OK) {
        ESP_LOGW(TAG, "%s: cannot start a hash", get_url);
        close_client(client);
        return false;
    }
    uint32_t total = 0;
    uint32_t since_yield = 0;
    bool ok = true;
    TickType_t started = xTaskGetTickCount();

    while (ok && !cancelled) {
        int n = esp_http_client_read(client, (char *)read_buf, sizeof(read_buf));
        if (n < 0) {
            ESP_LOGW(TAG, "%s: read failed after %lu bytes", get_url, (unsigned long)total);
            ok = false;
            break;
        }
        if (n == 0) {
            break;
        }
        if ((uint32_t)n > file->size - total) {
            ESP_LOGW(TAG, "%s: longer than the %lu bytes its manifest says", get_url, (unsigned long)file->size);
            ok = false;
            break;
        }
        if (recovery_hash_update(&sha, read_buf, n) != RECOVERY_OK) {
            ESP_LOGW(TAG, "%s: cannot hash", get_url);
            ok = false;
            break;
        }
        ok = sink(ctx, total, read_buf, n);
        total += n;
        since_yield += n;
        if (since_yield >= YIELD_EVERY) {
            since_yield = 0;
            vTaskDelay(1);
        }
    }
    close_client(client);

    if (cancelled) {
        recovery_hash_abort(&sha);
        return false;
    }
    if (ok && total != file->size) {
        ESP_LOGW(TAG, "%s: %lu bytes, its manifest says %lu", get_url, (unsigned long)total, (unsigned long)file->size);
        ok = false;
    }
    if (ok) {
        recovery_result_t r = recovery_hash_finish(&sha, file);
        if (r != RECOVERY_OK) {
            ESP_LOGW(TAG, "%s: %s", get_url, r == RECOVERY_ERR_HASH_MISMATCH ? "hash differs from its manifest" : recovery_result_name(r));
            ok = false;
        }
    } else {
        recovery_hash_abort(&sha);
    }
    if (ok) {
        uint32_t ms = (xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "%s: %lu bytes in %lu ms", get_url, (unsigned long)total, (unsigned long)ms);
    }
    return ok;
}

/* -------------------------------------------------------------- Manifest */

static int count_keys(void)
{
    int n = 0;
    while (recovery_trusted_keys[n]) {
        n++;
    }
    return n;
}

static void wait_for_time(const char *server)
{
    // A certificate cannot be checked before SNTP has set the clock.
    if (strncmp(server, "https://", 8) != 0) {
        return;
    }
    for (int ms = 0; ms < TIME_WAIT_MS && time(NULL) < 1704067200 && !cancelled; ms += 250) {  // 2024-01-01
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

/* Asks `server` for a manifest, and checks it. RECOVERY_DONE when `m` holds
   one for this board; otherwise how far it got. */
static outcome_t ask_server(const char *server, const char *serial, uint32_t idcode, recovery_manifest_t *m)
{
    wait_for_time(server);
    if (!recovery_request_url(url, sizeof(url), server, serial, idcode, IDENT_MAJOR, IDENT_MINOR)) {
        ESP_LOGW(TAG, "Server URL too long: %s", server);
        return FAIL_NO_PACKAGE;
    }
    ESP_LOGI(TAG, "Asking %s", url);
    int status = 0;
    esp_http_client_handle_t client = http_open(url, &status, manifest_url, sizeof(manifest_url));
    if (!client) {
        return FAIL_NO_PACKAGE;
    }
    if (status != 200) {
        ESP_LOGI(TAG, "HTTP %d: no package here", status);
        close_client(client);
        return FAIL_NO_PACKAGE;
    }
    size_t len = 0;
    int n;
    while (len < sizeof(manifest_text) &&
           (n = esp_http_client_read(client, manifest_text + len, sizeof(manifest_text) - len)) > 0) {
        len += n;
    }
    close_client(client);

    recovery_signature_t sig;
    recovery_result_t r = recovery_manifest_split(manifest_text, len, &sig);
    if (r == RECOVERY_OK) {
        r = recovery_manifest_verify(manifest_text, &sig, recovery_trusted_keys, count_keys());
    }
    if (r == RECOVERY_OK) {
        r = recovery_manifest_parse(manifest_text, sig.signed_len, m);
    }
    if (r != RECOVERY_OK) {
        ESP_LOGW(TAG, "Manifest from %s refused: %s", manifest_url, recovery_result_name(r));
        return FAIL_MANIFEST;
    }
    if (!recovery_manifest_for_board(m, idcode, serial)) {
        ESP_LOGW(TAG, "Package %s is for %08lx, serials '%s'; not this board", m->package,
                 (unsigned long)m->idcode, m->serial);
        return FAIL_MANIFEST;
    }
    ESP_LOGI(TAG, "Package %s from %s", m->package, manifest_url);
    return RECOVERY_DONE;
}

/* Tries the configured server, then the built-in ones. Of those that fail, the
   one that got furthest is reported. */
static outcome_t find_package(const char *serial, uint32_t idcode, recovery_manifest_t *m)
{
    static char configured[RECOVERY_URL_MAX + 1];
    recovery_get_url(configured, sizeof(configured));
    outcome_t worst = FAIL_NO_PACKAGE;
    for (int i = -1; !cancelled; i++) {
        const char *server = (i < 0) ? configured : recovery_builtin_servers[i];
        if (!server) {
            break;
        }
        if (!server[0]) {
            continue;
        }
        outcome_t o = ask_server(server, serial, idcode, m);
        if (o == RECOVERY_DONE) {
            return o;
        }
        if (o == FAIL_MANIFEST) {
            worst = o;
        }
    }
    return cancelled ? CANCELLED : worst;
}

/* ------------------------------------------------------------- Bitstream */

typedef struct {
    uint32_t size;
    uint32_t shift_limit;  // bytes before this are shifted as they come; the rest wait
} fpga_load_t;

static bool fpga_sink(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    fpga_load_t *f = (fpga_load_t *)ctx;
    if (offset < f->shift_limit) {
        size_t now = f->shift_limit - offset;
        if (now > len) {
            now = len;
        }
        jtag_fpga_shift(data, (int)now);
        offset += now;
        data += now;
        len -= now;
    }
    if (len) {
        memcpy(fpga_tail + (offset - f->shift_limit), data, len);
    }
    return true;
}

static outcome_t load_bitstream(const recovery_manifest_t *m)
{
    if (!recovery_file_url(url, sizeof(url), manifest_url, m->fpga.name)) {
        return FAIL_BITSTREAM;
    }
    fpga_load_t f = {
        .size = m->fpga.size,
        .shift_limit = (m->fpga.size > FPGA_HOLDBACK) ? m->fpga.size - FPGA_HOLDBACK : 0,
    };
    ESP_LOGI(TAG, "Loading the FPGA from %s", url);
    jtag_fpga_begin();
    if (!stream_file(url, &m->fpga, fpga_sink, &f)) {
        jtag_fpga_clear();
        return cancelled ? CANCELLED : FAIL_BITSTREAM;
    }
    jtag_fpga_finish(fpga_tail, (int)(f.size - f.shift_limit));

    vTaskDelay(1000 / portTICK_PERIOD_MS);  // as recover.py waits
    uint32_t user = vji_get_user_code();
    ESP_LOGI(TAG, "FPGA started; user code %08lx", (unsigned long)user);
    if (user == 0 || user == 0xFFFFFFFF) {
        // Nothing answers on the user chain: the design did not start.
        jtag_fpga_clear();
        return FAIL_FPGA_START;
    }
    return RECOVERY_DONE;
}

/* ----------------------------------------------------------------- Image */

typedef struct {
    recovery_image_t reader;
    uint32_t addr;  // where block[0] goes
    size_t len;     // bytes in block
} image_load_t;

static void flush_block(image_load_t *im)
{
    if (im->len) {
        memset(block + im->len, 0, BLOCK_PAD);
        vji_write_memory(im->addr, block, (int)(im->len + BLOCK_PAD));
        im->len = 0;
    }
}

static bool write_memory(void *ctx, uint32_t addr, const uint8_t *data, size_t len)
{
    image_load_t *im = (image_load_t *)ctx;
    if (im->len && addr != im->addr + im->len) {
        flush_block(im);  // a new record
    }
    while (len) {
        if (!im->len) {
            im->addr = addr;
        }
        size_t now = BLOCK_SIZE - im->len;
        if (now > len) {
            now = len;
        }
        memcpy(block + im->len, data, now);
        im->len += now;
        addr += now;
        data += now;
        len -= now;
        if (im->len == BLOCK_SIZE) {
            flush_block(im);
        }
    }
    return true;
}

static bool image_sink(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    image_load_t *im = (image_load_t *)ctx;
    recovery_image_result_t r = recovery_image_feed(&im->reader, data, len);
    if (r != RECOVERY_IMAGE_OK) {
        ESP_LOGW(TAG, "Image refused at byte %lu: %s", (unsigned long)offset, recovery_image_result_name(r));
        return false;
    }
    return true;
}

static outcome_t load_image(const recovery_manifest_t *m)
{
    static image_load_t im;
    if (!recovery_file_url(url, sizeof(url), manifest_url, m->image.name)) {
        return FAIL_IMAGE;
    }
    memset(&im, 0, sizeof(im));
    recovery_image_init(&im.reader, LOAD_MIN, LOAD_MAX, IMAGE_FAMILY, write_memory, &im);

    ESP_LOGI(TAG, "Loading the update image from %s", url);
    vji_set_outputs(CPU_RESET);
    bool ok = stream_file(url, &m->image, image_sink, &im);
    uint32_t start = 0;
    if (ok) {
        flush_block(&im);
        recovery_image_result_t r = recovery_image_finish(&im.reader, &start);
        if (r != RECOVERY_IMAGE_OK) {
            ESP_LOGW(TAG, "Image refused: %s", recovery_image_result_name(r));
            ok = false;
        }
    }
    if (!ok) {
        // The CPU stays in reset and no boot magic was written; clearing the
        // FPGA takes the half loaded image with it.
        jtag_fpga_clear();
        return cancelled ? CANCELLED : FAIL_IMAGE;
    }

    uint32_t magic[2] = { start, BOOT_MAGIC };  // little endian, as recover.py packs it
    vji_write_memory(BOOT_MAGIC_ADDR, (const uint8_t *)magic, sizeof(magic));
    vji_set_outputs(0);
    ESP_LOGI(TAG, "Updater started at %08lx (%lu records, %lu bytes); user code %08lx", (unsigned long)start,
             (unsigned long)im.reader.records, (unsigned long)im.reader.loaded,
             (unsigned long)vji_get_user_code());
    return RECOVERY_DONE;
}

/* ------------------------------------------------------------------ Task */

/* An idle connector may wait up to WIFI_RETRY_MAX_MS before it looks again,
   longer than this waits, and it tries the hotspot only on a walk that starts
   while the recovery runs. */
static bool wait_for_network(void)
{
    if (!connected_g) {
        wifi_autoconnect();
    }
    for (int ms = 0; ms < NETWORK_WAIT_MS && !cancelled; ms += 250) {
        esp_netif_ip_info_t ip;
        if (connected_g && my_sta_netif && esp_netif_get_ip_info(my_sta_netif, &ip) == ESP_OK && ip.ip.addr) {
            return true;
        }
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
    return false;
}

/* The JTAG pins are wired in parallel with the header a cable plugs into, so
   they are driven only while they are in use: for the IDCODE, and for the
   load itself. They float while the recovery waits for the network and talks
   to the servers. */
static outcome_t recover(void)
{
    static recovery_manifest_t m;
    char serial[16];

    vTaskDelay(500 / portTICK_PERIOD_MS);  // let the supplies of the FPGA settle
    jtag_start();
    uint32_t idcode = jtag_get_id_code();
    jtag_stop();
    read_serial(serial, sizeof(serial));
    ESP_LOGI(TAG, "Recovering board %s, FPGA %08lx, %d trusted key(s)", serial, (unsigned long)idcode, count_keys());
    if ((idcode & 0xFFF) != 0x093) {  // Xilinx's manufacturer code, and the mandatory 1
        return FAIL_NO_FPGA;
    }
    if (!wait_for_network()) {
        return cancelled ? CANCELLED : FAIL_NETWORK;
    }
    outcome_t o = find_package(serial, idcode, &m);
    if (o != RECOVERY_DONE) {
        return o;
    }
    jtag_start();
    o = load_bitstream(&m);
    if (o == RECOVERY_DONE) {
        o = load_image(&m);
    }
    jtag_stop();  // the pins float again, for a cable or the next recovery
    return o;
}

static void recovery_task(void *arg)
{
    outcome_t o = recover();
    if (o == RECOVERY_DONE) {
        ESP_LOGI(TAG, "Recovery handed over to the updater.");
        show(LEDS_DONE, 0);
    } else if (o == CANCELLED) {
        ESP_LOGI(TAG, "Recovery cancelled.");
        show(LEDS_FREE, 0);
    } else {
        ESP_LOGW(TAG, "Recovery failed at step %d; switching off.", o);
        show(LEDS_ERROR, o);
        extern_button_event(BUTTON_OFF);
    }
    running = false;
    vTaskDelete(NULL);
}

void recovery_start(void)
{
    if (running) {
        return;
    }
    cancelled = false;
    running = true;
    show(LEDS_BUSY, 0);
    // Core 1, away from Wi-Fi: the JTAG shifts keep the core busy.
    if (xTaskCreatePinnedToCore(recovery_task, "recovery", 10240, NULL, tskIDLE_PRIORITY + 1, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Cannot start the recovery task.");
        running = false;
        show(LEDS_ERROR, FAIL_NETWORK);
    }
}

void recovery_cancel(void)
{
    if (running) {
        cancelled = true;
    }
}

bool recovery_running(void)
{
    return running;
}
