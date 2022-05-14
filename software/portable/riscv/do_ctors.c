/*
 * Simple loop to execute the static constructors.
 */
#include <stdio.h>

typedef void (*constructor) (void);
extern constructor __init_array_start[];
extern constructor __init_array_end[];

void _do_ctors(void)
{
    constructor* ctor;

  for (ctor = __init_array_start; ctor != &__init_array_end[0]; ctor++) {
	  printf("CTOR: %p\n", *ctor);
	  (*ctor) ();
  }
}
