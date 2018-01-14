/*
    $Id: encoding.c 1442 2017-04-02 22:43:30Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include "encoding.h"
#include "error.h"
#include "string.h"
#include "ternary.h"
#include "unicode.h"
#include "values.h"

#include "strobj.h"
#include "bytesobj.h"
#include "typeobj.h"
#include "errorobj.h"

struct encoding_s *actual_encoding;

struct encoding_s {
    str_t name;
    str_t cfname;
    bool empty;
    bool failed;
    ternary_tree escape;
    struct avltree trans;
    struct avltree_node node;
};

struct trans2_s {
    uint16_t start;
    uint16_t end;
    uint8_t offset;
};

struct escape_s {
    size_t strlen;
    size_t len;
    uint8_t val[4];
    uint8_t *data;
};

static struct avltree encoding_tree;

static struct trans2_s no_trans[] = {
    {0x00, 0xff, 0x00},
};

static struct trans2_s petscii_trans[] = {
    {0x20, 0x40, 0x20}, /*  -@ */
    {0x41, 0x5a, 0xc1}, /* A-Z */
    {0x5b, 0x5b, 0x5b}, /* [ */
    {0x5d, 0x5d, 0x5d}, /* ] */
    {0x61, 0x7a, 0x41}, /* a-z */
    {0xa3, 0xa3, 0x5c}, /*  £ */
    {0x03c0, 0x03c0, 0xff}, /* π */
    {0x2190, 0x2190, 0x5f}, /* ← */
    {0x2191, 0x2191, 0x5e}, /* ↑ */
    {0x2500, 0x2500, 0xc0}, /* ─ */
    {0x2502, 0x2502, 0xdd}, /* │ */
    {0x250c, 0x250c, 0xb0}, /* ┌ */
    {0x2510, 0x2510, 0xae}, /* ┐ */
    {0x2514, 0x2514, 0xad}, /* └ */
    {0x2518, 0x2518, 0xbd}, /* ┘ */
    {0x251c, 0x251c, 0xab}, /* ├ */
    {0x2524, 0x2524, 0xb3}, /* ┤ */
    {0x252c, 0x252c, 0xb2}, /* ┬ */
    {0x2534, 0x2534, 0xb1}, /* ┴ */
    {0x253c, 0x253c, 0xdb}, /* ┼ */
    {0x256d, 0x256d, 0xd5}, /* ╭ */
    {0x256e, 0x256e, 0xc9}, /* ╮ */
    {0x256f, 0x256f, 0xcb}, /* ╯ */
    {0x2570, 0x2570, 0xca}, /* ╰ */
    {0x2571, 0x2571, 0xce}, /* ╱ */
    {0x2572, 0x2572, 0xcd}, /* ╲ */
    {0x2573, 0x2573, 0xd6}, /* ╳ */
    {0x2581, 0x2581, 0xa4}, /* ▁ */
    {0x2582, 0x2582, 0xaf}, /* ▂ */
    {0x2583, 0x2583, 0xb9}, /* ▃ */
    {0x2584, 0x2584, 0xa2}, /* ▄ */
    {0x258c, 0x258c, 0xa1}, /* ▌ */
    {0x258d, 0x258d, 0xb5}, /* ▍ */
    {0x258e, 0x258e, 0xb4}, /* ▎ */
    {0x258f, 0x258f, 0xa5}, /* ▏ */
    {0x2592, 0x2592, 0xa6}, /* ▒ */
    {0x2594, 0x2594, 0xa3}, /* ▔ */
    {0x2595, 0x2595, 0xa7}, /* ▕ */
    {0x2596, 0x2596, 0xbb}, /* ▖ */
    {0x2597, 0x2597, 0xac}, /* ▗ */
    {0x2598, 0x2598, 0xbe}, /* ▘ */
    {0x259a, 0x259a, 0xbf}, /* ▚ */
    {0x259d, 0x259d, 0xbc}, /* ▝ */
    {0x25cb, 0x25cb, 0xd7}, /* ○ */
    {0x25cf, 0x25cf, 0xd1}, /* ● */
    {0x25e4, 0x25e4, 0xa9}, /* ◤ */
    {0x25e5, 0x25e5, 0xdf}, /* ◥ */
    {0x2660, 0x2660, 0xc1}, /* ♠ */
    {0x2663, 0x2663, 0xd8}, /* ♣ */
    {0x2665, 0x2665, 0xd3}, /* ♥ */
    {0x2666, 0x2666, 0xda}, /* ♦ */
    {0x2713, 0x2713, 0xba}, /* ✓ */
};

