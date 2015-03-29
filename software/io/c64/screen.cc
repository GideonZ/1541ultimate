#include "screen.h"
#include "integer.h"
extern "C" {
    #include "itu.h"
    #include "small_printf.h"
}

Screen :: Screen(char *b, char *c, int sx, int sy)
{
    root_base       = b;
    root_base_col   = c;
    window_base     = b;
    window_base_col = c;
    size_x     = sx;
    size_y     = sy;
    window_x   = sx; // default to full screen
    window_y   = sy;
    offset_x   = 0;
    offset_y   = 0;
    cursor_x   = 0;
    cursor_y   = 0;
    pointer    = 0;
    color      = 15;
    border_h   = 0;
    border_v   = 0;
    reverse    = 0;
    backup_chars = NULL;
    backup_color = NULL;
    allow_scroll = true;
    escape       = false;
    parent       = NULL;
    cursor_on	 = 0;
    backup();
}

Screen :: Screen(Screen *scr, int x1, int y1, int sx, int sy)
{
	x1 += scr->border_v;
	y1 += scr->border_h;

    root_base       = scr->root_base;
    root_base_col   = scr->root_base_col;
    window_base     = scr->root_base+(y1 * scr->size_x)+x1;
    window_base_col = scr->root_base_col+(y1 * scr->size_x)+x1;
//    printf("Window Created: %p %p\n", window_base, window_base_col);
    size_x     = scr->size_x;
    size_y     = scr->size_y;
    window_x   = sx;
    window_y   = sy;
    offset_x   = x1;
    offset_y   = y1;
    cursor_x   = 0;
    cursor_y   = 0;
    pointer    = 0;
    color      = 15;
    border_h   = 0;
    border_v   = 0;
    reverse    = 0;
    backup_chars = NULL;
    backup_color = NULL;
    allow_scroll = true;
    escape       = false;
    parent       = scr;
    cursor_on	 = 0;
    backup();
}

Screen :: ~Screen()
{
    restore();
}

void Screen :: dump(void)
{
    printf("Screen %p (window_base: %p) %d/%d %d/%d [%d/%d]\n", this, window_base, window_x, window_y, offset_x, offset_y, border_v, border_h);
    if(parent)
        parent->dump();
}

void Screen :: backup(void)
{
    backup_chars = new char[window_x * window_y];
    backup_color = new char[window_x * window_y];
    
    char *b = window_base;
    char *c = window_base_col;
    int i=0;
    for(int y=0;y<window_y;y++) {
        for(int x=0;x<window_x;x++) {
            backup_chars[i] = b[x];
            backup_color[i++] = c[x];
            b[x] = 0;
            c[x] = (char)color;
        }
        b+=size_x;
        c+=size_x;
    }
    cursor_x = cursor_y = 0;
    pointer = 0; //(offset_y * size_y) + offset_x;
}

void Screen :: restore(void)
{
    if (!backup_chars)
        return;
    if (!backup_color)
        return;

    while(border_h > 0) {
        border_h--;
        window_base -= size_x;
        window_base_col -= size_x;
        window_y  += 2;
    }

    while(border_v > 0) {
        border_v--;
        window_base -= 1;
        window_base_col -= 1;
        window_x  += 2;
    }
    
    char *b = window_base;
    char *c = window_base_col;

    int i=0;
    for(int y=0;y<window_y;y++) {
        for(int x=0;x<window_x;x++) {
            b[x] = backup_chars[i];
            c[x] = backup_color[i++];
        }
        b+=size_x;
        c+=size_x;
    }
    delete[] backup_chars;
    delete[] backup_color;    

    backup_chars = NULL;
    backup_color = NULL;
//    printf("Window destructed.\n");
    
}
   

void Screen :: set_color(int c)
{
    color = c;
}
    
int Screen :: get_color(void)
{
    return color;
}

