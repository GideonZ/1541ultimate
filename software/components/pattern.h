#ifndef PATTERN_H
#define PATTERN_H

bool pattern_match(const char *p, const char *f, bool case_sensitive = false);
void split_string(char sep, char *s, char **parts, int maxParts);
bool isEmptyString(const char *c);

#endif
