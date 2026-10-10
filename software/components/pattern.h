#ifndef PATTERN_H
#define PATTERN_H
#include "mystring.h"

bool pattern_match(const char *p, const char *f, bool case_sensitive = false);
bool pattern_match_escaped(const char *p, const char *f, bool case_sensitive = false, bool esc_p = false, bool esc_f = false);
int  split_string(char sep, char *s, char **parts, int maxParts);
bool isEmptyString(const char *c);

void set_extension(char *buffer, const char *ext, int buf_size);
void add_extension(char *buffer, const char *ext, int buf_size);
int  get_extension(const char *name, char *ext, bool caps=false);
void truncate_filename(const char *orig, char *buf, int buf_size);
int  fix_filename(char *buffer);
void petscii_to_fat(const char *pet, char *fat, int maxlen);
void fat_to_petscii(const char *fat, bool cutExt, char *pet, int len, bool term);
// A PETSCII name as the lower/upper case character set shows it: unshifted letters lower case,
// shifted letters upper case, the case sd2iec gives a name on FAT, and every other byte without
// a character of its own as {XX}. False when the text did not fit.
bool petscii_to_text(const char *pet, char *text, int len);
// The reverse of petscii_to_text. Returns the PETSCII length, or -1 for a character
// petscii_to_text never writes, a malformed or empty escape, a zero byte or no room.
int  text_to_petscii(const char *text, char *pet, int len);
const char *get_filename(const char *path);
int read_line(const char *buffer, int index, char *out, int outlen);
void url_encode(const char *src, mstring &dest);

#endif
