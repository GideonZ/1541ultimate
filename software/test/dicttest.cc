#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#include "dict.h"

#include <stdio.h>

int main()
{
    Dict<const char *, int> my_dict(10, NULL, 0);
    
    my_dict.set("Gideon", 47);
    my_dict.set("Lili", 43);
    my_dict.set("Iris", 11);
    my_dict.set("Gideon", 48);

    printf("%d\n", my_dict["Gideon"]);
    printf("%d\n", my_dict["Lili"]);
    printf("%d\n", my_dict["Iris"]);
    printf("%d\n", my_dict["Gael"]);
}
