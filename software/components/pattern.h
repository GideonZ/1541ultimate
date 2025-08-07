#ifndef PATTERN_H
#define PATTERN_H

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
const char *get_filename(const char *path);
int read_line(const char *buffer, int index, char *out, int outlen);


#endif
