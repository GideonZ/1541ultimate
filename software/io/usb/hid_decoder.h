/*
 * hid_decoder.h
 *
 *  Created on: Feb 19, 2017
 *      Author: Gideon
 */

#ifndef _HID_DECODER_H
#define _HID_DECODER_H

#include "fifo.h"
#include "indexed_list.h"
#include "stack.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIZE_0 0x00
#define SIZE_1 0x01
#define SIZE_2 0x02
#define SIZE_4 0x03
#define SIZE_MASK 0x03

#define TYPE_MAIN 0x00
#define TYPE_GLOBAL 0x04
#define TYPE_LOCAL 0x08
#define TYPE_MASK 0x0C
#define ITEM_MASK 0xFC

#define MAIN_INPUT 0x80
#define MAIN_OUTPUT 0x90
#define MAIN_FEATURE 0xB0
#define MAIN_COLLECTION 0xA0
#define MAIN_END_COLL 0xC0

#define GLOBAL_USAGE_PAGE 0x04
#define GLOBAL_LOGICAL_MIN 0x14
#define GLOBAL_LOGICAL_MAX 0x24
#define GLOBAL_PHYSICAL_MIN 0x34
#define GLOBAL_PHYSICAL_MAX 0x44
#define GLOBAL_UNIT_EXP 0x54
#define GLOBAL_UNIT 0x64
#define GLOBAL_REPORT_SIZE 0x74
#define GLOBAL_REPORT_ID 0x84
#define GLOBAL_REPORT_COUNT 0x94
#define GLOBAL_PUSH 0xA4
#define GLOBAL_POP 0xB4

#define LOCAL_USAGE 0x08
#define LOCAL_USAGE_MIN 0x18
#define LOCAL_USAGE_MAX 0x28
#define LOCAL_DESIGNATOR_IDX 0x38
#define LOCAL_DESIGNATOR_MIN 0x48
#define LOCAL_DESIGNATOR_MAX 0x58
#define LOCAL_STRING_IDX 0x78
#define LOCAL_STRING_MIN 0x88
#define LOCAL_STRING_MAX 0x98
#define LOCAL_DELIMITER 0xA8

#define FLAGS_CONSTANT 0x01
#define FLAGS_VARIABLE 0x02
#define FLAGS_RELATIVE 0x04
// ...

#define COLLECTION_PHYSICAL 0x00
#define COLLECTION_APPLICATION 0x01
#define COLLECTION_LOGICAL 0x02

typedef struct
{
    uint32_t usage_page;
    int logical_minimum;
    int logical_maximum;
    int physical_minimum;
    int physical_maximum;
    int unit_exp;
    int unit;
    uint32_t report_size;
    uint32_t report_id;
    uint32_t report_count;
} t_globals;

typedef struct
{
    uint32_t usage;
    uint32_t usage_min;
    uint32_t usage_max;
    uint32_t designator_idx;
    uint32_t designator_min;
    uint32_t designator_max;
    uint32_t string_idx;
    uint32_t string_min;
    uint32_t string_max;
} t_locals;

typedef struct
{
    uint32_t usage;
    uint32_t flags;
    uint32_t length;
    uint32_t min, max;
} t_reportItem;

typedef struct {
    int offset;
    int length;
    int min, max;
    uint8_t flags;
    uint8_t report;
} t_item_location;

class UsageFifo
{
    Fifo<uint32_t> fifo;
    uint32_t lastUsage;

  public:
    UsageFifo() : fifo(256, 0) { lastUsage = 0; }

    uint32_t pop()
    {
        if (!fifo.is_empty()) {
            lastUsage = fifo.pop();
        }
        return lastUsage;
    }
    void push(uint32_t value) { fifo.push(value); }
    bool is_empty() { return fifo.is_empty(); }

    uint32_t head()
    {
        if (!fifo.is_empty()) {
            return fifo.head();
        }
        return lastUsage;
    }
};

