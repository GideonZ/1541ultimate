#include "editor.h"
#include <string.h>
#include <stdio.h>
#include <dump_hex.h>

HexEditor :: HexEditor(UserInterface *ui, const char *text_buffer, int max_len) : Editor(ui, text_buffer, max_len)
{
}

void HexEditor :: line_breakdown(const char *text_buffer, int buffer_size)
{
    Line current;
    int pos = 0;
    linecount = 0;
	
    text->clear_list();
    while (pos < buffer_size) {
        current.buffer = text_buffer + pos;
        current.length = (buffer_size - pos > BYTES_PER_HEX_ROW) ? BYTES_PER_HEX_ROW : buffer_size - pos;;
        text->append(current);
        pos += current.length;
        linecount++;
    }
}

void HexEditor :: draw(int line_idx, Line *line)
{
    #define HEX_COL_START 5
    #define TXT_COL_START (HEX_COL_START + (3 * BYTES_PER_HEX_ROW))
    
    char hex_buf[CHARS_PER_HEX_ROW];
    memset(hex_buf, ' ', CHARS_PER_HEX_ROW);
    dump_hex_word(hex_buf, 0, line_idx * BYTES_PER_HEX_ROW);
    
    for (int i = 0; i < line->length; i++) {
        uint8_t c = (uint8_t) line->buffer[i];
        dump_hex_byte(hex_buf, HEX_COL_START + (3 * i), c);
        // represent all non-printable characters as '.' based on the character set used by firmware version 3.10j
        hex_buf[TXT_COL_START + i] = (char) ((c == 0 || c == 8 || c == 10 || c == 13 || (c >=20 && c <= 31) || (c >= 144 && c <= 159)) ? '.' : c);
    }
    
    window->output_length(hex_buf, CHARS_PER_HEX_ROW);
    window->repeat(' ', window->get_size_x() - CHARS_PER_HEX_ROW);
}
