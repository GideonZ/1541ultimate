/*
 * Host side tests for the update image reader of the control module.
 *
 *   make -C target/pc/linux/recoveryimage test
 *   RECOVERY_IMAGE=update.ue2:update.cfw make -C target/pc/linux/recoveryimage test
 *
 * The real software/u64ctrl/main/recovery_image.c is compiled here. The images
 * are built by the test in the layout tools/hex2bin -r writes; RECOVERY_IMAGE
 * names real images to stream as well, separated by ':'.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recovery_image.h"

/* The window the recovery task loads into: above the bootloader and the boot
   magic it reads at 0xFFF8, up to the end of the 0x03000000-0x03FFFFFF region
   target/u64ii/riscv/update/linker.x links the updater into. */
#define LOAD_MIN 0x00010000u
#define LOAD_MAX 0x04000000u

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

static void check_result(recovery_image_result_t got, recovery_image_result_t expected, const char *what)
{
    checks++;
    if (got != expected) {
        failures++;
        printf("  FAIL  %s (expected '%s', got '%s')\n", what, recovery_image_result_name(expected),
               recovery_image_result_name(got));
    } else {
        printf("  ok    %s\n", what);
    }
}

/* The board's memory, as far as the window reaches. */
static uint8_t *memory;
static uint32_t next_addr;     // where a write within the same record must continue
static int discontiguous;
static int fail_after;         // fail the write after this many bytes; -1 never
static uint32_t written;

static bool write_memory(void *ctx, uint32_t addr, const uint8_t *data, size_t len)
{
    if (next_addr && addr != next_addr) {
        discontiguous++;
    }
    if (fail_after >= 0 && written + len > (uint32_t)fail_after) {
        return false;
    }
    memcpy(memory + addr, data, len);
    next_addr = addr + (uint32_t)len;
    written += (uint32_t)len;
    return true;
}

/* An image under construction. */
static uint8_t image[64 * 1024];
static size_t image_len;

static void put32(uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        image[image_len++] = (uint8_t)(v >> (8 * i));
    }
}

static void preheader(const char *family, const char *product)
{
    put32(RECOVERY_IMAGE_FINGERPRINT);
    memset(image + image_len, 0, 32);
    strncpy((char *)image + image_len, family, 16);
    strncpy((char *)image + image_len + 16, product, 16);
    image_len += 32;
}

/* A record whose data byte i is (seed + i), padded as hex2bin pads it. */
static void record(uint32_t load, uint32_t length, uint32_t start, uint8_t seed)
{
    put32(load);
    put32(length);
    put32(start);
    for (uint32_t i = 0; i < length; i++) {
        image[image_len++] = (uint8_t)(seed + i);
    }
    while (image_len & 3) {
        image[image_len++] = 0xEE;  // padding must never reach memory
    }
}

static int memory_holds(uint32_t load, uint32_t length, uint8_t seed)
{
    for (uint32_t i = 0; i < length; i++) {
        if (memory[load + i] != (uint8_t)(seed + i)) {
            return 0;
        }
    }
    return 1;
}

static void reset_memory(void)
{
    memset(memory, 0, LOAD_MAX);
    next_addr = 0;
    discontiguous = 0;
    fail_after = -1;
    written = 0;
}

/* Feeds the image in pieces of `chunk` bytes, then finishes. */
static recovery_image_result_t load_in(size_t chunk, const char *family, uint32_t *start, recovery_image_t *im)
{
    reset_memory();
    recovery_image_init(im, LOAD_MIN, LOAD_MAX, family, write_memory, NULL);
    for (size_t pos = 0; pos < image_len; pos += chunk) {
        size_t n = (image_len - pos < chunk) ? image_len - pos : chunk;
        recovery_image_result_t r = recovery_image_feed(im, image + pos, n);
        if (r != RECOVERY_IMAGE_OK) {
            return r;
        }
    }
    return recovery_image_finish(im, start);
}

static recovery_image_result_t load(const char *family, uint32_t *start)
{
    recovery_image_t im;
    return load_in(image_len ? image_len : 1, family, start, &im);
}

