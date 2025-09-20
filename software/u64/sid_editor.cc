/*
 * sid_editor.cc
 *
 *  Created on: Sep 1, 2019
 *      Author: gideon
 */

#include "sid_editor.h"
#include <stdio.h>
#include "u64_config.h"

#define CFG_SID1_ADDRESS      0x02
#define CFG_SID2_ADDRESS      0x03
#define CFG_EMUSID1_ADDRESS   0x04
#define CFG_EMUSID2_ADDRESS   0x05
#define CFG_STEREO_DIFF       0x10
#define CFG_EMUSID_SPLIT      0x47
#define CFG_AUTO_MIRRORING    0x4A

SidEditor :: SidEditor(UserInterface *intf, ConfigStore *cfg)
{
    this->user_interface = intf;
    this->cfg = cfg;

    this->screen = 0;
    this->keyb = 0;
    this->window = 0;

    edit = 0;
    mirror_item = cfg->find_item(CFG_AUTO_MIRRORING);

    address_item[0] = cfg->find_item(CFG_SID1_ADDRESS);
    address_item[1] = cfg->find_item(CFG_SID2_ADDRESS);
    address_item[2] = cfg->find_item(CFG_EMUSID1_ADDRESS);
    address_item[3] = cfg->find_item(CFG_EMUSID2_ADDRESS);

    split_item[0] = cfg->find_item(CFG_STEREO_DIFF);
    split_item[1] = cfg->find_item(CFG_STEREO_DIFF);
    split_item[2] = cfg->find_item(CFG_EMUSID_SPLIT);
    split_item[3] = cfg->find_item(CFG_EMUSID_SPLIT);
}

SidEditor :: ~SidEditor()
{

}

void SidEditor :: init(Screen *scr, Keyboard *keyb)
{
    this->screen = scr;
    this->keyb = keyb;
    window = new Window(screen, (screen->get_size_x() - 40) >> 1, 2, 40, screen->get_size_y()-3);
    window->draw_border();
    window->clear();
    draw();
}

void SidEditor :: deinit(void)
{

}

int SidEditor :: poll(int)
{
    int ret = 0;
    int c = keyb->getch();

    if(c == -2) { // error
        return MENU_EXIT;
    }
    if(c >= 0) {
        ret = handle_key(c);
        if(ret < 0) {
            keyb->wait_free();
        }
    }
    return ret;
}

void SidEditor :: draw()
{
    const char hexchar[] = "0123456789ABCDEF";
    const char *sep = "\002\002\053\002\002\002\002\053\002\002\002\002\053\002\002\002\002\053\002\002\002\002\053\002\002\002\002\053\002\002\002\002\014";
    const char *opn = "\040\040\212\040\040\040\040\053\040\040\040\040\053\040\040\040\040\053\040\040\040\040\053\040\040\040\040\053\040\040\040\040\014";
    const char *end = "\040\040\005\040\040\040\040\215\040\040\040\040\215\040\040\040\040\215\040\040\040\040\215\040\040\040\040\215\040\040\040\040\006";

    if(user_interface->host && user_interface->host->is_accessible())
        user_interface->host->set_colors(0, user_interface->color_border);

    window->move_cursor(0, 0);
    window->output("  \004D400\004D500\004D600\004D700\004DE00\004DF00\004");
    for (int i=0; i < 8; i++) {
        window->move_cursor(0, 1 + 2*i);
        window->output(i == 0 ? sep : opn);
        window->move_cursor(0, 2 + 2*i);
        window->output(hexchar[2*i]);
        window->output("0\004    \004    \004    \004    \004    \004    \004");
    }
    window->move_cursor(0, 17);
    window->output(end);

    window->move_cursor(33, 3);
    window->set_color(13);
    window->reverse_mode(1);
    window->output("1");
    window->move_cursor(33, 4);
    window->output("SKT1 ");

    window->move_cursor(33, 6);
    window->set_color(14);
    window->reverse_mode(1);
    window->output("2");
    window->move_cursor(33, 7);
    window->output("SKT2 ");

    window->move_cursor(33, 9);
    window->set_color(7);
    window->reverse_mode(1);
    window->output("3");
    window->move_cursor(33, 10);
    window->output("USID1");

    window->move_cursor(33, 12);
    window->set_color(10);
    window->reverse_mode(1);
    window->output("4");
    window->move_cursor(33, 13);
    window->output("USID2");

    draw_edit();
    redraw();
}

