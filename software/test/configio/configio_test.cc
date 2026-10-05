/*
 * configio_test.cc
 *
 * Host unit tests for the .cfg save/load rules in ConfigIO.
 *
 * Two behaviours are covered here because neither can be reached from the
 * device test harness:
 *
 *   1. Which stores are written to a .cfg file. Stores backed by a device
 *      rather than by an Ultimate flash page (the SID replacements) have to be
 *      written; the RTC store has to stay out. On real hardware a SID
 *      replacement store only exists when such a cartridge is plugged in, so
 *      the rule itself can only be proven here, where a page-less store can
 *      simply be constructed.
 *
 *   2. What an unknown item or store in a .cfg does. Loading a file written by
 *      a machine with different hardware is the normal case for this, and it
 *      must warn rather than fail: the warning goes to stdout, which the
 *      firmware routes to syslog.
 *
 *   3. An enum listed in its own order (ConfigItem::setListOrder). The menu,
 *      the +/- keys and REST follow the list order, while the value that is
 *      stored, and the label a .cfg names, stay those of the enum.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "configio.h"
#include "file.h"
#include "stream_textlog.h"

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (ok) {
        printf("ok   %s\n", what);
    } else {
        failures++;
        printf("FAIL %s\n", what);
    }
}

/* A File that lives in memory, so the tests never touch a filesystem. */
class MemFile : public File
{
    char buffer[4096];
    uint32_t length;
    uint32_t pos;
public:
    MemFile() : File(NULL), length(0), pos(0) { buffer[0] = 0; }

    void load(const char *text)
    {
        length = (uint32_t)strlen(text);
        if (length > sizeof(buffer) - 1) {
            length = sizeof(buffer) - 1;
        }
        memcpy(buffer, text, length);
        buffer[length] = 0;
        pos = 0;
    }

    const char *text()
    {
        buffer[length] = 0;
        return buffer;
    }

    FRESULT read(void *dest, uint32_t len, uint32_t *transferred) override
    {
        uint32_t avail = length - pos;
        if (len > avail) {
            len = avail;
        }
        memcpy(dest, buffer + pos, len);
        pos += len;
        *transferred = len;
        return FR_OK;
    }

    FRESULT write(const void *src, uint32_t len, uint32_t *transferred) override
    {
        if (length + len > sizeof(buffer) - 1) {
            len = sizeof(buffer) - 1 - length;
        }
        memcpy(buffer + length, src, len);
        length += len;
        *transferred = len;
        return FR_OK;
    }
};

/* Stands in for the RTC store: page-less, but deliberately not saved. */
class ExcludedStore : public ConfigStore
{
public:
    ExcludedStore(const char *name, t_cfg_definition *defs)
        : ConfigStore(NULL, name, defs, NULL) { }
    bool save_to_cfg_file(void) override { return false; }
};

static const char *chips[] = { "6581", "8580" };

static const char *listed[] = { "Zero", "One", "Two", "Three" };
static const uint8_t listed_order[] = { 2, 0, 3, 1 };

static t_cfg_definition order_defs[] = {
    { 0x01, CFG_TYPE_ENUM,  "Listed", "%s", listed, 0, 3, 0 },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 } };

/* Real ARMSID enum labels. They carry leading spaces so the menu can right
   align them, and one contains a space in the middle. Both shapes end up in a
   .cfg verbatim, so both have to survive a round trip. */
static const char *filtlow8[] = { "  30", " ~45", " ~70", "  100" };
static const char *filthi8[] = { "12 kHz", "10 kHz", "8 kHz" };

static t_cfg_definition sid_defs[] = {
    { 0x01, CFG_TYPE_ENUM,  "Emulation Mode", "%s", chips, 0, 1, 0 },
    { 0x02, CFG_TYPE_ENUM,  "8580 Lowest Filt Freq", "%s", filtlow8, 0, 3, 0 },
    { 0x03, CFG_TYPE_ENUM,  "8580 Highest Filt Freq", "%s", filthi8, 0, 2, 0 },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 } };

static t_cfg_definition clock_defs[] = {
    { 0x01, CFG_TYPE_VALUE, "Time Zone", "%d", NULL, -12, 12, 0 },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 } };