void Screen :: set_color(int col, int x1, int y1, int sx, int sy, bool stop_on_change)
{
    if(x1 < 0)
        x1 = 0;
    if(y1 < 0)
        y1 = 0;
    if((sx+x1) > window_x)
        sx = window_x - x1;
    if((sy+y1) > window_y)
        sy = window_y - y1;
    if(sx < 1)
        return;
    if(sy < 1)
        return;
    int local_pointer = (y1 * size_x) + x1;
    char *c = &window_base_col[local_pointer];
    char first = (*c) & 0x0F;
    for(int y=0;y<sy;y++) {
        for(int x=0;x<sx;x++) {
            if(stop_on_change && ((c[x]&0x0F) != first))
                break;
            c[x] = (char)col;
        }
        c+=size_x;
    }
}
    
void Screen :: clear(void)
{
    char *b = window_base;
    char *c = window_base_col;
    for(int y=0;y<window_y;y++) {
        for(int x=0;x<window_x;x++) {
            b[x] = 0;
            c[x] = (char)color;
        }
        b+=size_x;
        c+=size_x;
    }
    cursor_x = cursor_y = 0;
    pointer = 0; //(offset_y * size_y) + offset_x;
    allow_scroll = true;
    reverse = 0;
}

void Screen :: no_scroll(void)
{
    allow_scroll = false;
}
    
    
void Screen :: move_cursor(int x, int y)
{
	if(cursor_on)
		window_base[pointer] ^= 0x80;

	if(x >= window_x)
        x = window_x-1;
    if(y >= window_y)
        y = window_y-1;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
        
    cursor_x = x;
    cursor_y = y;
    pointer = (y * size_x) + x;

	if(cursor_on)
		window_base[pointer] ^= 0x80;
}

void Screen :: scroll_up()
{
    char *b = window_base;
    char *c = window_base_col;
    for(int y=0;y<window_y-1;y++) {
        for(int x=0;x<window_x;x++) {
            b[x] = b[x+size_x];
            c[x] = c[x+size_x];
        }
        b+=size_x;
        c+=size_x;
    }
    for(int x=0;x<window_x;x++)
        b[x] = 0;
}

void Screen :: scroll_down()
{
    char *b = &window_base[(window_y-2)*size_x];
    char *c = &window_base_col[(window_y-2)*size_x];
    for(int y=0;y<window_y-1;y++) {
        for(int x=0;x<window_x;x++) {
            b[x+size_x] = b[x];
            c[x+size_x] = c[x];
        }
        b-=size_x;
        c-=size_x;
    }
    for(int x=0;x<window_x;x++)
        b[x+size_x] = 0;
}

void Screen :: reverse_mode(int r)
{
    reverse = r;
}
    
void Screen :: output(char c)
{
	if(escape) {
		escape = false;
		if((c >= 0x10)&&(c <= 0x1F)) {
			color = int(c - 0x10);
		}
		return;
	}
		
	if (cursor_on) {
		char *p = window_base + pointer;
		*p ^= 0x80; // remove cursor
	}
	switch(c) {
        case 0x0A:
            if(cursor_y == window_y-1) {
                if(allow_scroll)
                    scroll_up();
            } else {
                pointer += size_x;
                cursor_y++;
            }
            pointer -= cursor_x;
            cursor_x = 0;
            break;
        case 0x0D:
            pointer -= cursor_x;
            cursor_x = 0;
            break;
        case 0x08:
            // todo: backspace
            break;
        case 0x1B: // escape
        	escape = true;
        	break;
        default:
            if(reverse)
                window_base[pointer] = c | 0x80;
            else
                window_base[pointer] = c;
                
            window_base_col[pointer] = (char)color;
            pointer ++;
            cursor_x++;
        
            if(cursor_x == window_x) {
                cursor_x = 0;
                pointer -= window_x;
                if(cursor_y == window_y-1) {
                    if(allow_scroll)
                        scroll_up();
                } else {
                    pointer += size_x;
                    cursor_y++;
                }
            }
    }
	if (cursor_on) {
		char *p = window_base + pointer;
		*p ^= 0x80; // place cursor
	}
}
    
void Screen :: output(char *string)
{
    while(*string) {
        output(*(string++));
    }
}

