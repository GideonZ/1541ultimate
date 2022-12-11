#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION

#include "json.h"
#include <stdio.h>

JSON_List *JSON::List()
{
    return new JSON_List();
}

JSON_Object *JSON::Obj()
{
    return new JSON_Object();
}

#ifdef JSON_TEST
int main()
{
    // Try to build a JSON object
    JSON_Object obj;
    obj.add("integer", 1)
        ->add("string", "koeiereet")
        ->add("list", JSON::List()
            ->add("hoi")
            ->add(1)
            ->add(obj.Obj()
                ->add("kaas", 3)
                ->add("eten", "lekker")
            )
        ->add("listlast")
    )-> add("objlast", -1);
    printf("%s\n", obj.render());

    JSON *list = JSON::List()->add(1)->add(2)->add(3);
    printf("%s\n", list->render());
    delete list;
}
#endif