/* PETSCII codes, must be sorted */
static const char *petscii_esc =
    "\x07" "{bell}\0"
    "\x90" "{black}\0"
    "\x90" "{blk}\0"
    "\x1f" "{blue}\0"
    "\x1f" "{blu}\0"
    "\x95" "{brn}\0"
    "\x95" "{brown}\0"
    "\xdf" "{cbm-*}\0"
    "\xa6" "{cbm-+}\0"
    "\xdc" "{cbm--}\0"
    "\x30" "{cbm-0}\0"
    "\x81" "{cbm-1}\0"
    "\x95" "{cbm-2}\0"
    "\x96" "{cbm-3}\0"
    "\x97" "{cbm-4}\0"
    "\x98" "{cbm-5}\0"
    "\x99" "{cbm-6}\0"
    "\x9a" "{cbm-7}\0"
    "\x9b" "{cbm-8}\0"
    "\x29" "{cbm-9}\0"
    "\xa4" "{cbm-@}\0"
    "\xde" "{cbm-^}\0"
    "\xb0" "{cbm-a}\0"
    "\xbf" "{cbm-b}\0"
    "\xbc" "{cbm-c}\0"
    "\xac" "{cbm-d}\0"
    "\xb1" "{cbm-e}\0"
    "\xbb" "{cbm-f}\0"
    "\xa5" "{cbm-g}\0"
    "\xb4" "{cbm-h}\0"
    "\xa2" "{cbm-i}\0"
    "\xb5" "{cbm-j}\0"
    "\xa1" "{cbm-k}\0"
    "\xb6" "{cbm-l}\0"
    "\xa7" "{cbm-m}\0"
    "\xaa" "{cbm-n}\0"
    "\xb9" "{cbm-o}\0"
    "\xa8" "{cbm-pound}\0"
    "\xaf" "{cbm-p}\0"
    "\xab" "{cbm-q}\0"
    "\xb2" "{cbm-r}\0"
    "\xae" "{cbm-s}\0"
    "\xa3" "{cbm-t}\0"
    "\xde" "{cbm-up arrow}\0"
    "\xb8" "{cbm-u}\0"
    "\xbe" "{cbm-v}\0"
    "\xb3" "{cbm-w}\0"
    "\xbd" "{cbm-x}\0"
    "\xb7" "{cbm-y}\0"
    "\xad" "{cbm-z}\0"
    "\x93" "{clear}\0"
    "\x93" "{clr}\0"
    "\x92" "{control-0}\0"
    "\x90" "{control-1}\0"
    "\x05" "{control-2}\0"
    "\x1c" "{control-3}\0"
    "\x9f" "{control-4}\0"
    "\x9c" "{control-5}\0"
    "\x1e" "{control-6}\0"
    "\x1f" "{control-7}\0"
    "\x9e" "{control-8}\0"
    "\x12" "{control-9}\0"
    "\x1b" "{control-:}\0"
    "\x1d" "{control-;}\0"
    "\x1f" "{control-=}\0"
    "\x00" "{control-@}\0"
    "\x01" "{control-a}\0"
    "\x02" "{control-b}\0"
    "\x03" "{control-c}\0"
    "\x04" "{control-d}\0"
    "\x05" "{control-e}\0"
    "\x06" "{control-f}\0"
    "\x07" "{control-g}\0"
    "\x08" "{control-h}\0"
    "\x09" "{control-i}\0"
    "\x0a" "{control-j}\0"
    "\x0b" "{control-k}\0"
    "\x06" "{control-left arrow}\0"
    "\x0c" "{control-l}\0"
    "\x0d" "{control-m}\0"
    "\x0e" "{control-n}\0"
    "\x0f" "{control-o}\0"
    "\x1c" "{control-pound}\0"
    "\x10" "{control-p}\0"
    "\x11" "{control-q}\0"
    "\x12" "{control-r}\0"
    "\x13" "{control-s}\0"
    "\x14" "{control-t}\0"
    "\x1e" "{control-up arrow}\0"
    "\x15" "{control-u}\0"
    "\x16" "{control-v}\0"
    "\x17" "{control-w}\0"
    "\x18" "{control-x}\0"
    "\x19" "{control-y}\0"
    "\x1a" "{control-z}\0"
    "\x0d" "{cr}\0"
    "\x9f" "{cyan}\0"
    "\x9f" "{cyn}\0"
    "\x14" "{delete}\0"
    "\x14" "{del}\0"
    "\x08" "{dish}\0"
    "\x11" "{down}\0"
    "\x09" "{ensh}\0"
    "\x1b" "{esc}\0"
    "\x82" "{f10}\0"
    "\x84" "{f11}\0"
    "\x8f" "{f12}\0"
    "\x85" "{f1}\0"
    "\x89" "{f2}\0"
    "\x86" "{f3}\0"
    "\x8a" "{f4}\0"
    "\x87" "{f5}\0"
    "\x8b" "{f6}\0"
    "\x88" "{f7}\0"
    "\x8c" "{f8}\0"
    "\x80" "{f9}\0"
    "\x97" "{gray1}\0"
    "\x98" "{gray2}\0"
    "\x9b" "{gray3}\0"
    "\x1e" "{green}\0"
    "\x97" "{grey1}\0"
    "\x98" "{grey2}\0"
    "\x9b" "{grey3}\0"
    "\x1e" "{grn}\0"
    "\x97" "{gry1}\0"
    "\x98" "{gry2}\0"
    "\x9b" "{gry3}\0"
    "\x84" "{help}\0"
    "\x13" "{home}\0"
    "\x94" "{insert}\0"
    "\x94" "{inst}\0"
    "\x9a" "{lblu}\0"
    "\x5f" "{left arrow}\0"
    "\x9d" "{left}\0"
    "\x0a" "{lf}\0"
    "\x99" "{lgrn}\0"
    "\x0e" "{lower case}\0"
    "\x96" "{lred}\0"
    "\x9a" "{lt blue}\0"
    "\x99" "{lt green}\0"
    "\x96" "{lt red}\0"
    "\x81" "{orange}\0"
    "\x81" "{orng}\0"
    "\xff" "{pi}\0"
    "\x5c" "{pound}\0"
    "\x9c" "{purple}\0"
    "\x9c" "{pur}\0"
    "\x1c" "{red}\0"
    "\x0d" "{return}\0"
    "\x92" "{reverse off}\0"
    "\x12" "{reverse on}\0"
    "\x1d" "{rght}\0"
    "\x1d" "{right}\0"
    "\x83" "{run}\0"
    "\x92" "{rvof}\0"
    "\x12" "{rvon}\0"
    "\x92" "{rvs off}\0"
    "\x12" "{rvs on}\0"
    "\x8d" "{shift return}\0"
    "\xc0" "{shift-*}\0"
    "\xdb" "{shift-+}\0"
    "\x3c" "{shift-,}\0"
    "\xdd" "{shift--}\0"
    "\x3e" "{shift-.}\0"
    "\x3f" "{shift-/}\0"
    "\x30" "{shift-0}\0"
    "\x21" "{shift-1}\0"
    "\x22" "{shift-2}\0"
    "\x23" "{shift-3}\0"
    "\x24" "{shift-4}\0"
    "\x25" "{shift-5}\0"
    "\x26" "{shift-6}\0"
    "\x27" "{shift-7}\0"
    "\x28" "{shift-8}\0"
    "\x29" "{shift-9}\0"
    "\x5b" "{shift-:}\0"
    "\x5d" "{shift-;}\0"
    "\xba" "{shift-@}\0"
    "\xde" "{shift-^}\0"
    "\xc1" "{shift-a}\0"
    "\xc2" "{shift-b}\0"
    "\xc3" "{shift-c}\0"
    "\xc4" "{shift-d}\0"
    "\xc5" "{shift-e}\0"
    "\xc6" "{shift-f}\0"
    "\xc7" "{shift-g}\0"
    "\xc8" "{shift-h}\0"
    "\xc9" "{shift-i}\0"
    "\xca" "{shift-j}\0"
    "\xcb" "{shift-k}\0"
    "\xcc" "{shift-l}\0"
    "\xcd" "{shift-m}\0"
    "\xce" "{shift-n}\0"
    "\xcf" "{shift-o}\0"
    "\xa9" "{shift-pound}\0"
    "\xd0" "{shift-p}\0"
    "\xd1" "{shift-q}\0"
    "\xd2" "{shift-r}\0"
    "\xa0" "{shift-space}\0"
    "\xd3" "{shift-s}\0"
    "\xd4" "{shift-t}\0"
    "\xde" "{shift-up arrow}\0"
    "\xd5" "{shift-u}\0"
    "\xd6" "{shift-v}\0"
    "\xd7" "{shift-w}\0"
    "\xd8" "{shift-x}\0"
    "\xd9" "{shift-y}\0"
    "\xda" "{shift-z}\0"
    "\x20" "{space}\0"
    "\x8d" "{sret}\0"
    "\x03" "{stop}\0"
    "\x0e" "{swlc}\0"
    "\x8e" "{swuc}\0"
    "\x09" "{tab}\0"
    "\x5e" "{up arrow}\0"
    "\x09" "{up/lo lock off}\0"
    "\x08" "{up/lo lock on}\0"
    "\x8e" "{upper case}\0"
    "\x91" "{up}\0"
    "\x05" "{white}\0"
    "\x05" "{wht}\0"
    "\x9e" "{yellow}\0"
    "\x9e" "{yel}\0"
    "\x00" "\0";