class HidReport
{
  public:
    uint32_t applicationUsage;
    uint32_t reportId;
    IndexedList<t_reportItem *> items;
    HidReport(uint32_t u, uint32_t id) : items(16, NULL)
    {
        applicationUsage = u;
        reportId = id;
    }
    ~HidReport()
    {
        for (int i = 0; i < items.get_elements(); i++) {
            delete items[i];
        }
    }
    void addItem(uint32_t usage, uint32_t flags, uint32_t length, uint32_t min, uint32_t max)
    {
        t_reportItem *t = new t_reportItem;
        t->usage = usage;
        t->flags = flags;
        t->length = length;
        t->min = min;
        t->max = max;
        items.append(t);
    }
    char *getFlagsString(char *buffer, t_reportItem *t)
    {
        if (t->flags & FLAGS_CONSTANT) {
            strcpy(buffer, "Constant");
        } else if (t->flags & FLAGS_VARIABLE) {
            strcpy(buffer, "Variable");
        } else {
            strcpy(buffer, "Array");
        }
        strcat(buffer, (t->flags & FLAGS_RELATIVE) ? ", Relative" : ", Absolute");
        return buffer;
    }

    static int getValueFromData(uint8_t *data, t_item_location& loc)
    {
        int first_byte = loc.offset >> 3;
        int last_byte = (loc.offset + loc.length - 1) >> 3;
        int shift = loc.offset & 7;
        int result;
        // fits in any byte
        if (first_byte == last_byte) {
            result = (data[first_byte] >> shift);
        } else {
            uint32_t longer;
            uint8_t *pnt = data + first_byte;
            longer = *(pnt++);
            longer |= uint32_t(*(pnt++)) << 8;
            longer |= uint32_t(*(pnt++)) << 16;
            longer |= uint32_t(*(pnt++)) << 24;
            longer >>= shift;
            result = (int)longer;
        }
        result &= ((1 << loc.length) - 1); // mask properly
        if (loc.min < 0) { // signed
            if (result & (1 << (loc.length-1))) { // upper bit set
                result -= (1 << loc.length);
            }
        }
        return result;
    }
};

class HidItemList
{
  public:
    bool hasReportID;
    HidReport *inputReportList[256];
    HidReport *outputReportList[256];
    HidReport *featureReportList[256];

    HidItemList()
    {
        hasReportID = false;
        memset(inputReportList, 0, sizeof(inputReportList));
        memset(outputReportList, 0, sizeof(outputReportList));
        memset(featureReportList, 0, sizeof(featureReportList));
    }

    ~HidItemList()
    {
        for (int i = 0; i < 256; i++) {
            if (inputReportList[i])
                delete inputReportList[i];
            if (outputReportList[i])
                delete outputReportList[i];
            if (featureReportList[i])
                delete featureReportList[i];
        }
    }