static void test_single_record(void)
{
    static const size_t chunks[] = { 1, 3, 7, 12, 13, 4096, 0 };
    recovery_image_t im;
    uint32_t start;

    printf("A .ue2 with one record, as the releases are\n");
    image_len = 0;
    record(0x03000000, 1000, 0x03000000, 0x11);
    int all = 1;
    for (int i = 0; ; i++) {
        size_t chunk = chunks[i] ? chunks[i] : image_len;
        start = 0;
        if (load_in(chunk, "U64MK2", &start, &im) != RECOVERY_IMAGE_OK || start != 0x03000000 ||
            !memory_holds(0x03000000, 1000, 0x11) || im.loaded != 1000 || im.records != 1 || discontiguous) {
            printf("        failed when fed %zu bytes at a time\n", chunk);
            all = 0;
        }
        if (!chunks[i]) {
            break;
        }
    }
    check(all, "loads the same whatever pieces it arrives in");
    check(memory[0x03000000 + 1000] == 0 && memory[0x03000000 - 1] == 0, "writes nothing outside the record");
}

static void test_cfw(void)
{
    uint32_t start = 0;

    printf("A .cfw\n");
    image_len = 0;
    preheader("U64MK2", "");
    record(0x03000000, 64, 0x03000000, 0x22);
    check_result(load("U64MK2", &start), RECOVERY_IMAGE_OK, "of the expected family loads");
    check(start == 0x03000000 && memory_holds(0x03000000, 64, 0x22), "loads the records after its header");
    check_result(load(NULL, &start), RECOVERY_IMAGE_OK, "loads when no family is expected");

    image_len = 0;
    preheader("U64", "");
    record(0x03000000, 64, 0x03000000, 0x22);
    check_result(load("U64MK2", &start), RECOVERY_IMAGE_ERR_FAMILY, "of another family is refused");
    check(written == 0, "of another family writes nothing");

    image_len = 0;
    preheader("", "Ultimate 64");
    record(0x03000000, 64, 0x03000000, 0x22);
    check_result(load("U64MK2", &start), RECOVERY_IMAGE_OK, "without a family loads, as in the application");

    image_len = 0;
    preheader("U64MK2", "");
    check_result(load("U64MK2", &start), RECOVERY_IMAGE_ERR_NO_RECORDS, "with no records is refused");
}

static void test_records(void)
{
    uint32_t start = 0;
    recovery_image_t im;

    printf("Several records\n");
    image_len = 0;
    record(0x00030000, 5, 0, 0x30);           // 3 bytes of padding follow
    record(0x00040000, 6, 0x00040000, 0x40);  // 2 bytes
    record(0x00050000, 7, 0x00050002, 0x50);  // 1 byte
    record(0x00060000, 8, 0, 0x60);           // none
    check_result(load_in(5, NULL, &start, &im), RECOVERY_IMAGE_OK, "load");
    check(memory_holds(0x30000, 5, 0x30) && memory_holds(0x40000, 6, 0x40) &&
          memory_holds(0x50000, 7, 0x50) && memory_holds(0x60000, 8, 0x60), "each land where their header says");
    check(memory[0x30005] == 0 && memory[0x40006] == 0 && memory[0x50007] == 0, "keep their padding out of memory");
    check(im.records == 4 && im.loaded == 26, "are counted");
    check(start == 0x00050002, "start where the last record with a start address says");

    image_len = 0;
    record(0x03000000, 16, 0, 0x70);
    record(0, 0, 0x03000004, 0);
    check_result(load(NULL, &start), RECOVERY_IMAGE_OK, "may end in an empty record that carries the start");
    check(start == 0x03000004, "start from that empty record");
}

static void test_window(void)
{
    uint32_t start = 0;

    printf("The load window\n");
    image_len = 0;
    record(0x0000FFF8, 8, 0x03000000, 0);
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_RANGE, "refuses a record over the boot magic");
    check(written == 0, "writes nothing of a refused record");

    image_len = 0;
    record(LOAD_MAX - 8, 16, LOAD_MAX - 8, 0);
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_RANGE, "refuses a record that runs past its end");

    image_len = 0;
    record(LOAD_MAX - 8, 8, LOAD_MAX - 8, 0x80);
    check_result(load(NULL, &start), RECOVERY_IMAGE_OK, "takes a record that ends exactly at its end");

    image_len = 0;
    put32(0x03000000);
    put32(0xFFFFFFF0);
    put32(0x03000000);
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_RANGE, "refuses a length that wraps the address space");

    image_len = 0;
    record(0x03000000, 8, 0x0000FFF8, 0);
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_RANGE, "refuses a start outside it");
}