static struct trans2_s petscii_screen_trans[] = {
    {0x20, 0x3f, 0x20}, /*  -? */
    {0x40, 0x40, 0x00}, /* @ */
    {0x41, 0x5a, 0x41}, /* A-Z */
    {0x5b, 0x5b, 0x1b}, /* [ */
    {0x5d, 0x5d, 0x1d}, /* ] */
    {0x61, 0x7a, 0x01}, /* a-z */
    {0xa3, 0xa3, 0x1c}, /*  £ */
    {0x03c0, 0x03c0, 0x5e}, /* π */
    {0x2190, 0x2190, 0x1f}, /* ← */
    {0x2191, 0x2191, 0x1e}, /* ↑ */
    {0x2500, 0x2500, 0x40}, /* ─ */
    {0x2502, 0x2502, 0x5d}, /* │ */
    {0x250c, 0x250c, 0x70}, /* ┌ */
    {0x2510, 0x2510, 0x6e}, /* ┐ */
    {0x2514, 0x2514, 0x6d}, /* └ */
    {0x2518, 0x2518, 0x7d}, /* ┘ */
    {0x251c, 0x251c, 0x6b}, /* ├ */
    {0x2524, 0x2524, 0x73}, /* ┤ */
    {0x252c, 0x252c, 0x72}, /* ┬ */
    {0x2534, 0x2534, 0x71}, /* ┴ */
    {0x253c, 0x253c, 0x5b}, /* ┼ */
    {0x256d, 0x256d, 0x55}, /* ╭ */
    {0x256e, 0x256e, 0x49}, /* ╮ */
    {0x256f, 0x256f, 0x4b}, /* ╯ */
    {0x2570, 0x2570, 0x4a}, /* ╰ */
    {0x2571, 0x2571, 0x4e}, /* ╱ */
    {0x2572, 0x2572, 0x4d}, /* ╲ */
    {0x2573, 0x2573, 0x56}, /* ╳ */
    {0x2581, 0x2581, 0x64}, /* ▁ */
    {0x2582, 0x2582, 0x6f}, /* ▂ */
    {0x2583, 0x2583, 0x79}, /* ▃ */
    {0x2584, 0x2584, 0x62}, /* ▄ */
    {0x258c, 0x258c, 0x61}, /* ▌ */
    {0x258d, 0x258d, 0x75}, /* ▍ */
    {0x258e, 0x258e, 0x74}, /* ▎ */
    {0x258f, 0x258f, 0x65}, /* ▏ */
    {0x2592, 0x2592, 0x66}, /* ▒ */
    {0x2594, 0x2594, 0x63}, /* ▔ */
    {0x2595, 0x2595, 0x67}, /* ▕ */
    {0x2596, 0x2596, 0x7b}, /* ▖ */
    {0x2597, 0x2597, 0x6c}, /* ▗ */
    {0x2598, 0x2598, 0x7e}, /* ▘ */
    {0x259a, 0x259a, 0x7f}, /* ▚ */
    {0x259d, 0x259d, 0x7c}, /* ▝ */
    {0x25cb, 0x25cb, 0x57}, /* ○ */
    {0x25cf, 0x25cf, 0x51}, /* ● */
    {0x25e4, 0x25e4, 0x69}, /* ◤ */
    {0x25e5, 0x25e5, 0x5f}, /* ◥ */
    {0x2660, 0x2660, 0x41}, /* ♠ */
    {0x2663, 0x2663, 0x58}, /* ♣ */
    {0x2665, 0x2665, 0x53}, /* ♥ */
    {0x2666, 0x2666, 0x5a}, /* ♦ */
    {0x2713, 0x2713, 0x7a}, /* ✓ */
};

