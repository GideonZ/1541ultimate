#ifndef CHECK_H_
#define CHECK_H_
#include <stdio.h>
extern int failures;
extern int checks;
// Counts one check and prints it: "ok" or "FAIL".
void check(bool condition, const char *what, ...);
#endif
