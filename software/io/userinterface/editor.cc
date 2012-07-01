#include "editor.h"
#include <string.h>

const char *test_text =
                  "Een editor is iemand die verantwoordelijk is voor de verwerking, bewerking en/of samenstelling "
                  "(montage) van beeld- en/of geluidsmateriaal tot een product dat geschikt is voor publicatie.\n\n"
                  "Een editor kan werkzaam zijn in de filmbranche, bij de televisie, bij de radio, of bij commerciële "
                  "audio/videobedrijven. Er is geen goede vertaling van het woord 'editor' in het Nederlands. "
                  "'Montagespecialist' komt dichtbij, maar is geen gangbare term. Overigens moet 'editor' vanuit "
                  "het Engels vertaald worden met 'redacteur'.\n\n"
                  "Verslaggevers die werken bij de snelle nieuwsvoorziening zijn meestal zelf ook de editor en de "
                  "regisseur van hun eigen opnamen. Eenvoudig samengestelde programma's kunnen gemakkelijk door de "
                  "maker(s) zelf worden gemonteerd. Maar bij ingewikkeldere producties, zoals grote documentaires "
                  "en vooral speelfilms, worden de taken meestal verdeeld en wordt het 'monteren' aan "
                  "specialisten overgelaten.";

struct Line empty_line = { NULL, 0 };

Editor :: Editor(char *text_buffer)
{
    parent_win = NULL;
    keyb = NULL;
    window = NULL;
    line_length = 38;
    
    text = new IndexedList<Line>(16, empty_line);
    if(!text_buffer)
        text_buffer = (char *)test_text;
        
    line_breakdown(text_buffer);
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

Editor :: ~Editor(void)
{
    delete text;
}

void Editor :: line_breakdown(char *text_buffer)
{
    Line current;

    char *c = text_buffer;
    char last;
    int last_space;

    // printf("Line length = %d\n", line_length);

    while(c) {
        current.buffer = c;
        last_space = -1;

        for(int i=0;i<line_length;i++) {
            last = c[i];
            if((last == 0x0a)||(last == 0x0d)||(last == 0)) {
                current.length = i;
                // printf("adding returned line = %d\n", current.length);
                text->append(current);
                if(last == 0)
                    return;
                c = c + i + 1;
                if((*c == 0x0a)&&(last == 0x0d))
                    c++;
                i = -1;
                last_space = -1;
                current.buffer = c;
                // printf("First char = %b\n", *c);
                continue;
            }
            if(last == 0x20) {
                last_space = i;
            }
        }
        if(last_space == -1) { // truncate!
            current.length = line_length;
            c += line_length;
        } else { // break at last space
            current.length = last_space + 1;
            c += last_space + 1;
        }
        // printf("adding line len = %d\n", current.length);
        text->append(current);
    }
}

void Editor :: init(Screen *scr, Keyboard *key)
{
    parent_win = scr;
    keyb = key;

    line_length = parent_win->get_size_x();
    height = parent_win->get_size_y()-1;

    printf("Line length: %d. Height: %d\n", line_length, height);
    
    window = new Screen(parent_win, 0, 1, line_length, height);
    window->draw_border();
    height -= 2;
    first_line = 0;
    draw();
}
    
void Editor :: draw(void)
{
    struct Line line;
    for(int i=0;i<height;i++) {
        window->move_cursor(0, i);
        line = (*text)[i + first_line];
        window->output_length(line.buffer, line.length);
    }
}

void Editor :: deinit()
{
    if(window)
        delete window;
}
    
int Editor :: poll(int dummy, Event &e)
{
    int ret = 0;
    char c;
        
    if(!keyb) {
        printf("Editor: Keyboard not initialized.. exit.\n");
        return -1;
    }

    c = keyb->getch();
    if(c) {
        ret = handle_key(c);
    }
    return ret;
}

int Editor :: handle_key(char c)
{
    int ret = 0;
    
    switch(c) {
        case 0x9D: // left
        case 0x03: // runstop
            ret = -1;
            break;
        case 0x11: // down
            first_line++;
            draw();
            break;
        case 0x91: // up
            if(first_line>0) {
                first_line--;
                draw();
            }
            break;
        case 0x14: // backspace
            break;
        case 0x20: // space
        case 0x0D: // return
            ret = 1;
            break;
            
        default:
            printf("Unhandled context key: %b\n", c);
    }    
    return ret;
}