/* petscii screen codes, must be sorted */
static const char *petscii_screen_esc =
    "\x5f" "{cbm-*}\0"
    "\x66" "{cbm-+}\0"
    "\x5c" "{cbm--}\0"
    "\x30" "{cbm-0}\0"
    "\x29" "{cbm-9}\0"
    "\x64" "{cbm-@}\0"
    "\x5e" "{cbm-^}\0"
    "\x70" "{cbm-a}\0"
    "\x7f" "{cbm-b}\0"
    "\x7c" "{cbm-c}\0"
    "\x6c" "{cbm-d}\0"
    "\x71" "{cbm-e}\0"
    "\x7b" "{cbm-f}\0"
    "\x65" "{cbm-g}\0"
    "\x74" "{cbm-h}\0"
    "\x62" "{cbm-i}\0"
    "\x75" "{cbm-j}\0"
    "\x61" "{cbm-k}\0"
    "\x76" "{cbm-l}\0"
    "\x67" "{cbm-m}\0"
    "\x6a" "{cbm-n}\0"
    "\x79" "{cbm-o}\0"
    "\x68" "{cbm-pound}\0"
    "\x6f" "{cbm-p}\0"
    "\x6b" "{cbm-q}\0"
    "\x72" "{cbm-r}\0"
    "\x6e" "{cbm-s}\0"
    "\x63" "{cbm-t}\0"
    "\x5e" "{cbm-up arrow}\0"
    "\x78" "{cbm-u}\0"
    "\x7e" "{cbm-v}\0"
    "\x73" "{cbm-w}\0"
    "\x7d" "{cbm-x}\0"
    "\x77" "{cbm-y}\0"
    "\x6d" "{cbm-z}\0"
    "\x1f" "{left arrow}\0"
    "\x5e" "{pi}\0"
    "\x1c" "{pound}\0"
    "\x40" "{shift-*}\0"
    "\x5b" "{shift-+}\0"
    "\x3c" "{shift-,}\0"
    "\x5d" "{shift--}\0"
    "\x3e" "{shift-.}\0"
    "\x3f" "{shift-/}\0"
    "\x30" "{shift-0}\0"
    "\x21" "{shift-1}\0"
    "\x22" "{shift-2}\0"
    "\x23" "{shift-3}\0"
    "\x24" "{shift-4}\0"
    "\x25" "{shift-5}\0"
    "\x26" "{shift-6}\0"
    "\x27" "{shift-7}\0"
    "\x28" "{shift-8}\0"
    "\x29" "{shift-9}\0"
    "\x1b" "{shift-:}\0"
    "\x1d" "{shift-;}\0"
    "\x7a" "{shift-@}\0"
    "\x5e" "{shift-^}\0"
    "\x41" "{shift-a}\0"
    "\x42" "{shift-b}\0"
    "\x43" "{shift-c}\0"
    "\x44" "{shift-d}\0"
    "\x45" "{shift-e}\0"
    "\x46" "{shift-f}\0"
    "\x47" "{shift-g}\0"
    "\x48" "{shift-h}\0"
    "\x49" "{shift-i}\0"
    "\x4a" "{shift-j}\0"
    "\x4b" "{shift-k}\0"
    "\x4c" "{shift-l}\0"
    "\x4d" "{shift-m}\0"
    "\x4e" "{shift-n}\0"
    "\x4f" "{shift-o}\0"
    "\x69" "{shift-pound}\0"
    "\x50" "{shift-p}\0"
    "\x51" "{shift-q}\0"
    "\x52" "{shift-r}\0"
    "\x60" "{shift-space}\0"
    "\x53" "{shift-s}\0"
    "\x54" "{shift-t}\0"
    "\x5e" "{shift-up arrow}\0"
    "\x55" "{shift-u}\0"
    "\x56" "{shift-v}\0"
    "\x57" "{shift-w}\0"
    "\x58" "{shift-x}\0"
    "\x59" "{shift-y}\0"
    "\x5a" "{shift-z}\0"
    "\x20" "{space}\0"
    "\x1e" "{up arrow}\0"
    "\x00" "\0";