void SidEditor :: redraw(void)
{
    uint8_t base[4];
    uint8_t base_orig[4];
    uint8_t mask[4];
    uint8_t split[4];

    U64Config :: get_sid_addresses(cfg, base, mask, split);
    for (int i=0; i<4; i++) {
        base_orig[i] = base[i];
    }
    U64Config :: fix_splits(base, mask, split);
    if (mirror_item->getValue()) {
        U64Config :: auto_mirror(base, mask, split, 4);
    }

    int colors[] = { 13, 14, 7, 10 };
    int darker[] = {  5,  6, 8,  2 };

    char m;
    // start column is 3 + 5 * base
    for(int i=0;i<4;i++) {
        int col = 3+i;
        for (int ah = 0x40; ah <= 0xF0; ah += 0x10, col += 5) {
            if (ah == 0x80) {
                ah = 0xe0;
            }
            for (int al = 0; al < 0x10; al += 2) {
                int a = ah + al;

                if ((a & mask[i]) == base[i]) {
                    switch (split[i]) {
                    case 0:
                        m = ' ';
                        break;
                    case 0x06:
                        m = 'A' + ((a & split[i]) >> 1);
                        break;
                    case 0x18:
                        m = 'A' + ((a & split[i]) >> 3);
                        break;
                    case 0x12:
                        m = 'A' + ((a & split[i] & 2) >> 1) + ((a & split[i] & 0x10) >> 3);
                        break;
                    default:
                        m = (a & split[i]) ? 'B' : 'A';
                    }
                    if (a == base_orig[i]) {
                        window->set_color(colors[i]);
                    } else {
                        window->set_color(darker[i]);
                    }
                    window->reverse_mode(1);
                    window->move_cursor(col, 2+al);
                    window->output(' ');
                    window->move_cursor(col, 3+al);
                    window->output(m);
                } else {
                    window->reverse_mode(0);
                    window->move_cursor(col, 2+al);
                    window->output(' ');
                    window->move_cursor(col, 3+al);
                    window->output(' ');
                }
            }
        }
    }
}

void SidEditor :: draw_edit(void)
{
    const char *sids[4] = { "Socket 1", "Socket 2", "UltiSid 1", "UltiSid 2" };
    extern const char *u64_sid_base[];
    extern const char *stereo_addr[];
    extern const char *sid_split[];

    window->reverse_mode(0);
    window->set_color(user_interface->color_fg);
    window->move_cursor(0, 18);
    window->output(sids[edit]);
    window->output(": ");
    window->output(u64_sid_base[address_item[edit]->getValue()]);
    window->output("       ");

    window->move_cursor(0, 19);
    window->output('S');
    window->reverse_mode(1);
    window->output('p');
    window->reverse_mode(0);
    window->output("lit: ");
    if (edit < 2) {
        window->output(stereo_addr[split_item[edit]->getValue()]);
    } else {
        window->output(sid_split[split_item[edit]->getValue()]);
    }
    window->output("        ");

    window->move_cursor(23, 19);
    window->reverse_mode(1);
    window->output('M');
    window->reverse_mode(0);
    window->output("irroring: ");
    window->output(mirror_item->getValue() ? "On " : "Off");
}

int SidEditor :: handle_key(int c)
{
//    if (c == 'r') {
//        redraw();
//    }
    switch (c) {
    case '1':
    case '2':
    case '3':
    case '4':
        edit = c - '1';
        draw_edit();
        break;

    case 'U':
    case 'u':
        address_item[edit]->setValue(0);
        draw_edit();
        redraw();
        break;

    case KEY_UP:
        address_item[edit]->previous(1);
        draw_edit();
        redraw();
        break;

    case KEY_DOWN:
        address_item[edit]->next(1);
        draw_edit();
        redraw();
        break;

    case KEY_RIGHT:
        address_item[edit]->next(8);
        draw_edit();
        redraw();
        break;

    case KEY_LEFT:
        address_item[edit]->previous(8);
        draw_edit();
        redraw();
        break;

    case 'p':
        split_item[edit]->next(1);
        draw_edit();
        redraw();
        break;

    case 'm':
        mirror_item->setValue(1 - mirror_item->getValue());
        draw_edit();
        redraw();
        break;

    case KEY_ESCAPE:
    case KEY_F8:
    case KEY_BREAK:
    case KEY_BACK:
        // restore ugly blue
        if(user_interface->host && user_interface->host->is_accessible())
            user_interface->host->set_colors(user_interface->color_bg, user_interface->color_border);

        return MENU_CLOSE;

    default:
        break;
    }

    return 0;
}