static bool contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

int main(int argc, char **argv)
{
    ConfigManager *cm = ConfigManager::getConfigManager();

    /* A SID replacement store: no flash page, but it must be saved. */
    ConfigStore *sid = new ConfigStore(NULL, "SID Socket 1: PDsid", sid_defs, NULL);
    cm->add_custom_store(sid);

    /* The RTC store: no flash page, and it must not be saved. */
    ExcludedStore *clock = new ExcludedStore("Clock Settings", clock_defs);
    cm->add_custom_store(clock);

    printf("-- which stores reach a .cfg file\n");
    MemFile out;
    ConfigIO::S_write_to_file(&out);
    const char *written = out.text();

    check(contains(written, "[SID Socket 1: PDsid]"),
          "a page-less SID store is written to the .cfg");
    check(contains(written, "Emulation Mode=6581"),
          "the SID store's item is written with its value");
    check(!contains(written, "[Clock Settings]"),
          "a store that opts out is kept out of the .cfg");

    printf("-- reading a .cfg back\n");
    sid->set_value(0x01, 1); // 8580, so the read below has something to change
    check(sid->get_value(0x01) == 1, "precondition: the SID store reads 8580");

    MemFile in;
    StreamTextLog log(4096);
    in.load("[SID Socket 1: PDsid]\nEmulation Mode=6581\n\n");
    bool ok = ConfigIO::S_read_from_file(&in, &log);
    check(ok, "a well-formed .cfg loads without error");
    check(sid->get_value(0x01) == 0, "the value from the file is applied to the store");

    printf("-- enum labels with spaces survive a round trip\n");
    /* An ARMSID writes "8580 Lowest Filt Freq= ~45" into a .cfg, padding and
       all. The padding is written verbatim and read back as the same choice,
       including a label whose own name contains spaces. */
    sid->set_value(0x02, 1);   // " ~45"
    sid->set_value(0x03, 0);   // "12 kHz"
    MemFile pad;
    ConfigIO::S_write_to_file(&pad);
    check(contains(pad.text(), "8580 Lowest Filt Freq= ~45"),
          "a padded enum label is written with its spaces intact");
    check(contains(pad.text(), "8580 Highest Filt Freq=12 kHz"),
          "an enum label containing a space is written whole");

    sid->set_value(0x02, 0);
    sid->set_value(0x03, 2);
    MemFile padback;
    StreamTextLog logpad(4096);
    padback.load("[SID Socket 1: PDsid]\n8580 Lowest Filt Freq= ~45\n"
                 "8580 Highest Filt Freq=12 kHz\n\n");
    check(ConfigIO::S_read_from_file(&padback, &logpad),
          "reading those values back is not an error");
    check(sid->get_value(0x02) == 1, "the padded label is matched exactly");
    check(sid->get_value(0x03) == 0, "the label with an inner space is matched");

    printf("-- a hand-edited file may space things how it likes\n");
    /* Nobody types the menu's alignment padding. "8580 Lowest Filt Freq=30" is
       what a person writes for the choice the firmware spells "  30", and it
       used to be rejected as an invalid choice. */
    sid->set_value(0x02, 3);
    sid->set_value(0x03, 2);
    sid->set_value(0x01, 0);
    MemFile hand;
    StreamTextLog log_hand(4096);
    hand.load("[SID Socket 1: PDsid]\n"
              "8580 Lowest Filt Freq=30\n"          /* label is "  30" */
              "8580 Highest Filt Freq=  12 kHz  \n" /* inner space kept */
              "  Emulation Mode  =  8580  \n"       /* spaces around both */
              "\n");
    check(ConfigIO::S_read_from_file(&hand, &log_hand),
          "a hand-spaced file loads without error");
    check(sid->get_value(0x02) == 0, "an unpadded value matches a padded label");
    check(sid->get_value(0x03) == 0, "outer spaces are ignored, the inner one is not");
    check(sid->get_value(0x01) == 1, "a padded item name finds its item");

    printf("-- tolerance is not laxity\n");
    sid->set_value(0x02, 1);
    MemFile bogus;
    StreamTextLog log_bogus(4096);
    bogus.load("[SID Socket 1: PDsid]\n8580 Lowest Filt Freq=31\n\n");
    check(!ConfigIO::S_read_from_file(&bogus, &log_bogus),
          "a value that is not a choice is still an error");
    check(sid->get_value(0x02) == 1, "and the store keeps its previous value");

    printf("-- an unknown item is a warning, not a failure\n");
    MemFile unknown_item;
    StreamTextLog log2(4096);
    unknown_item.load("[SID Socket 1: PDsid]\nFilter Strength=7\nEmulation Mode=8580\n\n");
    ok = ConfigIO::S_read_from_file(&unknown_item, &log2);
    check(ok, "an unknown item does not fail the load");
    check(sid->get_value(0x01) == 1,
          "items after the unknown one are still applied");

    printf("-- an unknown store is a warning, not a failure\n");
    MemFile unknown_store;
    StreamTextLog log3(4096);
    unknown_store.load("[SID Socket 2: PDsid]\nEmulation Mode=6581\n\n");
    ok = ConfigIO::S_read_from_file(&unknown_store, &log3);
    check(ok, "a store this machine does not have does not fail the load");
    check(sid->get_value(0x01) == 1, "an unknown store does not touch another store");

    printf("-- a malformed file is still an error\n");
    MemFile broken;
    StreamTextLog log4(4096);
    broken.load("[SID Socket 1: PDsid]\nEmulation Mode\n\n");
    ok = ConfigIO::S_read_from_file(&broken, &log4);
    check(!ok, "a line with no '=' is reported as an error");

    MemFile orphan;
    StreamTextLog log5(4096);
    orphan.load("Emulation Mode=6581\n\n");
    ok = ConfigIO::S_read_from_file(&orphan, &log5);
    check(!ok, "an item outside any store is reported as an error");

    printf("-- an enum listed in its own order\n");
    ConfigStore *ordered = new ConfigStore(NULL, "Ordered", order_defs, NULL);
    cm->add_custom_store(ordered);
    ConfigItem *item = ordered->find_item(0x01);
    item->setListOrder(listed_order);

    IndexedList<ConfigSetting *> choices(4, NULL);
    item->fetch_possible_settings(choices);
    check(choices.get_elements() == 4 && !strcmp(choices[0]->setting_name.c_str(), "Two") &&
          !strcmp(choices[3]->setting_name.c_str(), "One"),
          "the choices are listed in the list order");
    check(choices[0]->setting_index == 2 && choices[3]->setting_index == 1,
          "each listed choice still selects its own enum value");
    check(item->listPosition(2) == 0 && item->listPosition(1) == 3,
          "a value's position is its place in the list order");

    item->setValue(2);
    item->next(1);
    check(item->getValue() == 0, "+ steps to the next value in the list order");
    item->next(1);
    item->previous(1);
    check(item->getValue() == 0, "- steps back in the list order");
    item->setValue(1);
    item->next(1);
    check(item->getValue() == 2, "+ on the last listed value wraps to the first");
    item->previous(1);
    check(item->getValue() == 1, "- on the first listed value wraps to the last");
    item->setValue(2);
    item->next(5);
    check(item->getValue() == 0, "a step larger than the list wraps around it");
    item->setValue(2);
    item->previous(5);
    check(item->getValue() == 1, "a large step back wraps around it too");
    item->setValue(2);
    item->next(-1);
    check(item->getValue() == 1, "a negative step moves back in the list order");

    item->setValue(3);
    MemFile listed_out;
    ConfigIO::S_write_to_file(&listed_out);
    check(contains(listed_out.text(), "Listed=Three"),
          "a .cfg names the selected label, whatever the list order");
    MemFile listed_in;
    StreamTextLog log6(4096);
    listed_in.load("[Ordered]\nListed=One\n\n");
    check(ConfigIO::S_read_from_file(&listed_in, &log6) && item->getValue() == 1,
          "reading a .cfg selects the label's enum value, not a list position");

    item->setListOrder(NULL);
    item->setValue(1);
    item->next(1);
    check(item->getValue() == 2, "without a list order, + still steps in value order");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