static struct trans2_s no_screen_trans[] = {
    {0x00, 0x1F, 0x80},
    {0x20, 0x3F, 0x20},
    {0x40, 0x5F, 0x00},
    {0x60, 0x7F, 0x40},
    {0x80, 0x9F, 0x80},
    {0xA0, 0xBF, 0x60},
    {0xC0, 0xFE, 0x40},
    {0xFF, 0xFF, 0x5E},
};

static int trans_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct trans_s *a = cavltree_container_of(aa, struct trans_s, node);
    const struct trans_s *b = cavltree_container_of(bb, struct trans_s, node);

    if (a->start > b->end) {
        return -1;
    }
    if (a->end < b->start) {
        return 1;
    }
    return 0;
}

static void trans_free(struct avltree_node *aa)
{
    struct trans_s *a = avltree_container_of(aa, struct trans_s, node);
    free(a);
}

static int encoding_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct encoding_s *a = cavltree_container_of(aa, struct encoding_s, node);
    const struct encoding_s *b = cavltree_container_of(bb, struct encoding_s, node);

    return str_cmp(&a->cfname, &b->cfname);
}

static void escape_free(void *e) {
    struct escape_s *esc = (struct escape_s *)e;
    if (esc->data != esc->val) free(esc->data);
    free(esc);
}

