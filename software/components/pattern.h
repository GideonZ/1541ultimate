#ifndef PATTERN_H
#define PATTERN_H

bool pattern_match(const char *p, const char *f, bool case_sensitive = false);
void split_string(char sep, char *s, char **parts, int maxParts);
bool isEmptyString(const char *c);

void set_extension(char *buffer, const char *ext, int buf_size);
void add_extension(char *buffer, const char *ext, int buf_size);
void get_extension(const char *name, char *ext);
void fix_filename(char *buffer);
void petscii_to_fat(const char *pet, char *fat);
void fat_to_petscii(const char *fat, bool cutExt, char *pet, int len, bool term);


#endif
