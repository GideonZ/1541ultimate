#include "json.h"
#define JSMN_STRICT 1
#define JSMN_PARENT_LINKS 1
#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JSON_List *JSON::List() { return new JSON_List(); }

JSON_Object *JSON::Obj() { return new JSON_Object(); }

static const char *types[] = { "???", "OBJ", "LST", "STR", "PRM", "KEY" };

static int parse_json(char *text, size_t text_size, jsmntok_t *tokens, size_t token_count)
{
    jsmn_parser parser;
    jsmn_init(&parser);
    int num_tokens = jsmn_parse(&parser, text, text_size, tokens, token_count);
    return num_tokens;
}

static JSON *convert(char *text, jsmntok_t *tokens, size_t num_tokens)
{
    JSON **objects = (JSON **)malloc(sizeof(JSON *) * num_tokens);
    memset(objects, 0, num_tokens * sizeof(JSON *));
    bool invalidJson = false;

    for (int i = 0; i < num_tokens; i++) {
        jsmntok_t *current = &(tokens[i]);
        text[current->end] = 0;
        jsmntok_t *parent = current->parent < 0 ? NULL : (&tokens[current->parent]);

        switch (current->type) {
        case JSMN_STRING:
            // printf("STR %.*s\n", current->end - current->start, raw_space + current->start);
            //  If the parent is an object, then the string is a key value. For key values
            //  we don't create a string object.
            if (parent->type != JSMN_OBJECT) {
                objects[i] = new JSON_String(text + current->start);
            } else {
                current->type = JSMN_KEY;
            }
            break;
        case JSMN_PRIMITIVE:
            // printf("PRIM %.*s\n", current->end - current->start, raw_space + current->start);
            switch (text[current->start]) {
            case 't':
                objects[i] = new JSON_Bool(true);
                break;
            case 'f':
                objects[i] = new JSON_Bool(false);
                break;
            default:
                int value = strtol(text + current->start, NULL, 0);
                objects[i] = new JSON_Integer(value);
            }
            break;
        case JSMN_ARRAY:
            objects[i] = JSON::List();
            // printf("LIST\n");
            break;
        case JSMN_OBJECT:
            objects[i] = JSON::Obj();
            // printf("OBJ\n");
            break;
        }

        // {
        //     jsmntok *t = current;
        //     printf("%d %s (%d-%d):'%.*s' P=%d\n", i, types[t->type], t->start, t->end, t->end - t->start, text + t->start, t->parent);
        // }

        if (parent) {
            if (parent->type == JSMN_KEY) {
                if (objects[i] == NULL) {
                    //printf("Internal error. Current object is (nil)\n");
                    invalidJson = true;
                } else {
                    JSON *o = objects[parent->parent];
                    if (!o || o->type() != eObject) {
                        //printf("Can't attach current object %s to item %d\n", objects[i]->render(), parent->parent);
                        invalidJson = true;
                        delete objects[i];
                        objects[i] = NULL;
                    } else {
                        ((JSON_Object *)o)->add(text + parent->start, objects[i]);
                    }
                }
            } else if (parent->type == JSMN_ARRAY) {
                ((JSON_List *)objects[current->parent])->add(objects[i]);
            } else if (objects[i]) {
                //printf("Current object %s cannot be attached.\n", objects[i]->render());
                invalidJson = true;
                delete objects[i];
                objects[i] = NULL;
            }
        }
    }
    JSON *retval = objects[0];
    if (invalidJson) {
        delete retval;
        retval = NULL;
    }
    free(objects); // This doesn't free the objects themselves, but the array with pointers to these objects.
    return retval;
}

int convert_text_to_json_objects(char *text, size_t text_size, size_t max_tokens, JSON **out)
{
    jsmntok_t *tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * max_tokens);
    // memset(tokens, 0, max_tokens * sizeof(jsmntok_t)); // not necessary?
    int tokens_used = parse_json(text, text_size, tokens, max_tokens);

    // for (int i=0;i<tokens_used;i++) {
    //     jsmntok *t = &tokens[i];
    //     printf("%d %s (%d-%d):'%.*s' P=%d\n", i, types[t->type], t->start, t->end, t->end - t->start, text + t->start, t->parent);
    // }

    if (tokens_used < 0) {
        printf("Parsing JSON failed with error %d\n", tokens_used);
        free(tokens);
        return tokens_used;
    } else {
        printf("Parsing JSON succeeded: %d tokens.\n", tokens_used);
    }
    *out = NULL;
    *out = convert(text, tokens, tokens_used);
    free(tokens);
    if (!(*out)) {
        return JSMN_ERROR_ILLEGAL;
    }
    return tokens_used;
}

#ifdef JSON_TEST
int main()
{
    // Try to build a JSON object
    JSON_Object obj;
    obj.add("integer", 1)
        ->add("string", "koeiereet")
        ->add("list",
              JSON::List()->add("hoi")->add(1)->add(obj.Obj()->add("kaas", 3)->add("eten", "lekker"))->add("listlast"))
        ->add("objlast", -1);
    printf("%s\n", obj.render());

    JSON *list = JSON::List()->add(1)->add(2)->add(3);
    printf("%s\n", list->render());
    delete list;
}
#endif