static void encoding_free(struct avltree_node *aa)
{
    struct encoding_s *a = avltree_container_of(aa, struct encoding_s, node);

    free((char *)a->name.data);
    if (a->name.data != a->cfname.data) free((uint8_t *)a->cfname.data);
    ternary_cleanup(a->escape, escape_free);
    avltree_destroy(&a->trans, trans_free);
    free(a);
}

static struct encoding_s *lasten = NULL;
struct encoding_s *new_encoding(const str_t *name, linepos_t epoint)
{
    struct avltree_node *b;
    struct encoding_s *tmp;

    if (lasten == NULL) {
        lasten = (struct encoding_s *)mallocx(sizeof *lasten);
    }
    str_cfcpy(&lasten->cfname, name);
    b = avltree_insert(&lasten->node, &encoding_tree, encoding_compare);
    if (b == NULL) { /* new encoding */
        str_cpy(&lasten->name, name);
        if (lasten->cfname.data == name->data) lasten->cfname = lasten->name;
        else str_cfcpy(&lasten->cfname, NULL);
        lasten->escape = NULL;
        lasten->empty = true;
        lasten->failed = false;
        avltree_init(&lasten->trans);
        tmp = lasten;
        lasten = NULL;
        return tmp;
    }
    tmp = avltree_container_of(b, struct encoding_s, node);
    if (tmp->failed && tmp->empty) err_msg2(ERROR__EMPTY_ENCODI, NULL, epoint);
    return tmp;            /* already exists */
}

