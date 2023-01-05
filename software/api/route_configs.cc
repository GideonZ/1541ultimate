#include "routes.h"
#include "pattern.h"
#include "config.h"

void emit_store(ConfigStore *st, JSON_Object *pobj, ArgsURI &args)
{
    JSON_Object *sobj = JSON::Obj();
    pobj->add(st->get_store_name(), sobj);

    ConfigItem *i;

    for(int n = 0; n < st->items.get_elements(); n++) {
        i = st->items[n];
        if ((args.get_path_depth() < 2) || (pattern_match(args.get_path(1), i->definition->item_text))) {
            JSON *obj;            
            if ((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRFUNC)) {
                obj = new JSON_String(i->getString());
            } else if(i->definition->type == CFG_TYPE_ENUM) {
                obj = new JSON_String(i->definition->items[i->getValue()]);
            } else if(i->definition->type == CFG_TYPE_VALUE) {
                obj = new JSON_Integer(i->getValue());
            } else {
                continue;
            }
            if (args.get_path_depth() < 2) {
                sobj->add(i->definition->item_text, obj);
            } else {
                JSON_Object *ob;
                sobj->add(i->definition->item_text, ob = JSON::Obj() -> add("current", obj));
                if (i->definition->type == CFG_TYPE_ENUM) {
                    JSON_List *list = JSON::List();
                    ob->add("values", list);
                    for(int j=0;j <= i->definition->max; j++) {
                        list->add(i->definition->items[j]);
                    }
                    ob->add("default", i->definition->items[i->definition->def]);
                } else if (i->definition->type == CFG_TYPE_VALUE) {
                    ob->add("min", i->definition->min);
                    ob->add("max", i->definition->max);
                    ob->add("format", (const char *)i->definition->item_format);
                    ob->add("default", (int)i->definition->def);
                } else if((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRFUNC)) {
                    ob->add("default", (const char *)i->definition->def);
                }
            }
        }
    }
}

API_CALL(GET, configs, none, NULL, ARRAY ( { } ))
{
    ConfigManager *cfg = ConfigManager::getConfigManager();
    IndexedList<ConfigStore *> *stores = cfg->getStores();
    int path_elements = args.get_path_depth();

    if (path_elements == 0) { // nothing specified, just return categories
        JSON_List *list = JSON::List();
        resp->json->add("categories", list);
        for(int i=0; i<stores->get_elements(); i++) {
            ConfigStore *s = (*stores)[i];
            list->add(s->get_store_name());
        }
    } else {  // path specified, so the output would list the stores that match the path
        for(int i=0; i<stores->get_elements(); i++) {
            ConfigStore *s = (*stores)[i];
            if ((path_elements < 1) || pattern_match(args.get_path(0), s->get_store_name())) {
                emit_store(s, resp->json, args);
            }
        }
    }

    resp->json_response(HTTP_OK);
}

API_CALL(PUT, configs, none, NULL, ARRAY ( { {"value", P_OPTIONAL }} ))
{
    ConfigManager *cfg = ConfigManager::getConfigManager();
    IndexedList<ConfigStore *> *stores = cfg->getStores();
    int path_elements = args.get_path_depth();

    const char *valuestr = args["value"];
    if (valuestr && (path_elements != 2)) {
        resp->error("When using the 'value' argument, the path should have two elements; the config category and the "
                    "config item name. Given: '%s'",
                    args.get_full_path());
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    } else if (!valuestr && (path_elements != 3)) {
        resp->error("When not using the 'value' argument, the path should have three elements; the config category and "
                    "the config item name, followed by the setting. Given: '%s'",
                    args.get_full_path());
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (!valuestr) {
        valuestr = args.get_path(2);
    }
    bool found = false;
    ConfigStore *st;
    for(int i=0; i<stores->get_elements(); i++) {
        st = (*stores)[i];
        if (pattern_match(args.get_path(0), st->get_store_name())) {
            found = true;
            break; // found!
        }
    }

    if (!found) {
        resp->error("There is no config category that matches '%s'", args.get_path(0));
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    ConfigItem *item;
    found = false;
    for(int n = 0; n < st->items.get_elements(); n++) {
        item = st->items[n];
        if (pattern_match(args.get_path(1), item->definition->item_text)) {
            found = true;
            break;
        }
    }
    if (!found) {
        resp->error("Item '%s' not found in this configuration category [%s].", args.get_path(1), st->get_store_name());
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    // now we know what item should be configured, and how to 'read' the string
    found = false;
    int value;
    if (item->definition->type == CFG_TYPE_VALUE) {
        int value = strtol(valuestr, NULL, 0);
        if (value < item->definition->min || value > item->definition->max) {
            resp->error("Value %d is outside of the allowable range (%d-%d)", value, item->definition->min, item->definition->max);
            resp->json_response(HTTP_BAD_REQUEST);
            return;
        }
        item->setValue(value);
    } else if ((item->definition->type == CFG_TYPE_STRING) || (item->definition->type == CFG_TYPE_STRFUNC)) {
        item->setString(valuestr);
    } else if (item->definition->type == CFG_TYPE_ENUM) {
        // this is the most nasty one. Let's just iterate over the possibilities and compare the resulting strings
        for(int n = item->definition->min; n <= item->definition->max; n++) {
            if (strcasecmp(valuestr, item->definition->items[n]) == 0) {
                item->setValue(n);
                found = true;
                break;
            }
        }
        if (!found) {
            resp->error("Value '%s' is not a valid choice for item %s", valuestr, item->definition->item_text);
            resp->json_response(HTTP_NOT_FOUND);
            return;
        }
    }
    resp->json_response(HTTP_OK);
}

//API_CALL(POST, configs, none, &attachment_writer, ARRAY ( { } ))
//{
//
//}
