#include "editor.h"
#include <string.h>
#include <stdio.h>

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

Editor :: Editor(UserInterface *ui, const char *text_buffer)
{
	user_interface = ui;
	screen = NULL;
    keyb = NULL;
    window = NULL;
    line_length = 38;
    height = 0;
    linecount = 0;
    first_line = 0;
    complete_text = text_buffer;
    
    text = new IndexedList<Line>(16, empty_line);
    if(!text_buffer)
        text_buffer = (char *)test_text;
        
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

Editor :: ~Editor(void)
{
    delete text;
}

void Editor :: line_breakdown(const char *text_buffer)
{
    Line current;

    const char *c = text_buffer;
    char last;
    int last_space;
	linecount = 0;

    // printf("Line length = %d\n", line_length);
	text->clear_list();
    while(c) {
        current.buffer = c;
        last_space = -1;

        for(int i=0;i<line_length;i++) {
            last = c[i];
            if((last == 0x0a)||(last == 0x0d)||(last == 0)) {
                current.length = i;
                // printf("adding returned line = %d\n", current.length);
                text->append(current);
				linecount++;
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
		linecount++;
    }
}

void Editor :: init(Screen *scr, Keyboard *key)
{
    screen = scr;
    keyb = key;

    line_length = screen->get_size_x();
    height = screen->get_size_y()-3;

    printf("Line length: %d. Height: %d\n", line_length, height);
    
    window = new Window(screen, 0, 2, line_length, height);
    window->set_color(user_interface->color_fg);
    window->draw_border();
    height -= 2;
    first_line = 0;
    line_length -= 2;
    line_breakdown(complete_text);
    draw();
}
    
void Editor :: draw(void)
{
    struct Line line;
    int width = window->get_size_x();
    for(int i=0;i<height;i++) {
        window->move_cursor(0, i);
        line = (*text)[i + first_line];
        if (line.buffer) {
        	window->output_length(line.buffer, line.length);
        } else {
        	window->output_length("", width);
    	}
    }
}

void Editor :: deinit()
{
    if(window)
        delete window;
}
    
int Editor :: poll(int dummy)
{
    int ret = 0;
    int c;
        
    if(!keyb) {
        printf("Editor: Keyboard not initialized.. exit.\n");
        return -1;
    }

    c = keyb->getch();
    if(c > 0) {
        ret = handle_key(c);
    }
    return ret;
}

int Editor :: handle_key(uint8_t c)
{
    int ret = 0;
    
    switch(c) {
        case KEY_LEFT: // left
        case KEY_BREAK: // runstop
            ret = -1;
            break;
        case KEY_DOWN: // down
			if (first_line < linecount - height) {
				first_line++;
				draw();
			}
            break;
        case KEY_UP: // up
            if(first_line>0) {
                first_line--;
                draw();
            }
            break;
        case KEY_F1: // F1 -> page up
        case KEY_PAGEUP:
			first_line -= height + 1;
			if (first_line < 0) {
				first_line = 0;
			}
			draw();
			break;
        case KEY_F7: // F7 -> page down
        case KEY_PAGEDOWN:
        	first_line += height - 1;
			if (first_line >= linecount - height) {
				first_line = linecount - height;
				if (first_line < 0)
					first_line = 0;
			}
			draw();
			break;
        case KEY_BACK: // backspace
            break;
        case KEY_SPACE: // space
        case KEY_RETURN: // return
            ret = 1;
            break;
            
        default:
            printf("Unhandled context key: %b\n", c);
    }    
    return ret;
}