static struct trans_s *lasttr = NULL;
struct trans_s *new_trans(struct trans_s *trans, struct encoding_s *enc)
{
    struct avltree_node *b;
    struct trans_s *tmp;
    if (lasttr == NULL) {
        lasttr = (struct trans_s *)mallocx(sizeof *lasttr);
    }
    lasttr->start = trans->start;
    lasttr->end = trans->end;
    lasttr->offset = trans->offset;
    b = avltree_insert(&lasttr->node, &enc->trans, trans_compare);
    if (b == NULL) { /* new encoding */
        tmp = lasttr;
        lasttr = NULL;
        enc->empty = false;
        return tmp;
    }
    return avltree_container_of(b, struct trans_s, node);            /* already exists */
}

static struct escape_s *lastes = NULL;
bool new_escape(const str_t *v, Obj *val, struct encoding_s *enc, linepos_t epoint)
{
    struct escape_s *b, tmp;
    Obj *val2;
    Iter *iter;
    iter_next_t iter_next;
    uval_t uval;
    size_t i, len;
    uint8_t *odata = NULL, *d;
    bool foundold;
    bool ret;

    if (lastes == NULL) {
        lastes = (struct escape_s *)mallocx(sizeof *lastes);
    }
    b = (struct escape_s *)ternary_insert(&enc->escape, v->data, v->data + v->len, lastes, false);
    if (b == NULL) err_msg_out_of_memory();
    foundold = (b != lastes);
    if (foundold) {
        odata = b->data;
        b->data = NULL; /* lock old one */
    }

    i = 0;
    lastes->data = NULL; /* lock new one */
    len = sizeof tmp.val;
    d = tmp.val;

    if (val->obj == STR_OBJ) {
        Obj *tmp2 = bytes_from_str((Str *)val, epoint, BYTES_MODE_TEXT);
        iter = tmp2->obj->getiter(tmp2);
        val_destroy(tmp2);
    } else iter = val->obj->getiter(val);

    iter_next = iter->v.obj->next;
    while ((val2 = iter_next(iter)) != NULL) {
        Error *err = val2->obj->uval(val2, &uval, 8, epoint);
        if (err != NULL) {
            err_msg_output_and_destroy(err);
            uval = 0;
        }
        if (i >= len) {
            if (i == sizeof tmp.val) {
                len = 16;
                d = (uint8_t *)mallocx(len);
                memcpy(d, tmp.val, i);
            } else {
                len += 1024;
                if (len < 1024) err_msg_out_of_memory(); /* overflow */
                d = (uint8_t *)reallocx(d, len);
            }
        }
        d[i++] = (uint8_t)uval;
        val_destroy(val2);
    }
    val_destroy(&iter->v);

    if (!foundold) { /* new escape */
        if (d == tmp.val) {
            memcpy(lastes->val, tmp.val, i);
            d = lastes->val;
        } else if (i < len) {
            d = (uint8_t *)reallocx(d, i);
        }
        lastes->strlen = v->len;
        lastes->len = i;
        lastes->data = d; /* unlock new */
        lastes = NULL;
        enc->empty = false;
        return false;
    }
    b->data = odata; /* unlock old */
    ret = (i != b->len || memcmp(d, b->data, i) != 0);
    if (tmp.val != d) free(d);
    return ret;            /* already exists */
}

static void add_esc(const char *s, struct encoding_s *enc) {
    str_t tmp;
    Bytes *tmp2;
    struct linepos_s nopoint = {0, 0};
    tmp2 = new_bytes(1);
    tmp2->len = 1;
    while (s[1] != 0) {
        tmp.data = (uint8_t *)s + 1;
        tmp.len = strlen(s + 1);
        tmp2->data[0] = (uint8_t)s[0];
        new_escape(&tmp, (Obj *)tmp2, enc, &nopoint);
        s += tmp.len + 2;
    }
    val_destroy(&tmp2->v);
}

static void add_trans(struct trans2_s *t, size_t ln, struct encoding_s *tmp) {
    size_t i;
    struct trans_s tmp2;
    for (i = 0; i < ln; i++) {
        tmp2.start = t[i].start;
        tmp2.end = t[i].end;
        tmp2.offset = t[i].offset;
        new_trans(&tmp2, tmp);
    }
}