void Screen :: output_length(char *string, int len)
{
	if(cursor_on)
		window_base[pointer] ^= 0x80;

	int temp = pointer - cursor_x;
    char c;
    int i;
    for(i=0;i<window_x;string++) {
        c = *string;
        if(i == len) {
			for(int j=i;j<window_x;j++)
				window_base[temp++] = 0;
			break;
		}
        window_base[temp] = c;
        window_base_col[temp] = (char)color;
        temp++;
        i++;
    }
    pointer = temp;
    cursor_x = i;

	if(cursor_on)
		window_base[pointer] ^= 0x80;
}

void Screen :: repeat(char a, int len)
{
	if(cursor_on)
		window_base[pointer] ^= 0x80;

	for(int i=0;i<len;i++) {
		window_base[pointer++] = a;
	}

	if(cursor_on)
		window_base[pointer] ^= 0x80;
}

void Screen :: output_line(char *string)
{
    int temp = pointer - cursor_x;
    int col  = color;
    char c;
    for(int i=0;i<window_x;string++) {
        c = *string;
        if(!c) {
			for(int j=i;j<window_x;j++)
				window_base[temp++] = 0;
			break;
		}
		
        if((c >= 0x10)&&(c <= 0x1F)) {
            col = int(c - 0x10);
            continue;
        }
        if(reverse)
            window_base[temp] = c | 0x80;
        else
            window_base[temp] = c;
            
        window_base_col[temp] = (char)col;
        temp++;
        i++;
    }
}
    
void Screen :: draw_border(void)
{
    char *scr = window_base;
    int w = window_x;
    int h = window_y;
    
    // draw box from left to right
    scr[0] = 1; // upper left corner
    for(int b=1;b<w-1;b++) {
        scr[b] = 2; // horizontal line
    }
    scr[w-1] = 3; // upper right corner
    for(int b=1;b<h-1;b++) {
        scr += size_x;
        scr[0] = 4;   // vertical bar
        scr[w-1] = 4; // vertical bar
    }
    scr += size_x;
    scr[0] = 5; // lower left corner
    for(int b=1;b<w-1;b++) {
        scr[b] = 2; // horizontal line
    }
    scr[w-1] = 6; // lower right corner
    
    window_base += size_x + 1;
    window_base_col += size_x + 1;
    window_x  -= 2;
    window_y  -= 2;
    cursor_x   = 0;
    cursor_y   = 0;
    pointer    = 0;
    border_h ++;
    border_v ++;
}

void Screen :: draw_border_horiz(void)
{
    char *scr = window_base;
    int w = window_x;
    int h = window_y;
    
    // draw box from left to right
    for(int b=0;b<w;b++) {
        scr[b] = 2; // horizontal line
    }
    scr += size_x * (h-1);
    for(int b=0;b<w;b++) {
        scr[b] = 2; // horizontal line
    }
    
    window_base += size_x;
    window_base_col += size_x;
    window_y  -= 2;
    cursor_x   = 0;
    cursor_y   = 0;
    pointer    = 0;
    border_h ++;
}

void Screen :: make_reverse(int x, int y, int len)
{
    char *scr = window_base;
    scr += (y * size_x) + x;
    for(int i=0;i<len;i++) {
        *(scr++) ^= 0x80;
    }
}

int Screen :: get_size_x(void)
{
    return window_x;
}

int Screen :: get_size_y(void)
{
    return window_y;
}


void Screen :: set_char(int x, int y, char c)
{
    char *scr = window_base;
    scr -= border_v;
    scr -= (size_x * border_h);
    scr += (y * size_x) + x;
    *scr = c;        
}
    
int console_print(Screen *screen, const char *fmt, ...)
{
	static char str[256];

    va_list ap;
    int ret;
	char *pnt = str;
	
    va_start(ap, fmt);
	if(screen) {
	    ret = _vprintf(_string_write_char, (void **)&pnt, fmt, ap);
	    _string_write_char(0, (void **)&pnt);
	    screen->output(str);
	} else {
		ret = _vprintf(_diag_write_char, (void **)&pnt, fmt, ap);
	}
    va_end(ap);
    return (ret);
}