static void test_incomplete(void)
{
    uint32_t start = 0;
    recovery_image_t im;

    printf("Incomplete images\n");
    image_len = 0;
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_NO_RECORDS, "an empty stream");
    reset_memory();
    recovery_image_init(&im, LOAD_MIN, LOAD_MAX, NULL, write_memory, NULL);
    check_result(recovery_image_finish(&im, &start), RECOVERY_IMAGE_ERR_NO_RECORDS, "nothing fed at all");

    image_len = 0;
    record(0x03000000, 6, 0, 0);
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_NO_START, "no start address anywhere");

    static const struct { size_t cut; const char *what; } cuts[] = {
        { 2, "inside the first four bytes" },
        { 8, "inside a record header" },
        { 12 + 3, "inside the data" },
        { 12 + 6 + 1, "inside the padding" },
    };
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        image_len = 0;
        record(0x03000000, 6, 0x03000000, 0);
        image_len = cuts[i].cut;
        check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_TRUNCATED, cuts[i].what);
    }

    image_len = 0;
    preheader("U64MK2", "");
    image_len = 20;
    check_result(load(NULL, &start), RECOVERY_IMAGE_ERR_TRUNCATED, "inside a .cfw header");
}

static void test_write_failure(void)
{
    uint32_t start = 0;
    recovery_image_t im;
    uint8_t more[4] = { 0 };

    printf("A failing write\n");
    image_len = 0;
    record(0x03000000, 100, 0x03000000, 0);
    reset_memory();
    fail_after = 50;
    recovery_image_init(&im, LOAD_MIN, LOAD_MAX, NULL, write_memory, NULL);
    check_result(recovery_image_feed(&im, image, 12 + 40), RECOVERY_IMAGE_OK, "is not reached by the first part");
    check_result(recovery_image_feed(&im, image + 52, image_len - 52), RECOVERY_IMAGE_ERR_WRITE, "stops the load");
    check_result(recovery_image_feed(&im, more, sizeof(more)), RECOVERY_IMAGE_ERR_FAILED, "fails every later feed");
    check_result(recovery_image_finish(&im, &start), RECOVERY_IMAGE_ERR_FAILED, "fails the finish");
}

/* Streams a real image in uneven pieces and compares memory with the file. */
static void test_real_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        check(0, path);
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(size);
    size_t got = fread(file, 1, size, f);
    fclose(f);

    printf("%s\n", path);
    reset_memory();
    recovery_image_t im;
    recovery_image_init(&im, LOAD_MIN, LOAD_MAX, "U64MK2", write_memory, NULL);
    recovery_image_result_t r = RECOVERY_IMAGE_OK;
    size_t chunk = 1;
    for (size_t pos = 0; pos < got && r == RECOVERY_IMAGE_OK; ) {
        size_t n = (got - pos < chunk) ? got - pos : chunk;
        r = recovery_image_feed(&im, file + pos, n);
        pos += n;
        chunk = (chunk * 7 + 5) % 16411 + 1;  // uneven, like network reads
    }
    uint32_t start = 0;
    if (r == RECOVERY_IMAGE_OK) {
        r = recovery_image_finish(&im, &start);
    }
    check_result(r, RECOVERY_IMAGE_OK, "loads");
    printf("        %u record(s), %u bytes, start %08x\n", im.records, im.loaded, start);

    // Walk the records again and compare each with what landed in memory.
    size_t pos = (got >= 4 && file[0] == 0xDE && file[1] == 0xAD && file[2] == 0x15 && file[3] == 0x41) ? 36 : 0;
    int same = (r == RECOVERY_IMAGE_OK);
    while (same && pos + 12 <= got) {
        uint32_t load = file[pos] | (file[pos + 1] << 8) | (file[pos + 2] << 16) | ((uint32_t)file[pos + 3] << 24);
        uint32_t length = file[pos + 4] | (file[pos + 5] << 8) | (file[pos + 6] << 16) | ((uint32_t)file[pos + 7] << 24);
        same = memcmp(memory + load, file + pos + 12, length) == 0;
        pos += 12 + ((length + 3) & ~3u);
    }
    check(same && pos == got, "holds every record byte for byte");
    free(file);
}

int main(void)
{
    memory = calloc(1, LOAD_MAX);
    if (!memory) {
        printf("cannot allocate the memory model\n");
        return 2;
    }
    test_single_record();
    test_cfw();
    test_records();
    test_window();
    test_incomplete();
    test_write_failure();

    const char *real = getenv("RECOVERY_IMAGE");
    if (real && *real) {
        char *paths = malloc(strlen(real) + 1);
        strcpy(paths, real);
        for (char *p = strtok(paths, ":"); p; p = strtok(NULL, ":")) {
            test_real_image(p);
        }
        free(paths);
    }

    printf("\n%d checks, %d failed\n", checks, failures);
    free(memory);
    return failures ? 1 : 0;
}