static struct {
    size_t i, i2, j, len, len2;
    bool err;
    const uint8_t *data, *data2;
    linepos_t epoint;
    char mode;
} encode_state;

void encode_string_init(const Str *v, linepos_t epoint) {
    encode_state.i = 0;
    encode_state.j = 0;
    encode_state.len2 = 0;
    encode_state.len = v->len;
    encode_state.data = v->data;
    encode_state.epoint = epoint;
    encode_state.err = false;
}

void encode_error(Error_types no) {
    if (!encode_state.err) {
        struct linepos_s epoint = *encode_state.epoint;
        epoint.pos = interstring_position(&epoint, encode_state.data, encode_state.i2);
        err_msg2(no, NULL, &epoint);
        encode_state.err = true;
    }
}

int encode_string(void) {
    uchar_t ch;
    unsigned int ln;
    const struct escape_s *e;
    const struct avltree_node *c;
    const struct trans_s *t;
    struct trans_s tmp;

    if (encode_state.j < encode_state.len2) {
        return encode_state.data2[encode_state.j++];
    }
next:
    if (encode_state.i >= encode_state.len) return EOF;
    encode_state.i2 = encode_state.i;
    e = (struct escape_s *)ternary_search(actual_encoding->escape, encode_state.data + encode_state.i, encode_state.data + encode_state.len);
    if (e != NULL && e->data != NULL) {
        encode_state.i += e->strlen;
        encode_state.data2 = e->data;
        encode_state.len2 = e->len;
        if (encode_state.len2 < 1) goto next;
        encode_state.j = 1;
        return e->data[0];
    }
    ch = encode_state.data[encode_state.i];
    if ((ch & 0x80) != 0) ln = utf8in(encode_state.data + encode_state.i, &ch); else ln = 1;
    tmp.start = tmp.end = ch;

    c = avltree_lookup(&tmp.node, &actual_encoding->trans, trans_compare);
    if (c != NULL) {
        t = cavltree_container_of(c, struct trans_s, node);
        if (tmp.start >= t->start && tmp.end <= t->end) {
            encode_state.i += ln;
            return (uint8_t)(ch - t->start + t->offset);
        }
    }
    if (!encode_state.err && (!actual_encoding->empty || !actual_encoding->failed)) {
        struct linepos_s epoint = *encode_state.epoint;
        epoint.pos = interstring_position(&epoint, encode_state.data, encode_state.i);
        err_msg_unknown_char(ch, &actual_encoding->name, &epoint);
        actual_encoding->failed = true;
        encode_state.err = true;
    }
    encode_state.i += ln;
    return 256 + '?';
}

void init_encoding(bool toascii)
{
    struct encoding_s *tmp;
    static const str_t none_enc = {4, (const uint8_t *)"none"};
    static const str_t screen_enc = {6, (const uint8_t *)"screen"};
    struct linepos_s nopoint = {0, 0};

    avltree_init(&encoding_tree);

    if (!toascii) {
        tmp = new_encoding(&none_enc, &nopoint);
        if (tmp == NULL) {
            return;
        }
        add_trans(no_trans, lenof(no_trans), tmp);

        tmp = new_encoding(&screen_enc, &nopoint);
        if (tmp == NULL) {
            return;
        }
        add_trans(no_screen_trans, lenof(no_screen_trans), tmp);
    } else {
        tmp = new_encoding(&none_enc, &nopoint);
        if (tmp == NULL) {
            return;
        }
        add_esc(petscii_esc, tmp);
        add_trans(petscii_trans, lenof(petscii_trans), tmp);

        tmp = new_encoding(&screen_enc, &nopoint);
        if (tmp == NULL) {
            return;
        }
        add_esc(petscii_screen_esc, tmp);
        add_trans(petscii_screen_trans, lenof(petscii_screen_trans), tmp);
    }
}

void destroy_encoding(void)
{
    avltree_destroy(&encoding_tree, encoding_free);
    free(lasten);
    free(lasttr);
    free(lastes);
}