    bool getInputItem(uint32_t appl, uint32_t id, t_item_location& loc)
    {
        int offset = hasReportID ? 8 : 0;
        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            if (r->applicationUsage != appl) {
                continue;
            }
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                if (item->usage == id) {
                    loc.report = r->reportId;
                    loc.offset = offset;
                    loc.length = item->length;
                    loc.min    = item->min;
                    loc.max    = item->max;
                    loc.flags  = item->flags;
                    printf("  %08x, %d bits from %d, (%d-%d) F:%02x (id:%02x)\n", item->usage, item->length, offset,
                           item->min, item->max, item->flags, r->reportId);
                    return true;
                }
                offset += item->length;
            }
            printf("\n");
        }
        return false;
    }

    void dump(void)
    {
        char buffer[64];

        printf("The reports %shave a report ID prepended.\n", hasReportID ? "" : "do NOT ");

        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            printf("Input Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }

        for (int i = 0; i < 256; i++) {
            HidReport *r = outputReportList[i];
            if (!r) {
                continue;
            }
            printf("Output Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }

        for (int i = 0; i < 256; i++) {
            HidReport *r = featureReportList[i];
            if (!r) {
                continue;
            }
            printf("Feature Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }
    }
};

class HidReportParser
{
    Stack<t_globals *> globalsStack;
    Stack<UsageFifo *> usageFifoStack;
    UsageFifo *usageFifo;
    t_globals globals;
    t_locals locals;
    uint32_t applicationUsage;

    int getValue(const uint8_t *descriptor, int i, uint8_t size, uint32_t *value, int *signed_value)
    {
        *value = 0;
        const int bytes[4] = {0, 1, 2, 4};
        for (int x = 0; x < bytes[size]; x++) {
            *value |= ((uint32_t)descriptor[i + x]) << (8 * x);
        }
        *signed_value = *value;
        switch (size) {
        case 1:
            *signed_value = (*value & 0x80) ? (int)*value - 256 : (int)*value;
            break;
        case 2:
            *signed_value = (*value & 0x8000) ? (int)*value - 0x10000 : (int)*value;
            break;
        }
        return bytes[size];
    }

    typedef enum { INPUT = 0, OUTPUT, FEATURE } t_report_type;

    HidReport *getReport(HidReport **reportList, uint32_t idx)
    {
        if (idx > 255) {
            idx = 255;
        }
        if (!reportList[idx]) {
            reportList[idx] = new HidReport(applicationUsage, idx);
        }
        return reportList[idx];
    }

    void iterate(HidReport **reportList, const char *type, uint32_t flags)
    {
        // variable data:  // each usage in the fifo maps to an entry in the report
        // variable array: // array: a usage map code will be in the array; the entries in the fifo are the "map" from
        // logical to usage constant        // there should not be any usage in the fifo.
        if (flags & FLAGS_CONSTANT) {
            printf("%s Constant with size %d bits\n", type, globals.report_count * globals.report_size);
            getReport(reportList, globals.report_id)
                ->addItem(0, flags, globals.report_count * globals.report_size, 0, 0);
        } else if (flags & FLAGS_VARIABLE) {
            printf("%s Variables with size %d bits, logical range (%d-%d): ", type, globals.report_size,
                   globals.logical_minimum, globals.logical_maximum);
            for (int i = 0; i < globals.report_count; i++) {
                uint32_t usage = usageFifo->pop();
                printf("%08lx ", usage);
                getReport(reportList, globals.report_id)
                    ->addItem(usage, flags, globals.report_size, globals.logical_minimum, globals.logical_maximum);
            }
            printf("\n");
        } else {
            printf("%s Array with %d elements with %d bits each. Mapping:\n", type, globals.report_count,
                   globals.report_size);
            getReport(reportList, globals.report_id)
                ->addItem(usageFifo->head(), flags, globals.report_size, globals.logical_minimum, globals.report_count);
            for (int i = globals.logical_minimum; i <= globals.logical_maximum; i++) {
                uint32_t usage = usageFifo->pop();
                printf(" %04x: %08lx\n", i, usage);
            }
        }
    }

    void clearLocal(void) { memset(&locals, 0, sizeof(t_locals)); }

    void clearGlobal(void) { memset(&globals, 0, sizeof(t_globals)); }

    void cleanup(void)
    {
        do {
            if (usageFifo)
                delete usageFifo;
            if (usageFifoStack.is_empty())
                break;
            usageFifo = usageFifoStack.pop();
        } while (1);
    }

    void init(void)
    {
        clearLocal();
        clearGlobal();
        usageFifo = new UsageFifo();
        applicationUsage = 0;
    }

  public:
    HidReportParser() : globalsStack(8, NULL), usageFifoStack(8, NULL)
    {
        usageFifo = NULL;
        applicationUsage = 0;
    }

    ~HidReportParser() { cleanup(); }

    int decode(HidItemList *result, const uint8_t *descriptor, int length)
    {
        cleanup();
        init();

        uint32_t *pGlobals = (uint32_t *)&globals;
        uint32_t *pLocals = (uint32_t *)&locals;
        t_globals *tempGlobals;

        uint32_t usage;

        for (int i = 0; i < length;) {
            uint8_t cmd = descriptor[i++];
            uint32_t value = 0;
            int signed_value = 0;

            int advance = getValue(descriptor, i, cmd & SIZE_MASK, &value, &signed_value);

            // printf("%03x(%d): %02x: %08x (%d)\n", i, advance, cmd, value, signed_value);
            i += advance;
            switch (cmd & ITEM_MASK) {
            case GLOBAL_PUSH:
                tempGlobals = new t_globals;
                *tempGlobals = globals; // struct copy
                globalsStack.push(tempGlobals);
                break;
            case GLOBAL_POP:
                if (!globalsStack.is_empty()) {
                    tempGlobals = globalsStack.pop();
                    globals = *tempGlobals; // struct copy
                    delete tempGlobals;
                }
                break;
            case GLOBAL_REPORT_ID:
                printf("Report ID %02X:\n", value);
                result->hasReportID = true;
                globals.report_id = value;
                break;
            case GLOBAL_USAGE_PAGE:
                globals.usage_page = value;
                break;
            case GLOBAL_LOGICAL_MIN:
                globals.logical_minimum = signed_value;
                break;
            case GLOBAL_LOGICAL_MAX:
                globals.logical_maximum = signed_value;
                break;
            case GLOBAL_PHYSICAL_MIN:
                globals.physical_minimum = signed_value;
                break;
            case GLOBAL_PHYSICAL_MAX:
                globals.physical_maximum = signed_value;
                break;
            case GLOBAL_UNIT_EXP:
                globals.unit_exp = signed_value;
                break;
            case GLOBAL_UNIT:
                globals.unit = signed_value;
                break;
            case GLOBAL_REPORT_SIZE:
                globals.report_size = value;
                break;
            case GLOBAL_REPORT_COUNT:
                globals.report_count = value;
                break;
            case LOCAL_USAGE_MIN:
                locals.usage_min = value;
                break;
            case LOCAL_DESIGNATOR_IDX:
                locals.designator_idx = value;
                break;
            case LOCAL_DESIGNATOR_MIN:
                locals.designator_min = value;
                break;
            case LOCAL_DESIGNATOR_MAX:
                locals.designator_max = value;
                break;
            case LOCAL_STRING_IDX:
                locals.string_idx = value;
                break;
            case LOCAL_STRING_MIN:
                locals.string_min = value;
                break;
            case LOCAL_STRING_MAX:
                locals.string_max = value;
                break;
            case LOCAL_DELIMITER:
                break;

            case MAIN_COLLECTION:
                usage = usageFifo->pop();
                usageFifoStack.push(usageFifo);
                usageFifo = new UsageFifo();
                switch (value) {
                case COLLECTION_LOGICAL:
                    printf("%08x: {\n", usage);
                    break;
                case COLLECTION_PHYSICAL:
                    break;
                case COLLECTION_APPLICATION:
                    printf("The following belongs to collection: %08x\n", usage);
                    applicationUsage = usage;
                    break;
                }
                break;
            case MAIN_END_COLL:
                delete usageFifo;
                usageFifo = usageFifoStack.pop();
                break;
            case MAIN_INPUT:
                iterate(result->inputReportList, "INPUT", value);
                break;
            case MAIN_OUTPUT:
                iterate(result->outputReportList, "OUTPUT", value);
                break;
            case MAIN_FEATURE:
                iterate(result->featureReportList, "FEATURE", value);
                break;
            case LOCAL_USAGE:
                usageFifo->push(globals.usage_page << 16 | value);
                break;
            case LOCAL_USAGE_MAX:
                for (uint32_t n = locals.usage_min & 0xFFFF; n <= (value & 0xFFFF); n++) {
                    usageFifo->push(globals.usage_page << 16 | n);
                }
                break;
            default:
                return -1;
            }

            if ((cmd & TYPE_MASK) == TYPE_MAIN) {
                clearLocal();
            }
        }
        return 0;
    }
};
#endif // _HID_DECODER_H